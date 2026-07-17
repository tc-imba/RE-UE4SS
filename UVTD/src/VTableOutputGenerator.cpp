#include <UVTD/VTableOutputGenerator.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string_view>

#include <DynamicOutput/DynamicOutput.hpp>
#include <UVTD/ConfigUtil.hpp>
#include <UVTD/Helpers.hpp>

namespace RC::UVTD
{
    namespace
    {
        auto cleanup_files(const std::filesystem::path& output_path,
                           bool include_ini = false,
                           std::string_view filename_token = {}) -> void
        {
            if (!std::filesystem::exists(output_path))
            {
                return;
            }
            for (const auto& item : std::filesystem::directory_iterator(output_path))
            {
                if (item.is_directory())
                {
                    continue;
                }
                if (item.path().extension() != STR(".cpp") && item.path().extension() != STR(".hpp") &&
                    (!include_ini || item.path().extension() != STR(".ini")))
                {
                    continue;
                }
                if (!filename_token.empty() && item.path().filename().string().find(filename_token) == std::string::npos)
                {
                    continue;
                }
                File::delete_file(item.path());
            }
        }

        template <typename Dumper>
        auto emit_function_body(Dumper& function_body_dumper, const Class& class_entry, const MethodBody& function_entry) -> void
        {
            auto local_class_name = class_entry.class_name;
            if (auto pos = local_class_name.find(STR("Property")); pos != local_class_name.npos)
            {
                local_class_name.replace(0, 1, STR("F"));
            }

            function_body_dumper.send(STR("if (auto it = {}::VTableLayoutMap.find(STR(\"{}\")); it == {}::VTableLayoutMap.end())\n"),
                                      local_class_name,
                                      function_entry.name,
                                      local_class_name);
            function_body_dumper.send(STR("{\n"));
            function_body_dumper.send(
                    STR("    {}::VTableLayoutMap.emplace(STR(\"{}\"), 0x{:X});\n"),
                    local_class_name,
                    function_entry.name,
                    function_entry.offset);
            function_body_dumper.send(STR("}\n\n"));
        }
    } // namespace

    auto VTableOutputGenerator::generate_files() const -> void
    {
        const auto& pdb_info = m_generation_info;
        const auto& type_container = m_type_container;
        // Use base_version for file naming (e.g., "4_27" from "4_27_CasePreserving")
        const auto& pdb_name = pdb_info.base_version;
        // Include suffix in template filename if present
        auto pdb_full_name = pdb_info.full_name;
        // Construct filename prefix: base_version + suffix_string (e.g., "4_27" or "4_27_CasePreserving")
        auto pdb_filename_prefix = pdb_name + pdb_info.get_suffix_string();

        auto default_template_file = std::filesystem::path{STR("VTableLayout.ini")};

        Output::send(STR("Generating file '{}'\n"), default_template_file.wstring());

        auto template_file = std::format(STR("VTableLayout_{}_Template.ini"), pdb_full_name);

        Output::send(STR("Generating file '{}'\n"), template_file);

        const auto template_output_path = m_platform == MemberVarsOutputPlatform::Linux
                                                  ? vtable_templates_output_path / "Platform" / "Linux"
                                                  : vtable_templates_output_path;
        const auto function_body_output_path = m_platform == MemberVarsOutputPlatform::Linux
                                                       ? vtable_gen_function_bodies_path / "Platform" / "Linux"
                                                       : vtable_gen_function_bodies_path;

        Output::Targets<Output::NewFileDevice> ini_dumper;
        auto& ini_file_device = ini_dumper.get_device<Output::NewFileDevice>();
        ini_file_device.set_file_name_and_path(template_output_path / template_file);
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
                    });

            // Skip if no class entry or skipping based on config
            if (class_it == type_container.get_class_entries().end() || object_item.valid_for_vtable != ValidForVTable::Yes)
            {
                continue;
            }

            const auto& class_entry = class_it->second;

            // Skip if no functions
            if (class_entry.functions.empty())
            {
                continue;
            }

            // Create function body file (uses filename prefix for proper suffix handling)
            auto function_body_file = function_body_output_path /
                                      std::format(STR("{}_VTableOffsets_{}_FunctionBody.cpp"), pdb_filename_prefix, class_entry.class_name_clean);

            Output::send(STR("Generating file '{}'\n"), function_body_file.wstring());

            Output::Targets<Output::NewFileDevice> function_body_dumper;
            auto& function_body_file_device = function_body_dumper.get_device<Output::NewFileDevice>();
            function_body_file_device.set_file_name_and_path(function_body_file);
            function_body_file_device.set_formatter([](File::StringViewType string) {
                return File::StringType{string};
            });

            // Generate VTable offset entries with object_item order
            ini_dumper.send(STR("[{}]\n"), class_entry.class_name);

            if (m_platform == MemberVarsOutputPlatform::Default)
            {
                for (const auto& [function_index, function_entry] : class_entry.functions)
                {
                    emit_function_body(function_body_dumper, class_entry, function_entry);

                    // Handle INI output for function entries
                    ini_dumper.send(STR("; {}\n"), function_entry.signature.to_string());
                    ini_dumper.send(STR("{}\n"), function_entry.name);
                }
            }
            else
            {
                if (class_entry.vtable_entry_size == 0)
                {
                    throw std::runtime_error{std::format("Linux vtable entry size is missing for '{}'", to_string(class_entry.class_name))};
                }
                for (uint32_t offset = 0, slot_index = 0; offset <= class_entry.last_virtual_offset;
                     offset += class_entry.vtable_entry_size, ++slot_index)
                {
                    const auto function_it = class_entry.functions.find(offset);
                    if (function_it != class_entry.functions.end() && !function_it->second.name.empty())
                    {
                        emit_function_body(function_body_dumper, class_entry, function_it->second);
                        ini_dumper.send(STR("; {}\n"), function_it->second.signature.to_string());
                        ini_dumper.send(STR("{}\n"), function_it->second.name);
                    }
                    else
                    {
                        if (function_it != class_entry.functions.end() && !function_it->second.signature.name.empty())
                        {
                            ini_dumper.send(STR("; {} (reserved primary Itanium ABI slot {})\n"), function_it->second.signature.to_string(), slot_index);
                        }
                        else
                        {
                            ini_dumper.send(STR("; reserved primary Itanium ABI slot {}\n"), slot_index);
                        }
                        ini_dumper.send(STR("__uvtd_reserved_slot_{}\n"), slot_index);
                    }
                }
            }

            ini_dumper.send(STR("\n"));
        }
    }

    auto VTableOutputGenerator::output_cleanup(MemberVarsOutputPlatform platform) -> void
    {
        if (platform == MemberVarsOutputPlatform::Linux)
        {
            cleanup_files(vtable_gen_function_bodies_path / "Platform" / "Linux", false, "_VTableOffsets_");
            cleanup_files(vtable_templates_output_path / "Platform" / "Linux", true, "VTableLayout_");
        }
        else
        {
            cleanup_files(vtable_gen_function_bodies_path);
        }
    }
} // namespace RC::UVTD
