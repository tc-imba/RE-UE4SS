#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <UVTD/Config.hpp>

int main()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("uvtd_config_without_platform_layouts_" + std::to_string(unique));
    std::filesystem::create_directories(root);

    {
        std::ofstream object_items{root / "object_items.json", std::ios::binary};
        object_items << R"([{"name":"Sample","valid_for_vtable":0,"valid_for_member_vars":1}])";
    }

    auto& config = RC::UVTD::UVTDConfig::Get();
    const bool initialized = config.Initialize(root);
    const bool loaded_sample = config.object_items.size() == 1 &&
                               config.object_items.front().name == STR("Sample");

    std::filesystem::remove_all(root);

    if (!initialized)
    {
        std::fprintf(stderr, "configuration still requires the obsolete platform layout file\n");
        return 1;
    }
    if (!loaded_sample)
    {
        std::fprintf(stderr, "configuration did not load object_items.json\n");
        return 1;
    }
    return 0;
}
