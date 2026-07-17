#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <UVTD/Config.hpp>
#include <UVTD/MemberVarsOutputGenerator.hpp>
#include <UVTD/PDBNameInfo.hpp>
#include <UVTD/TypeContainer.hpp>
#include <UVTD/UnrealVirtualGenerator.hpp>

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
    const auto root = std::filesystem::temp_directory_path() /
                      ("uvtd_member_writer_" + std::to_string(unique));
    std::filesystem::create_directories(root);
    const auto original_cwd = std::filesystem::current_path();
    std::filesystem::current_path(root);

    auto& config = UVTDConfig::Get();
    config.object_items = {{STR("Sample"), ValidForVTable::Yes, ValidForMemberVars::Yes}};
    config.member_rename_map.clear();
    config.uprefix_to_fprefix.clear();

    TypeContainer container;
    Class sample{};
    sample.class_name = STR("Sample");
    sample.class_name_clean = STR("Sample");
    sample.total_size = 0x10;
    sample.valid_for_vtable = ValidForVTable::Yes;
    sample.valid_for_member_vars = ValidForMemberVars::Yes;
    sample.functions.emplace(0,
                             MethodBody{
                                     .name = STR("Virtual"),
                                     .signature = {.return_type = STR("void"), .name = STR("Virtual")},
                                     .offset = 0,
                             });
    sample.variables = {
        MemberVariable{.type = STR("int"), .name = STR("Value"), .offset = 0x4, .size = 0x4},
        MemberVariable{.type = STR("uint8"),
                       .name = STR("Flags"),
                       .offset = 0x8,
                       .size = 0x1,
                       .is_bitfield = true,
                       .bit_position = 0,
                       .bit_length = 3},
    };
    container.get_class_entries().emplace(STR("Sample"), std::move(sample));

    const auto info = PDBNameInfo::parse(STR("5_01"));
    if (!info)
    {
        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(root);
        return fail("5_01 did not parse");
    }

    MemberVarsOutputGenerator{*info, container}.generate_files();

    const auto ini_path = root / "UVTD_Generated_Output" / "assets" /
                          "MemberVarLayoutTemplates" /
                          "MemberVariableLayout_5_01_Template.ini";
    const auto setter_path = root / "UVTD_Generated_Output" / "deps" / "first" /
                             "Unreal" / "generated_include" / "FunctionBodies" /
                             "5_01_MemberVariableLayout_DefaultSetter_Sample.cpp";

    const std::string expected_ini =
        "[Sample]\n"
        "; Total Size: 0x10\n"
        "; int                                 Size: 0x0004\n"
        "Value = 0x4\n"
        "; uint8                               Size: 0x0001\n"
        "Flags = 0x8:0:3:1\n"
        "UEP_TotalSize = 0x10\n"
        "\n";
    const std::string expected_setter =
        "if (auto it = Sample::MemberOffsets.find(STR(\"Value\")); it == Sample::MemberOffsets.end())\n"
        "{\n"
        "    Sample::MemberOffsets.emplace(STR(\"Value\"), 0x4);\n"
        "}\n"
        "\n"
        "if (auto it = Sample::MemberOffsets.find(STR(\"Flags\")); it == Sample::MemberOffsets.end())\n"
        "{\n"
        "    Sample::MemberOffsets.emplace(STR(\"Flags\"), 0x8);\n"
        "    Sample::BitfieldInfos.emplace(STR(\"Flags\"), BitfieldInfo{0, 3, 1});\n"
        "}\n"
        "\n"
        "if (auto it = Sample::MemberOffsets.find(STR(\"UEP_TotalSize\")); it == Sample::MemberOffsets.end())\n"
        "{\n"
        "    Sample::MemberOffsets.emplace(STR(\"UEP_TotalSize\"), 0x10);\n"
        "}\n";

    const bool ini_matches = read_file(ini_path) == expected_ini;
    const bool setter_matches = read_file(setter_path) == expected_setter;

    UnrealVirtualGenerator{*info, container}.generate_files();
    const auto virtual_source_path = root / "UVTD_Generated_Output" / "deps" /
                                     "first" / "Unreal" / "src" /
                                     "VersionedContainer" / "UnrealVirtualImpl" /
                                     "UnrealVirtual501.cpp";
    const auto default_virtual_source = read_file(virtual_source_path);
    const std::string expected_default_virtual_source =
        "#include <Unreal/VersionedContainer/UnrealVirtualImpl/UnrealVirtual501.hpp>\n"
        "\n"
        "#include <functional>\n"
        "\n"
        "// These are all the structs that have virtuals that need to have their offset set\n"
        "\n"
        "namespace RC::Unreal\n"
        "{\n"
        "    void UnrealVirtual501::set_virtual_offsets()\n"
        "    {\n"
        "#include <FunctionBodies/5_01_VTableOffsets_Sample_FunctionBody.cpp>\n"
        "\n"
        "#include <FunctionBodies/5_01_MemberVariableLayout_DefaultSetter_Sample.cpp>\n"
        "    }\n"
        "}\n";
    const bool default_virtual_matches =
        default_virtual_source == expected_default_virtual_source;

    MemberVarsOutputGenerator{*info, container, MemberVarsOutputPlatform::Linux}.generate_files();
    const auto linux_setter_path = root / "UVTD_Generated_Output" / "deps" /
                                   "first" / "Unreal" / "generated_include" /
                                   "FunctionBodies" / "Platform" / "Linux" /
                                   "5_01_MemberVariableLayout_DefaultSetter_Sample.cpp";
    const bool linux_setter_matches = read_file(linux_setter_path) == expected_setter;

    UnrealVirtualGenerator{*info, container, MemberVarsOutputPlatform::Linux}.generate_files();
    const auto linux_virtual_source = read_file(virtual_source_path);
    const std::string expected_linux_include =
        "#if PLATFORM_LINUX\n"
        "#include <FunctionBodies/Platform/Linux/5_01_MemberVariableLayout_DefaultSetter_Sample.cpp>\n"
        "#else\n"
        "#include <FunctionBodies/5_01_MemberVariableLayout_DefaultSetter_Sample.cpp>\n"
        "#endif\n";
    const std::string expected_linux_vtable_include =
        "#if PLATFORM_LINUX\n"
        "#include <FunctionBodies/Platform/Linux/5_01_VTableOffsets_Sample_FunctionBody.cpp>\n"
        "#else\n"
        "#include <FunctionBodies/5_01_VTableOffsets_Sample_FunctionBody.cpp>\n"
        "#endif\n";
    const bool linux_virtual_matches =
        linux_virtual_source.find(expected_linux_include) != std::string::npos &&
        linux_virtual_source.find(expected_linux_vtable_include) != std::string::npos;

    std::filesystem::current_path(original_cwd);
    std::filesystem::remove_all(root);

    if (!ini_matches) return fail("member layout template bytes changed");
    if (!setter_matches) return fail("default setter bytes changed");
    if (!default_virtual_matches) return fail("default virtual output changed");
    if (!linux_setter_matches) return fail("Linux setter did not reuse default bytes");
    if (!linux_virtual_matches) return fail("Linux virtual include selection is incorrect");
    return 0;
}
