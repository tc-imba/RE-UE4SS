#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <UVTD/CommandLine.hpp>

namespace
{
    auto fail(const char* message) -> int
    {
        std::fprintf(stderr, "%s\n", message);
        return 1;
    }

    auto parse(std::vector<std::string> arguments) -> RC::UVTD::CommandLineOptions
    {
        std::vector<char*> argv;
        argv.reserve(arguments.size());
        for (auto& argument : arguments)
        {
            argv.emplace_back(argument.data());
        }
        return RC::UVTD::parse_command_line(static_cast<int>(argv.size()), argv.data());
    }

    auto parse_fails(std::vector<std::string> arguments, std::string_view expected) -> bool
    {
        try
        {
            parse(std::move(arguments));
        }
        catch (const std::exception& error)
        {
            return std::string_view{error.what()}.find(expected) != std::string_view::npos;
        }
        return false;
    }

    auto write_config(const std::filesystem::path& directory, std::string_view type_name, bool vtable = false, bool members = true) -> void
    {
        std::filesystem::create_directories(directory);
        std::ofstream output{directory / "object_items.json"};
        output << "[{\"name\":\"" << type_name << "\",\"valid_for_vtable\":" << (vtable ? 1 : 0)
               << ",\"valid_for_member_vars\":" << (members ? 1 : 0) << "}]";
    }

    auto read_file(const std::filesystem::path& path) -> std::string
    {
        std::ifstream input{path, std::ios::binary};
        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }
} // namespace

