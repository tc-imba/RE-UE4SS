#include <format>
#include <unordered_map>

#include <DynamicOutput/DynamicOutput.hpp>
#include <UVTD/ConfigUtil.hpp>
#include <UVTD/Helpers.hpp>
#include <UVTD/MemberVarsDumper.hpp>
#include <UVTD/MemberVarsOutputGenerator.hpp>

namespace RC::UVTD
{
    auto MemberVarsDumper::process_class(const PDB::TPIStream& tpi_stream,
                                         const PDB::CodeView::TPI::Record* class_record,
                                         const File::StringType& name,
                                         const SymbolNameInfo& name_info) -> void
    {
        auto changed = change_prefix(name, symbols.is_425_plus);
        if (!changed.has_value()) return;

        File::StringType class_name = *changed;
        File::StringType class_name_clean = Symbols::clean_name(class_name);
        auto& class_entry = type_container.get_or_create_class_entry(class_name, class_name_clean, name_info);

        // Get the class size directly from the PDB numeric leaf
        const uint8_t* data = reinterpret_cast<const uint8_t*>(class_record->data.LF_CLASS.data);
        const uint8_t* data_copy = data;
        class_entry.total_size = static_cast<uint32_t>(Symbols::read_numeric(data_copy));

        auto fields = tpi_stream.GetTypeRecord(class_record->data.LF_CLASS.field);

        auto list_size = fields->header.size - sizeof(uint16_t);
        for (size_t i = 0; i < list_size; i++)
        {
            auto field_record = (PDB::CodeView::TPI::FieldList*)((uint8_t*)&fields->data.LF_FIELD.list + i);

            if (field_record->kind == PDB::CodeView::TPI::TypeRecordKind::LF_MEMBER)
            {
                process_member(tpi_stream, field_record, class_entry);
            }
        }
    }

    auto MemberVarsDumper::process_member(const PDB::TPIStream& tpi_stream, const PDB::CodeView::TPI::FieldList* field_record, Class& class_entry) -> void
    {
        // Get the original member name without renaming - this is important so we capture the true variable name
        File::StringType member_name = Symbols::get_leaf_name(field_record->data.LF_STMEMBER.name, field_record->data.LF_MEMBER.lfEasy.kind);

        auto changed = change_prefix(Symbols::get_type_name(tpi_stream, field_record->data.LF_MEMBER.index, symbols.is_x64()), symbols.is_425_plus);
        if (!changed.has_value()) return;

        File::StringType type_name = *changed;

        // Check if we should completely exclude this type based on configuration
        if (ConfigUtil::ShouldFilterType(type_name, TypeFilterCategory::CompleteExclusion))
        {
            return;
        }

        // Check if this variable already exists (to avoid duplicates)
        auto existing = std::find_if(class_entry.variables.begin(),
                                     class_entry.variables.end(),
                                     [&member_name](const MemberVariable& var) {
                                         return var.name == member_name;
                                     });

        // Extract bitfield information if applicable
        auto bitfield_info = Symbols::get_bitfield_info(tpi_stream, field_record->data.LF_MEMBER.index);

        if (existing != class_entry.variables.end())
        {
            // Update existing variable
            existing->type = type_name;
            existing->offset = *(uint16_t*)field_record->data.LF_MEMBER.offset;
            existing->is_bitfield = bitfield_info.is_bitfield;
            existing->bit_position = bitfield_info.bit_position;
            existing->bit_length = bitfield_info.bit_length;
        }
        else
        {
            // Calculate the size of this member
            uint32_t member_size = Symbols::get_type_size(tpi_stream, field_record->data.LF_MEMBER.index, symbols.is_x64());

            MemberVariable variable;
            variable.type = type_name;
            variable.name = member_name;
            variable.offset = *(uint16_t*)field_record->data.LF_MEMBER.offset;
            variable.type_index = field_record->data.LF_MEMBER.index;
            variable.size = member_size;
            variable.is_bitfield = bitfield_info.is_bitfield;
            variable.bit_position = bitfield_info.bit_position;
            variable.bit_length = bitfield_info.bit_length;

            class_entry.variables.push_back(variable);
        }
    }

    auto MemberVarsDumper::dump_member_variable_layouts(std::unordered_map<File::StringType, SymbolNameInfo>& names) -> void
    {
        Output::send(STR("Dumping {} symbols for {}\n"), names.size(), symbols.pdb_file_path.filename().stem().wstring());

        const PDB::TPIStream tpi_stream = PDB::CreateTPIStream(symbols.pdb_file);

        for (const PDB::CodeView::TPI::Record* type_record : tpi_stream.GetTypeRecords())
        {
            if (type_record->header.kind == PDB::CodeView::TPI::TypeRecordKind::LF_CLASS ||
                type_record->header.kind == PDB::CodeView::TPI::TypeRecordKind::LF_STRUCTURE)
            {
                if (type_record->data.LF_CLASS.property.fwdref) continue;

                const File::StringType class_name = Symbols::get_leaf_name(type_record->data.LF_CLASS.data, type_record->data.LF_CLASS.lfEasy.kind);
                auto class_name_final = class_name;
                unify_uobject_array_if_needed(class_name_final);
                if (!names.contains(class_name_final)) continue;

                const auto name_info = names.find(class_name_final);
                if (name_info == names.end()) continue;

                process_class(tpi_stream, type_record, class_name, name_info->second);
            }
        }
        return;
    }

    auto MemberVarsDumper::generate_code() -> void
    {
        std::unordered_map<File::StringType, SymbolNameInfo> member_vars_names;

        // Use config utility instead of hardcoded list
        for (const ObjectItem& item : ConfigUtil::GetObjectItems())
        {
            if (item.valid_for_member_vars != ValidForMemberVars::Yes) continue;
            member_vars_names.emplace(item.name, SymbolNameInfo{item.valid_for_vtable, item.valid_for_member_vars});
        }

        dump_member_variable_layouts(member_vars_names);
    }

    auto MemberVarsDumper::generate_files() -> void
    {
        MemberVarsOutputGenerator{symbols.get_pdb_name_info(), type_container}.generate_files();
    }

    auto MemberVarsDumper::output_cleanup() -> void
    {
        if (std::filesystem::exists(member_variable_layouts_gen_function_bodies_path))
        {
            for (const auto& item : std::filesystem::directory_iterator(member_variable_layouts_gen_function_bodies_path))
            {
                if (item.is_directory())
                {
                    continue;
                }
                if (item.path().extension() != STR(".hpp") && item.path().extension() != STR(".cpp"))
                {
                    continue;
                }

                File::delete_file(item.path());
            }
        }
    }

} // namespace RC::UVTD