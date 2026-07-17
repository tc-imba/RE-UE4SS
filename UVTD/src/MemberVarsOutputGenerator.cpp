#include <algorithm>
#include <format>
#include <unordered_set>

#include <DynamicOutput/DynamicOutput.hpp>
#include <UVTD/ConfigUtil.hpp>
#include <UVTD/Helpers.hpp>
#include <UVTD/MemberVarsOutputGenerator.hpp>

namespace RC::UVTD
{
    auto MemberVarsOutputGenerator::generate_files() const -> void
    {
        const auto& pdb_info = m_generation_info;
        const auto& type_container = m_type_container;
        // Use full name for templates (includes suffixes like CasePreserving)
        const auto& pdb_full_name = pdb_info.full_name;
        // Use base version for function body files (these match the base version)
        const auto& pdb_base_version = pdb_info.base_version;
        // Use version_no_separator for class names (e.g., "427")
        const auto& pdb_name_no_underscore = pdb_info.version_no_separator;
        // Construct filename prefix: base_version + suffix_string (e.g., "4_27" or "4_27_CasePreserving")
        auto pdb_filename_prefix = pdb_base_version + pdb_info.get_suffix_string();
        const auto setter_output_path = m_platform == MemberVarsOutputPlatform::Linux
                                                ? member_variable_layouts_gen_function_bodies_path / "Platform" / "Linux"
                                                : member_variable_layouts_gen_function_bodies_path;

        auto template_file = std::format(STR("MemberVariableLayout_{}_Template.ini"), pdb_full_name);

        Output::send(STR("Generating file '{}'\n"), template_file);

        Output::Targets<Output::NewFileDevice> ini_dumper;
        auto& ini_file_device = ini_dumper.get_device<Output::NewFileDevice>();
        ini_file_device.set_file_name_and_path(member_variable_layouts_templates_output_path / template_file);
        ini_file_device.set_formatter([](File::StringViewType string) {
            return File::StringType{string};
        });

        // Iterate through object_items first to preserve order
        for (const auto& object_item : ConfigUtil::GetObjectItems())
        {
            const auto& class_name = object_item.name;

            // Find the corresponding class entry
            auto class_it = std::find_if(
                    type_container.get_class_entries().begin(),
                    type_container.get_class_entries().end(),
                    [&class_name](const auto& entry) {
                        return entry.first == class_name || entry.second.class_name == class_name;
                    }
                    );

            // Skip if no class entry or skipping based on config
            if (class_it == type_container.get_class_entries().end() ||
                object_item.valid_for_member_vars != ValidForMemberVars::Yes)
            {
                continue;
            }

            const auto& class_entry = class_it->second;

            if (class_entry.variables.empty())
            {
                continue;
            }

            File::StringType final_class_name_clean = class_entry.class_name_clean;
            unify_uobject_array_if_needed(final_class_name_clean);

            // Use filename prefix for function body files (base_version + suffix with underscore)
            auto default_setter_src_file = setter_output_path /
                                           std::format(STR("{}_MemberVariableLayout_DefaultSetter_{}.cpp"), pdb_filename_prefix, final_class_name_clean);

            Output::send(STR("Generating file '{}'\n"), default_setter_src_file.wstring());

            Output::Targets<Output::NewFileDevice> default_setter_src_dumper;
            auto& default_setter_src_file_device = default_setter_src_dumper.get_device<Output::NewFileDevice>();
            default_setter_src_file_device.set_file_name_and_path(default_setter_src_file);
            default_setter_src_file_device.set_formatter([](File::StringViewType string) {
                return File::StringType{string};
            });

            File::StringType final_class_name = class_entry.class_name;
            unify_uobject_array_if_needed(final_class_name);

            ini_dumper.send(STR("[{}]\n"), final_class_name);
            // Output total size as a comment at the top
            ini_dumper.send(STR("; Total Size: 0x{:X}\n"), class_entry.total_size);

            // Track variables we've already processed to avoid duplicates
            std::unordered_set<File::StringType> processed_variables;

            // Iterate through sorted variables with formatted type info
            for (size_t i = 0; i < class_entry.variables.size(); ++i)
            {
                const auto& variable = class_entry.variables[i];

                // Calculate padding to next member
                uint32_t padding = 0;
                if (i + 1 < class_entry.variables.size())
                {
                    uint32_t current_end = variable.offset + variable.size;
                    uint32_t next_start = class_entry.variables[i + 1].offset;
                    if (next_start > current_end)
                    {
                        padding = next_start - current_end;
                    }
                }

                // Output type info line with formatted columns
                // Format: Type (padded to 35 chars) | Size | Padding (if present)
                if (padding > 0)
                {
                    ini_dumper.send(fmt::format(STR("; {:<35} Size: 0x{:04X}  Padding: 0x{:X}\n"),
                                                variable.type,
                                                variable.size,
                                                padding));
                }
                else
                {
                    ini_dumper.send(fmt::format(STR("; {:<35} Size: 0x{:04X}\n"),
                                                variable.type,
                                                variable.size));
                }

                // Output the actual member assignment
                // For bitfields, include bit position, length, and storage size: offset:bit_pos:bit_len:storage_size
                if (variable.is_bitfield)
                {
                    ini_dumper.send(fmt::format(STR("{} = 0x{:X}:{}:{}:{}\n"), variable.name, variable.offset, variable.bit_position, variable.bit_length, variable.size));
                }
                else
                {
                    ini_dumper.send(fmt::format(STR("{} = 0x{:X}\n"), variable.name, variable.offset));
                }

                // Check if this is a name that should be renamed for code generation
                auto rename_info = ConfigUtil::GetMemberRenameInfo(class_entry.class_name, variable.name);
                File::StringType final_variable_name = variable.name;
                if (rename_info.has_value())
                {
                    final_variable_name = rename_info->mapped_name;
                }

                // Skip if we've already processed this variable to avoid duplicates
                if (processed_variables.find(final_variable_name) != processed_variables.end())
                {
                    continue;
                }
                processed_variables.insert(final_variable_name);

                // Generate the default setter code
                default_setter_src_dumper.send(STR("if (auto it = {}::MemberOffsets.find(STR(\"{}\")); it == {}::MemberOffsets.end())\n"),
                                               final_class_name,
                                               final_variable_name,
                                               final_class_name);
                default_setter_src_dumper.send(STR("{\n"));
                default_setter_src_dumper.send(
                        STR("    {}::MemberOffsets.emplace(STR(\"{}\"), 0x{:X});\n"),
                        final_class_name,
                        final_variable_name,
                        variable.offset);
                // For bitfields, also populate BitfieldInfos with bit position, length, and storage size
                if (variable.is_bitfield)
                {
                    default_setter_src_dumper.send(
                            STR("    {}::BitfieldInfos.emplace(STR(\"{}\"), BitfieldInfo{{{}, {}, {}}});\n"),
                            final_class_name,
                            final_variable_name,
                            variable.bit_position,
                            variable.bit_length,
                            variable.size);
                }
                default_setter_src_dumper.send(STR("}\n\n"));
            }

            // Output UEP_TotalSize at the end of the class section
            ini_dumper.send(STR("UEP_TotalSize = 0x{:X}\n"), class_entry.total_size);

            // Add UEP_TotalSize to the default setter
            {
                File::StringType total_size_class_name = final_class_name;
                unify_uobject_array_if_needed(total_size_class_name);
                default_setter_src_dumper.send(STR("if (auto it = {}::MemberOffsets.find(STR(\"UEP_TotalSize\")); it == {}::MemberOffsets.end())\n"),
                                               total_size_class_name, total_size_class_name);
                default_setter_src_dumper.send(STR("{\n"));
                default_setter_src_dumper.send(STR("    {}::MemberOffsets.emplace(STR(\"UEP_TotalSize\"), 0x{:X});\n"),
                                               total_size_class_name, class_entry.total_size);
                default_setter_src_dumper.send(STR("}\n"));
            }

            ini_dumper.send(STR("\n"));
        }
    }

    auto MemberVarsOutputGenerator::output_cleanup(MemberVarsOutputPlatform platform) -> void
    {
        const auto output_path = platform == MemberVarsOutputPlatform::Linux
                                         ? member_variable_layouts_gen_function_bodies_path / "Platform" / "Linux"
                                         : member_variable_layouts_gen_function_bodies_path;
        if (std::filesystem::exists(output_path))
        {
            for (const auto& item : std::filesystem::directory_iterator(output_path))
            {
                if (item.is_directory())
                {
                    continue;
                }
                if (item.path().extension() != STR(".hpp") && item.path().extension() != STR(".cpp"))
                {
                    continue;
                }
                if (platform == MemberVarsOutputPlatform::Linux &&
                    item.path().filename().string().find("_MemberVariableLayout_DefaultSetter_") == std::string::npos)
                {
                    continue;
                }

                File::delete_file(item.path());
            }
        }
    }

} // namespace RC::UVTD
