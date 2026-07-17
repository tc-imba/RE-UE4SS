#include <UVTD/CommandLine.hpp>

#include <chrono>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <UVTD/Config.hpp>
#include <UVTD/Helpers.hpp>
#include <UVTD/MemberVarsOutputGenerator.hpp>
#include <UVTD/MemberVarsWrapperGenerator.hpp>
#include <UVTD/PDBNameInfo.hpp>
#include <UVTD/SolBindingsGenerator.hpp>
#include <UVTD/UnrealVirtualGenerator.hpp>
#include <UVTD/VTableOutputGenerator.hpp>

#ifdef _WIN32
#include <UVTD/MemberVarsDumper.hpp>
#include <UVTD/Symbols.hpp>
#include <UVTD/VTableDumper.hpp>
#else
#include <UVTD/DwarfMemberVarsLoader.hpp>
#endif

namespace RC::UVTD
{
    namespace
    {
        constexpr std::string_view help_text = "Usage:\n"
                                               "  UnrealVTableDumper --input <PDB-or-ELF> --engine-version <version>\n"
                                               "    [--config <directory>] [--debug-file <path>]\n"
                                               "    [--member-vars] [--sol-bindings] [--vtable]\n"
                                               "\n"
                                               "Options:\n"
                                               "  --input <path>           PDB on Windows, ELF executable on Linux\n"
                                               "  --engine-version <name>  UVTD version identity, for example 5_01\n"
                                               "  --config <directory>     UVTD configuration directory (default: Config)\n"
                                               "  --debug-file <path>      Standalone ELF debug file on Linux\n"
                                               "  --member-vars            Generate member layouts and wrappers\n"
                                               "  --sol-bindings           Generate Sol bindings\n"
#ifdef _WIN32
                                               "  --vtable                 Generate vtables (Windows only)\n"
#else
                                               "  --vtable                 Generate native vtables\n"
#endif
                                               "  --help                   Show this help text\n";

        auto has_operations(const DumpSettings& operations) -> bool
        {
            return operations.should_dump_vtable || operations.should_dump_member_vars || operations.should_dump_sol_bindings;
        }

        auto require_value(int& index, int argc, char** argv, std::string_view option) -> std::string
        {
            if (index + 1 >= argc)
            {
                throw std::invalid_argument{std::format("{} requires a value", option)};
            }
            return argv[++index];
        }

        auto parse_generation_info(const File::StringType& engine_version) -> PDBNameInfo
        {
            const auto parsed = PDBNameInfo::parse(engine_version);
            if (!parsed)
            {
                throw std::invalid_argument{std::format("invalid --engine-version '{}' (expected a value such as 5_01)", to_string(engine_version))};
            }
            return *parsed;
        }

        auto validate_input_path(const std::filesystem::path& path, std::string_view option) -> std::filesystem::path
        {
            if (path.empty())
            {
                throw std::invalid_argument{std::format("{} is required", option)};
            }
            const auto absolute_path = std::filesystem::absolute(path);
            if (!std::filesystem::is_regular_file(absolute_path))
            {
                throw std::runtime_error{std::format("{} does not name a regular file: '{}'", option, absolute_path.string())};
            }
            return absolute_path;
        }

        auto initialize_config(const std::filesystem::path& config_dir) -> void
        {
            const auto absolute_config = std::filesystem::absolute(config_dir);
            if (!UVTDConfig::Get().Initialize(absolute_config))
            {
                throw std::runtime_error{std::format("failed to initialize UVTD configuration from '{}'", absolute_config.string())};
            }
        }

#ifdef _WIN32
        auto run_windows(const CommandLineOptions& options) -> int
        {
            if (options.debug_file)
            {
                throw std::invalid_argument{"--debug-file is only supported on Linux"};
            }

            const auto input = validate_input_path(options.input, "--input");
            if (input.filename().stem().wstring() != options.engine_version)
            {
                throw std::invalid_argument{
                        std::format("PDB stem '{}' does not match --engine-version '{}'", input.filename().stem().string(), to_string(options.engine_version))};
            }
            const auto generation_info = parse_generation_info(options.engine_version);
            initialize_config(options.config_dir);

            if (options.operations.should_dump_vtable || options.operations.should_dump_member_vars)
            {
                UnrealVirtualGenerator::output_cleanup();
            }
            if (options.operations.should_dump_vtable)
            {
                VTableDumper::output_cleanup();
            }
            if (options.operations.should_dump_member_vars)
            {
                MemberVarsDumper::output_cleanup();
                MemberVarsWrapperGenerator::output_cleanup();
            }
            if (options.operations.should_dump_sol_bindings)
            {
                SolBindingsGenerator::output_cleanup();
            }

            TypeContainer container;
            if (options.operations.should_dump_vtable)
            {
                VTableDumper dumper{Symbols{input}};
                dumper.generate_code();
                dumper.generate_files();
                container.join(dumper.get_type_container());
            }
            if (options.operations.should_dump_member_vars)
            {
                MemberVarsDumper dumper{Symbols{input}};
                dumper.generate_code();
                dumper.generate_files();
                container.join(dumper.get_type_container());
            }
            if (options.operations.should_dump_sol_bindings)
            {
                SolBindingsGenerator generator{Symbols{input}};
                generator.generate_code();
                generator.generate_files();
            }

            if (options.operations.should_dump_vtable || options.operations.should_dump_member_vars)
            {
                UnrealVirtualGenerator{generation_info, container}.generate_files();
            }
            if (options.operations.should_dump_member_vars)
            {
                MemberVarsWrapperGenerator{container}.generate_files();
            }
            return 0;
        }
#else
        class CurrentPathGuard
        {
            std::filesystem::path m_original{std::filesystem::current_path()};

