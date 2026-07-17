#include <cstdio>
#include <filesystem>

#include <File/File.hpp>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "uvtd_linux_file_smoke";
    const auto path = root / "nested" / "output.txt";
    std::filesystem::remove_all(root);

    {
        auto handle = RC::File::open(path,
                                     RC::File::OpenFor::Writing,
                                     RC::File::OverwriteExistingFile::Yes,
                                     RC::File::CreateIfNonExistent::Yes);
        handle.write_string_to_file(STR("UVTD Linux output\n"));
        handle.close();
    }

    auto handle = RC::File::open(path, RC::File::OpenFor::Reading);
    const auto contents = handle.read_all();
    handle.close();

    const bool passed = contents == RC::StringType{STR("UVTD Linux output\n")};
    std::filesystem::remove_all(root);
    if (!passed)
    {
        std::fprintf(stderr, "Linux file round-trip did not preserve generated output\n");
        return 1;
    }
    return 0;
}