int main(int argc, char** argv)
{
    using namespace RC::UVTD;

    if (argc != 2)
    {
        return fail("expected the DWARF fixture path");
    }
    const auto fixture = std::filesystem::absolute(argv[1]);

    const auto options = parse({
            "UnrealVTableDumper",
            "--input",
            argv[1],
            "--engine-version",
            "5_01",
            "--config",
            "Config",
            "--member-vars",
            "--sol-bindings",
            "--vtable",
            "--debug-file",
            argv[1],
    });
    if (options.input != argv[1] || options.debug_file != std::filesystem::path{argv[1]} || options.config_dir != "Config" ||
        options.engine_version != STR("5_01") || !options.operations.should_dump_member_vars || !options.operations.should_dump_sol_bindings ||
        !options.operations.should_dump_vtable)
    {
        return fail("valid command line did not parse correctly");
    }

    if (!parse({"UnrealVTableDumper", "--help"}).show_help || command_line_help().find("--member-vars") == std::string_view::npos)
    {
        return fail("help option or help text is missing");
    }
    if (!parse_fails({"UnrealVTableDumper", "--engine-version", "5_01", "--member-vars"}, "--input"))
    {
        return fail("missing input was accepted");
    }
    if (!parse_fails({"UnrealVTableDumper", "--input", argv[1], "--member-vars"}, "--engine-version"))
    {
        return fail("missing engine version was accepted");
    }
    if (!parse_fails({"UnrealVTableDumper", "--input", argv[1], "--engine-version", "5_01"}, "operation"))
    {
        return fail("missing operation was accepted");
    }
    if (!parse_fails({"UnrealVTableDumper", "--input", argv[1], "--input", argv[1], "--engine-version", "5_01", "--member-vars"}, "duplicate --input"))
    {
        return fail("duplicate scalar option was accepted");
    }
    if (!parse_fails({"UnrealVTableDumper", "--input", argv[1], "--engine-version", "5_01", "--unknown"}, "unknown option"))
    {
        return fail("unknown option was accepted");
    }

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("uvtd_command_line_" + std::to_string(unique));
    const auto config = root / "Config";
    const auto failure_config = root / "FailureConfig";
    write_config(config, "Fixture::PrimaryBase", true, true);
    write_config(failure_config, "Fixture::MultiplePolymorphic", true, true);

    const auto original_cwd = std::filesystem::current_path();
    std::filesystem::create_directories(root);
    std::filesystem::current_path(root);

    CommandLineOptions generation{};
    generation.input = fixture;
    generation.config_dir = config;
    generation.engine_version = STR("5_01");
    generation.operations.should_dump_member_vars = true;
    generation.operations.should_dump_vtable = true;
    generation.operations.should_dump_sol_bindings = true;

    try
    {
        if (run_command_line(generation) != 0)
        {
            std::filesystem::current_path(original_cwd);
            std::filesystem::remove_all(root);
            return fail("Linux command-line generation returned failure");
        }
    }
    catch (...)
    {
        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(root);
        throw;
    }

    const auto output_root = root / "UVTD_Generated_Output";
    const auto setter = output_root / "deps" / "first" / "Unreal" / "generated_include" / "FunctionBodies" / "Platform" / "Linux" /
                        "5_01_MemberVariableLayout_DefaultSetter_Fixture__PrimaryBase.cpp";
    const auto vtable_setter = output_root / "deps" / "first" / "Unreal" / "generated_include" / "FunctionBodies" / "Platform" / "Linux" /
                               "5_01_VTableOffsets_Fixture__PrimaryBase_FunctionBody.cpp";
    const auto vtable_template = output_root / "assets" / "VTableLayoutTemplates" / "Platform" / "Linux" / "VTableLayout_5_01_Template.ini";
    const auto wrapper = output_root / "deps" / "first" / "Unreal" / "generated_include" /
                         "MemberVariableLayout_HeaderWrapper_Fixture__PrimaryBase.hpp";
    const auto sol_binding = output_root / "GeneratedSolBindings" / "SolBindings_Fixture__PrimaryBase.hpp";
    const auto virtual_source = output_root / "deps" / "first" / "Unreal" / "src" / "VersionedContainer" / "UnrealVirtualImpl" /
                                "UnrealVirtual501.cpp";
    const bool member_ok = std::filesystem::exists(setter) && read_file(setter).find("Fixture::PrimaryBase::MemberOffsets") != std::string::npos;
    const bool vtable_setter_ok = std::filesystem::exists(vtable_setter) && read_file(vtable_setter).find("Compute") != std::string::npos;
    const bool vtable_template_ok = std::filesystem::exists(vtable_template) && read_file(vtable_template).find("__uvtd_reserved_slot_0") != std::string::npos;
    const bool wrapper_ok = std::filesystem::exists(wrapper);
    const bool sol_ok = std::filesystem::exists(sol_binding);
    const bool virtual_ok = read_file(virtual_source).find("Platform/Linux/5_01_VTableOffsets_Fixture__PrimaryBase_FunctionBody.cpp") != std::string::npos;
    if (!member_ok || !vtable_setter_ok || !vtable_template_ok || !wrapper_ok || !sol_ok || !virtual_ok)
    {
        std::fprintf(stderr, "member=%d vtable_setter=%d vtable_template=%d wrapper=%d sol=%d virtual=%d\n",
                     member_ok,
                     vtable_setter_ok,
                     vtable_template_ok,
                     wrapper_ok,
                     sol_ok,
                     virtual_ok);
        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(root);
        return fail("combined Linux command did not publish all shared-generator outputs");
    }

    const auto marker = output_root / "preserve-on-failure.txt";
    std::ofstream{marker} << "keep";
    generation.config_dir = failure_config;
    bool failed_without_publish = false;
    try
    {
        run_command_line(generation);
    }
    catch (const std::exception& error)
    {
        failed_without_publish = std::string_view{error.what()}.find("multiple polymorphic direct bases") != std::string_view::npos;
    }
    if (!failed_without_publish || !std::filesystem::exists(marker))
    {
        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(root);
        return fail("failed generation replaced the last published output");
    }

    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (entry.path().filename().string().starts_with(".uvtd-stage-"))
        {
            std::filesystem::current_path(original_cwd);
            std::filesystem::remove_all(root);
            return fail("staging directory was not cleaned up");
        }
    }

    std::filesystem::current_path(original_cwd);
    std::filesystem::remove_all(root);
    return 0;
}