          public:
            CurrentPathGuard() = default;
            CurrentPathGuard(const CurrentPathGuard&) = delete;
            auto operator=(const CurrentPathGuard&) -> CurrentPathGuard& = delete;

            ~CurrentPathGuard()
            {
                std::error_code error;
                std::filesystem::current_path(m_original, error);
            }
        };

        auto unique_sibling(const std::filesystem::path& parent, std::string_view prefix) -> std::filesystem::path
        {
            const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
            for (uint32_t attempt = 0; attempt < 1000; ++attempt)
            {
                auto candidate = parent / std::format("{}{}-{}", prefix, seed, attempt);
                if (!std::filesystem::exists(candidate))
                {
                    return candidate;
                }
            }
            throw std::runtime_error{"could not allocate a unique UVTD staging path"};
        }

        auto cleanup_path(const std::filesystem::path& path) -> void
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        auto publish_staged_output(const std::filesystem::path& working_directory, const std::filesystem::path& stage_parent) -> void
        {
            const auto staged_output = stage_parent / "UVTD_Generated_Output";
            const auto final_output = working_directory / "UVTD_Generated_Output";
            if (!std::filesystem::is_directory(staged_output))
            {
                throw std::runtime_error{"generation produced no UVTD_Generated_Output tree"};
            }

            const auto backup = unique_sibling(working_directory, ".uvtd-backup-");
            bool has_backup = false;
            if (std::filesystem::exists(final_output))
            {
                std::filesystem::rename(final_output, backup);
                has_backup = true;
            }

            try
            {
                std::filesystem::rename(staged_output, final_output);
            }
            catch (...)
            {
                if (has_backup && !std::filesystem::exists(final_output))
                {
                    std::filesystem::rename(backup, final_output);
                }
                throw;
            }

            cleanup_path(stage_parent);
            if (has_backup)
            {
                cleanup_path(backup);
            }
        }

        auto run_linux(const CommandLineOptions& options) -> int
        {
            const auto input = validate_input_path(options.input, "--input");
            const auto debug_input = options.debug_file ? validate_input_path(*options.debug_file, "--debug-file") : input;
            const auto generation_info = parse_generation_info(options.engine_version);
            initialize_config(options.config_dir);

            const DwarfLoadOptions load_options{
                    .members = options.operations.should_dump_member_vars || options.operations.should_dump_sol_bindings,
                    .vtables = options.operations.should_dump_vtable,
            };
            const auto requests = DwarfMemberVarsLoader::requests_from_config(load_options);
            if (requests.empty())
            {
                throw std::runtime_error{"configuration contains no classes enabled for the requested metadata"};
            }
            const auto container = DwarfMemberVarsLoader{debug_input, requests, load_options}.load();

            const auto working_directory = std::filesystem::current_path();
            const auto stage_parent = unique_sibling(working_directory, ".uvtd-stage-");
            std::filesystem::create_directories(stage_parent);
            try
            {
                {
                    CurrentPathGuard path_guard;
                    std::filesystem::current_path(stage_parent);

                    if (options.operations.should_dump_vtable)
                    {
                        VTableOutputGenerator::output_cleanup(MemberVarsOutputPlatform::Linux);
                        VTableOutputGenerator{generation_info, container, MemberVarsOutputPlatform::Linux}.generate_files();
                    }
                    if (options.operations.should_dump_member_vars)
                    {
                        MemberVarsOutputGenerator::output_cleanup(MemberVarsOutputPlatform::Linux);
                        MemberVarsWrapperGenerator::output_cleanup();

                        MemberVarsOutputGenerator{generation_info, container, MemberVarsOutputPlatform::Linux}.generate_files();
                        MemberVarsWrapperGenerator{container}.generate_files();
                    }
                    if (options.operations.should_dump_vtable || options.operations.should_dump_member_vars)
                    {
                        UnrealVirtualGenerator::output_cleanup();
                        UnrealVirtualGenerator{generation_info, container, MemberVarsOutputPlatform::Linux}.generate_files();
                    }
                    if (options.operations.should_dump_sol_bindings)
                    {
                        SolBindingsGenerator::output_cleanup();
                        SolBindingsGenerator{container}.generate_files();
                    }
                }
                publish_staged_output(working_directory, stage_parent);
            }
            catch (...)
            {
                cleanup_path(stage_parent);
                throw;
            }
            return 0;
        }
#endif
    } // namespace

