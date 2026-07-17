#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <UVTD/Config.hpp>
#include <UVTD/SolBindingsGenerator.hpp>
#include <UVTD/TypeContainer.hpp>

namespace
{
    auto read_file(const std::filesystem::path& path) -> std::string
    {
        std::ifstream stream{path, std::ios::binary};
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }
}

int main()
{
    using namespace RC::UVTD;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("uvtd_sol_type_container_" + std::to_string(unique));
    std::filesystem::create_directories(root);
    const auto original_cwd = std::filesystem::current_path();
    std::filesystem::current_path(root);

    auto& config = UVTDConfig::Get();
    config.types_to_filter.clear();
    config.member_rename_map.clear();

    TypeContainer container;
    Class sample{};
    sample.class_name = STR("Sample");
    sample.class_name_clean = STR("Sample");
    sample.variables = {
        MemberVariable{.type = STR("int"), .name = STR("Value"), .offset = 4, .size = 4},
    };
    container.get_class_entries().emplace(STR("Sample"), std::move(sample));

    SolBindingsGenerator{std::move(container)}.generate_files();

    const auto output = read_file(root / "UVTD_Generated_Output" /
                                  "GeneratedSolBindings" /
                                  "SolBindings_Sample.hpp");
    const std::string expected =
        "auto sol_class_Sample = sol().new_usertype<Sample>(\"Sample\",\n"
        "    \"GetValue\", static_cast<int&(Sample::*)()>(&Sample::GetValue)\n"
        ");\n";

    std::filesystem::current_path(original_cwd);
    std::filesystem::remove_all(root);

    if (output != expected)
    {
        std::fprintf(stderr, "Sol bindings did not consume the supplied TypeContainer\n");
        return 1;
    }
    return 0;
}
