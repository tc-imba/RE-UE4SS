#include <UVTD/LinuxInteractive.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <UVTD/CommandLine.hpp>
#include <UVTD/Config.hpp>
#include <UVTD/Helpers.hpp>

namespace RC::UVTD
{
    namespace
    {
        struct InteractiveMetadata
        {
            std::filesystem::path input;
            std::optional<std::filesystem::path> debug_file;
            File::StringType engine_version;
            std::filesystem::path config_dir{"Config"};
        };

        auto prompt_line(std::istream& input, std::ostream& output, std::string_view prompt, std::string& value) -> bool
        {
            output << prompt;
            output.flush();
            return static_cast<bool>(std::getline(input, value));
        }

        auto prompt_metadata(std::istream& input, std::ostream& output, InteractiveMetadata& metadata) -> bool
        {
            std::string value;
            if (!prompt_line(input, output, "ELF input path: ", value))
            {
                return false;
            }
            metadata.input = value;

            if (!prompt_line(input, output, "Debug file path (blank for embedded DWARF): ", value))
            {
                return false;
            }
            metadata.debug_file = value.empty() ? std::nullopt : std::optional<std::filesystem::path>{value};

            if (!prompt_line(input, output, "Engine version (for example 5_01): ", value))
            {
                return false;
            }
            metadata.engine_version = to_string_type(value.c_str());

            if (!prompt_line(input, output, "Config directory [Config]: ", value))
            {
                return false;
            }
            metadata.config_dir = value.empty() ? std::filesystem::path{"Config"} : std::filesystem::path{value};
            return true;
        }

        auto print_menu(std::ostream& output) -> void
        {
            output << "1. Generate VTable layouts\n"
                      "2. Generate class/struct member variable layouts\n"
                      "3. Generate Sol bindings\n"
                      "4. Generate everything\n"
                      "5. Change metadata input\n"
                      "9. Reload configurations\n"
                      "0. Exit\n"
                      "Selection: ";
            output.flush();
        }
    } // namespace

    auto run_linux_interactive(std::istream& input, std::ostream& output, std::ostream& errors) -> int
    {
        InteractiveMetadata metadata;
        if (!prompt_metadata(input, output, metadata))
        {
            return 0;
        }

        for (;;)
        {
            print_menu(output);
            std::string selection;
            if (!std::getline(input, selection))
            {
                return 0;
            }
            if (selection == "0")
            {
                return 0;
            }
            if (selection == "5")
            {
                if (!prompt_metadata(input, output, metadata))
                {
                    return 0;
                }
                continue;
            }
            if (selection == "9")
            {
                const auto config_dir = std::filesystem::absolute(metadata.config_dir);
                if (UVTDConfig::Get().Initialize(config_dir))
                {
                    output << "Configurations reloaded from " << config_dir.string() << "\n";
                }
                else
                {
                    errors << "Error: failed to reload configurations from " << config_dir.string() << "\n";
                }
                continue;
            }

            DumpSettings operations{};
            if (selection == "1")
            {
                operations.should_dump_vtable = true;
            }
            else if (selection == "2")
            {
                operations.should_dump_member_vars = true;
            }
            else if (selection == "3")
            {
                operations.should_dump_sol_bindings = true;
            }
            else if (selection == "4")
            {
                operations = {
                        .should_dump_vtable = true,
                        .should_dump_member_vars = true,
                        .should_dump_sol_bindings = true,
                };
            }
            else
            {
                output << "Invalid selection\n";
                continue;
            }

            try
            {
                CommandLineOptions options{
                        .input = metadata.input,
                        .debug_file = metadata.debug_file,
                        .config_dir = metadata.config_dir,
                        .engine_version = metadata.engine_version,
                        .operations = operations,
                };
                run_command_line(options);
                output << "Generation completed\n";
            }
            catch (const std::exception& error)
            {
                errors << "Error: " << error.what() << '\n';
            }
        }
    }

    auto run_linux_main(int argc, char** argv, std::istream& input, std::ostream& output, std::ostream& errors) -> int
    {
        if (argc == 1)
        {
            return run_linux_interactive(input, output, errors);
        }
        return run_command_line_main(argc, argv);
    }
} // namespace RC::UVTD
