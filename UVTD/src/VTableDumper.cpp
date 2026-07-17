#define NOMINMAX

#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>
#include <unordered_set>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <UVTD/ConfigUtil.hpp>
#include <UVTD/Helpers.hpp>
#include <UVTD/Symbols.hpp>
#include <UVTD/VTableDumper.hpp>
#include <UVTD/VTableMethodNames.hpp>
#include <UVTD/VTableOutputGenerator.hpp>

#include <Windows.h>

#include <PDB_CoalescedMSFStream.h>
#include <PDB_GlobalSymbolStream.h>
#include <PDB_IPIStream.h>
#include <PDB_ModuleInfoStream.h>
#include <PDB_ModuleSymbolStream.h>
#include <PDB_PublicSymbolStream.h>
#include <PDB_TPIStream.h>

namespace RC::UVTD
{
    auto VTableDumper::process_class(const PDB::TPIStream& tpi_stream,
                                     const PDB::CodeView::TPI::Record* class_record,
                                     const File::StringType& name,
                                     const SymbolNameInfo& name_info) -> void
    {
        auto changed = change_prefix(name, symbols.is_425_plus);
        if (!changed.has_value()) return;

        File::StringType class_name = *changed;
        File::StringType class_name_clean = Symbols::clean_name(class_name);

        auto& class_entry = type_container.get_or_create_class_entry(class_name, class_name_clean, name_info);

        auto fields = tpi_stream.GetTypeRecord(class_record->data.LF_CLASS.field);

        auto list_size = fields->header.size - sizeof(uint16_t);
        for (size_t i = 0; i < list_size; i++)
        {
            auto field_record = (PDB::CodeView::TPI::FieldList*)((uint8_t*)&fields->data.LF_FIELD.list + i);

            switch (field_record->kind)
            {
            case PDB::CodeView::TPI::TypeRecordKind::LF_METHOD:
                process_method_overload_list(tpi_stream, field_record, class_entry);
                break;
            case PDB::CodeView::TPI::TypeRecordKind::LF_ONEMETHOD:
                process_onemethod(tpi_stream, field_record, class_entry);
                break;
            }
        }
    }

    struct MethodListEntry
    {
        uint32_t index;
        uint32_t vftable_offset;
    };

    auto VTableDumper::process_method_overload_list(const PDB::TPIStream& tpi_stream,
                                                    const PDB::CodeView::TPI::FieldList* method_record,
                                                    Class& class_entry) -> void
    {
        auto list = tpi_stream.GetTypeRecord(method_record->data.LF_METHOD.mList);

        File::StringType base_method_name = Symbols::get_method_name(method_record);
        File::StringType base_method_name_clean = Symbols::clean_name(base_method_name);

        size_t next_offset = 0;

        for (size_t i = 0; i < method_record->data.LF_METHOD.count; i++)
        {
            PDB::CodeView::TPI::Record::Data* overload_record = 
                (PDB::CodeView::TPI::Record::Data*)((uint8_t*)&list->data.LF_METHODLIST.mList + next_offset);

            next_offset += sizeof(PDB::CodeView::TPI::Record::Data::METHOD);
            if (Symbols::is_virtual(overload_record->METHOD.attributes))
            {
                next_offset += sizeof(uint32_t);
            }

            if (!Symbols::is_virtual(overload_record->METHOD.attributes)) continue;
        
            auto function_record = tpi_stream.GetTypeRecord(overload_record->METHOD.index);
            if (!function_record || function_record->header.kind != PDB::CodeView::TPI::TypeRecordKind::LF_MFUNCTION) continue;

            int32_t vtable_offset = overload_record->METHOD.vbaseoff[0];

            // Generate signature for mangling
            MethodSignature signature = symbols.generate_method_signature(tpi_stream, function_record, base_method_name);
        
            // Generate mangled suffix based on parameters
            File::StringType mangled_suffix = generate_mangled_suffix(signature);
            File::StringType mangled_name = base_method_name_clean + mangled_suffix;

            auto& function = class_entry.functions[vtable_offset];
            function.name = mangled_name;
            function.signature = signature;
            function.offset = vtable_offset;
            function.is_overload = true;
        }
    }

    auto VTableDumper::process_onemethod(const PDB::TPIStream& tpi_stream, const PDB::CodeView::TPI::FieldList* method_record, Class& class_entry) -> void
    {
        const auto is_virtual = method_record->data.LF_ONEMETHOD.attributes.mprop == (uint16_t)PDB::CodeView::TPI::MethodProperty::Intro ||
                                method_record->data.LF_ONEMETHOD.attributes.mprop == (uint16_t)PDB::CodeView::TPI::MethodProperty::PureIntro;
        if (!is_virtual) return;

        File::StringType method_name = Symbols::get_method_name(method_record);
        int32_t vtable_offset = method_record->data.LF_ONEMETHOD.vbaseoff[0];
        auto function_record = tpi_stream.GetTypeRecord(method_record->data.LF_ONEMETHOD.index);

        Output::send(STR("  method {} offset {}\n"), method_name, vtable_offset);

        File::StringType method_name_clean = Symbols::clean_name(method_name);

        auto& function = class_entry.functions[vtable_offset];
        function.name = method_name_clean;
        function.signature = symbols.generate_method_signature(tpi_stream, function_record, method_name);
        function.offset = vtable_offset;
        function.is_overload = false;
    }

    auto VTableDumper::dump_vtable_for_symbol(std::unordered_map<File::StringType, SymbolNameInfo>& names) -> void
    {
        Output::send(STR("Dumping {} struct symbols for {}\n"), names.size(), symbols.pdb_file_path.filename().stem().wstring());

        const PDB::TPIStream tpi_stream = PDB::CreateTPIStream(symbols.pdb_file);

        for (const PDB::CodeView::TPI::Record* type_record : tpi_stream.GetTypeRecords())
        {
            if (type_record->header.kind == PDB::CodeView::TPI::TypeRecordKind::LF_CLASS ||
                type_record->header.kind == PDB::CodeView::TPI::TypeRecordKind::LF_STRUCTURE)
            {
                if (type_record->data.LF_CLASS.property.fwdref) continue;

                const File::StringType class_name = Symbols::get_leaf_name(type_record->data.LF_CLASS.data, type_record->data.LF_CLASS.lfEasy.kind);
                if (!names.contains(class_name)) continue;

                const auto name_info = names.find(class_name);
                if (name_info == names.end()) continue;

                process_class(tpi_stream, type_record, class_name, name_info->second);
            }
        }
        return;
    }

    auto VTableDumper::generate_code() -> void
    {
        std::unordered_map<File::StringType, SymbolNameInfo> vtable_names;

        // Use config utility instead of hardcoded list
        for (const auto& object_item : ConfigUtil::GetObjectItems())
        {
            if (object_item.valid_for_vtable != ValidForVTable::Yes) continue;

            vtable_names.emplace(object_item.name, SymbolNameInfo{object_item.valid_for_vtable, object_item.valid_for_member_vars});
        }

        dump_vtable_for_symbol(vtable_names);
    }

    auto VTableDumper::generate_files() -> void
    {
        VTableOutputGenerator{symbols.get_pdb_name_info(), type_container}.generate_files();
    }

    auto VTableDumper::output_cleanup() -> void
    {
        VTableOutputGenerator::output_cleanup();
    }
} // namespace RC::UVTD