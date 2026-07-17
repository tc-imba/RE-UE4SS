#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <UVTD/Config.hpp>
#include <UVTD/PDBNameInfo.hpp>
#include <UVTD/TypeContainer.hpp>
#include <UVTD/VTableOutputGenerator.hpp>

namespace
{
    auto read_file(const std::filesystem::path& path) -> std::string
    {
        std::ifstream stream{path, std::ios::binary};
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

    auto fail(const char* message) -> int
    {
        std::fprintf(stderr, "%s\n", message);
        return 1;
    }
}

int main()
{
    using namespace RC::UVTD;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("uvtd_vtable_writer_" + std::to_string(unique));
    std::filesystem::create_directories(root);
    const auto original_cwd = std::filesystem::current_path();
    std::filesystem::current_path(root);

    auto& config = UVTDConfig::Get();
    config.object_items = {{STR("Sample"), ValidForVTable::Yes, ValidForMemberVars::No}};

    TypeContainer container;
    Class sample{};
    sample.class_name = STR("Sample");
    sample.class_name_clean = STR("Sample");
    sample.valid_for_vtable = ValidForVTable::Yes;
    sample.last_virtual_offset = 0x18;
    sample.vtable_entry_size = 8;
    sample.functions.emplace(0x0,
                             MethodBody{
                                     .name = STR("First"),
                                     .signature = {.return_type = STR("void"), .name = STR("First")},
                                     .offset = 0x0,
                             });
    sample.functions.emplace(0x10,
                             MethodBody{
                                     .name = STR("Second_C__int"),
                                     .signature = {.return_type = STR("int"),
                                                   .name = STR("Second"),
                                                   .params = {{STR("int")}},
                                                   .qualifiers = {.is_const = true}},
                                     .offset = 0x10,
                                     .is_overload = true,
                             });
    container.get_class_entries().emplace(STR("Sample"), std::move(sample));

    const auto info = PDBNameInfo::parse(STR("5_01"));
    if (!info)
    {
        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(root);
        return fail("5_01 did not parse");
    }

    VTableOutputGenerator{*info, container}.generate_files();
    const auto default_template = root / "UVTD_Generated_Output" / "assets" / "VTableLayoutTemplates" / "VTableLayout_5_01_Template.ini";
    const auto default_setter = root / "UVTD_Generated_Output" / "deps" / "first" / "Unreal" / "generated_include" / "FunctionBodies" /
                                "5_01_VTableOffsets_Sample_FunctionBody.cpp";
    const std::string expected_template =
            "[Sample]\n"
            "; void First()\n"
            "First\n"
            "; int Second(int) const\n"
            "Second_C__int\n"
            "\n";
    const std::string expected_setter =
            "if (auto it = Sample::VTableLayoutMap.find(STR(\"First\")); it == Sample::VTableLayoutMap.end())\n"
            "{\n"
            "    Sample::VTableLayoutMap.emplace(STR(\"First\"), 0x0);\n"
            "}\n"
            "\n"
            "if (auto it = Sample::VTableLayoutMap.find(STR(\"Second_C__int\")); it == Sample::VTableLayoutMap.end())\n"
            "{\n"
            "    Sample::VTableLayoutMap.emplace(STR(\"Second_C__int\"), 0x10);\n"
            "}\n"
            "\n";

    const bool default_matches = read_file(default_template) == expected_template && read_file(default_setter) == expected_setter;

    VTableOutputGenerator{*info, container, MemberVarsOutputPlatform::Linux}.generate_files();
    const auto linux_template = root / "UVTD_Generated_Output" / "assets" / "VTableLayoutTemplates" / "Platform" / "Linux" /
                                "VTableLayout_5_01_Template.ini";
    const auto linux_setter = root / "UVTD_Generated_Output" / "deps" / "first" / "Unreal" / "generated_include" / "FunctionBodies" / "Platform" /
                              "Linux" / "5_01_VTableOffsets_Sample_FunctionBody.cpp";
    const auto linux_template_text = read_file(linux_template);
    const auto linux_setter_text = read_file(linux_setter);
    const bool linux_matches = linux_template_text.find("__uvtd_reserved_slot_1") != std::string::npos &&
                               linux_template_text.find("__uvtd_reserved_slot_3") != std::string::npos &&
                               linux_setter_text == expected_setter && linux_setter_text.find("__uvtd_reserved_slot_") == std::string::npos;

    VTableOutputGenerator::output_cleanup(MemberVarsOutputPlatform::Linux);
    const bool cleanup_is_scoped = std::filesystem::exists(default_template) && std::filesystem::exists(default_setter) &&
                                   !std::filesystem::exists(linux_template) && !std::filesystem::exists(linux_setter);

    std::filesystem::current_path(original_cwd);
    std::filesystem::remove_all(root);

    if (!default_matches) return fail("default vtable output bytes changed");
    if (!linux_matches) return fail("Linux vtable paths or reserved slots are incorrect");
    if (!cleanup_is_scoped) return fail("Linux vtable cleanup touched default output");
    return 0;
}