    auto command_line_help() -> std::string_view
    {
        return help_text;
    }

    auto parse_command_line(int argc, char** argv) -> CommandLineOptions
    {
        if (argc < 2)
        {
            throw std::invalid_argument{"no command-line options were provided"};
        }

        CommandLineOptions options;
        bool seen_input = false;
        bool seen_version = false;
        bool seen_config = false;
        bool seen_debug_file = false;

        for (int index = 1; index < argc; ++index)
        {
            const std::string_view option{argv[index]};
            if (option == "--help")
            {
                if (argc != 2)
                {
                    throw std::invalid_argument{"--help cannot be combined with other options"};
                }
                options.show_help = true;
                return options;
            }
            if (option == "--input")
            {
                if (seen_input)
                {
                    throw std::invalid_argument{"duplicate --input option"};
                }
                seen_input = true;
                options.input = require_value(index, argc, argv, option);
            }
            else if (option == "--engine-version")
            {
                if (seen_version)
                {
                    throw std::invalid_argument{"duplicate --engine-version option"};
                }
                seen_version = true;
                options.engine_version = to_string_type(require_value(index, argc, argv, option).c_str());
            }
            else if (option == "--config")
            {
                if (seen_config)
                {
                    throw std::invalid_argument{"duplicate --config option"};
                }
                seen_config = true;
                options.config_dir = require_value(index, argc, argv, option);
            }
            else if (option == "--debug-file")
            {
                if (seen_debug_file)
                {
                    throw std::invalid_argument{"duplicate --debug-file option"};
                }
                seen_debug_file = true;
                options.debug_file = require_value(index, argc, argv, option);
            }
            else if (option == "--member-vars")
            {
                if (options.operations.should_dump_member_vars)
                {
                    throw std::invalid_argument{"duplicate --member-vars option"};
                }
                options.operations.should_dump_member_vars = true;
            }
            else if (option == "--sol-bindings")
            {
                if (options.operations.should_dump_sol_bindings)
                {
                    throw std::invalid_argument{"duplicate --sol-bindings option"};
                }
                options.operations.should_dump_sol_bindings = true;
            }
            else if (option == "--vtable")
            {
                if (options.operations.should_dump_vtable)
                {
                    throw std::invalid_argument{"duplicate --vtable option"};
                }
                options.operations.should_dump_vtable = true;
            }
            else
            {
                throw std::invalid_argument{std::format("unknown option '{}'", option)};
            }
        }

        if (!seen_input)
        {
            throw std::invalid_argument{"--input is required"};
        }
        if (!seen_version)
        {
            throw std::invalid_argument{"--engine-version is required"};
        }
        parse_generation_info(options.engine_version);
        if (!has_operations(options.operations))
        {
            throw std::invalid_argument{"at least one generation operation is required"};
        }
        return options;
    }

    auto run_command_line(const CommandLineOptions& options) -> int
    {
        if (options.show_help)
        {
            return 0;
        }
        if (!has_operations(options.operations))
        {
            throw std::invalid_argument{"at least one generation operation is required"};
        }
#ifdef _WIN32
        return run_windows(options);
#else
        return run_linux(options);
#endif
    }

    auto run_command_line_main(int argc, char** argv) -> int
    {
        Output::set_default_devices<Output::DebugConsoleDevice, Output::NewFileDevice>();
        auto& file_device = Output::get_device<Output::NewFileDevice>();
        file_device.set_file_name_and_path(std::filesystem::current_path() / "UVTD.log");

        try
        {
            const auto options = parse_command_line(argc, argv);
            if (options.show_help)
            {
                std::cout << command_line_help();
                return 0;
            }
            return run_command_line(options);
        }
        catch (const std::exception& error)
        {
            std::cerr << "Error: " << error.what() << '\n';
            return 1;
        }
    }
} // namespace RC::UVTD
