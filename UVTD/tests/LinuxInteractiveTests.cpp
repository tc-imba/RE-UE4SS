#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <UVTD/LinuxInteractive.hpp>

namespace
{
    auto fail(const char* message) -> int
    {
        std::fprintf(stderr, "%s\n", message);
        return 1;
    }

    auto write_config(const std::filesystem::path& directory) -> void
    {
        std::filesystem::create_directories(directory);
        std::ofstream output{directory / "object_items.json"};
        output << "[{\"name\":\"Fixture::PrimaryBase\",\"valid_for_vtable\":1,\"valid_for_member_vars\":1}]";
    }

    auto occurrences(const std::string& text, std::string_view needle) -> size_t
    {
        size_t count = 0;
        size_t offset = 0;
        while ((offset = text.find(needle, offset)) != std::string::npos)
        {
            ++count;
            offset += needle.size();
        }
        return count;
    }
}

int main(int argc, char** argv)
{
    using namespace RC::UVTD;

    if (argc != 2)
    {
        return fail("expected the DWARF fixture path");
    }
    const auto fixture = std::filesystem::absolute(argv[1]);
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("uvtd_interactive_" + std::to_string(unique));
    const auto config = root / "Config";
    write_config(config);

    const auto original_cwd = std::filesystem::current_path();
    std::filesystem::create_directories(root);
    std::filesystem::current_path(root);

    std::istringstream scripted{
            fixture.string() + "\n\n5_01\n" + config.string() + "\n1\n2\n3\n4\n9\ninvalid\n0\n"};
    std::ostringstream output;
    std::ostringstream errors;
    const auto interactive_result = run_linux_interactive(scripted, output, errors);
    const auto output_text = output.str();
    const auto output_root = root / "UVTD_Generated_Output";
    const bool generated_everything =
            std::filesystem::exists(output_root / "assets" / "VTableLayoutTemplates" / "Platform" / "Linux" / "VTableLayout_5_01_Template.ini") &&
            std::filesystem::exists(output_root / "assets" / "MemberVarLayoutTemplates" / "MemberVariableLayout_5_01_Template.ini") &&
            std::filesystem::exists(output_root / "GeneratedSolBindings" / "SolBindings_Fixture__PrimaryBase.hpp");
    if (interactive_result != 0 || occurrences(output_text, "Generation completed") != 4 ||
        output_text.find("Configurations reloaded") == std::string::npos || output_text.find("Invalid selection") == std::string::npos ||
        !errors.str().empty() || !generated_everything)
    {
        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(root);
        return fail("scripted interactive operation choices are incorrect");
    }

    std::filesystem::remove_all(output_root);
    std::istringstream recovery{
            (root / "missing-elf").string() + "\n\n5_01\n" + config.string() + "\n1\n5\n" + fixture.string() + "\n\n5_01\n" + config.string() +
            "\n1\n0\n"};
    std::ostringstream recovery_output;
    std::ostringstream recovery_errors;
    if (run_linux_interactive(recovery, recovery_output, recovery_errors) != 0 ||
        recovery_errors.str().find("does not name a regular file") == std::string::npos ||
        recovery_output.str().find("Generation completed") == std::string::npos ||
        !std::filesystem::exists(output_root / "assets" / "VTableLayoutTemplates" / "Platform" / "Linux" / "VTableLayout_5_01_Template.ini"))
    {
        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(root);
        return fail("interactive failure recovery or change-input behavior is incorrect");
    }

    std::istringstream eof_input;
    std::ostringstream eof_output;
    std::ostringstream eof_errors;
    if (run_linux_interactive(eof_input, eof_output, eof_errors) != 0)
    {
        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(root);
        return fail("interactive EOF did not exit cleanly");
    }

    char program[] = "UnrealVTableDumper";
    char* no_argument_argv[]{program};
    std::istringstream main_eof;
    std::ostringstream main_output;
    std::ostringstream main_errors;
    if (run_linux_main(1, no_argument_argv, main_eof, main_output, main_errors) != 0 ||
        main_output.str().find("ELF input path") == std::string::npos)
    {
        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(root);
        return fail("Linux no-argument entry did not select the interactive driver");
    }

    std::filesystem::current_path(original_cwd);
    std::filesystem::remove_all(root);
    return 0;
}
