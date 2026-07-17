#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

#include <UVTD/TypeMetadata.hpp>

namespace RC::UVTD
{
    struct CommandLineOptions
    {
        std::filesystem::path input;
        std::optional<std::filesystem::path> debug_file;
        std::filesystem::path config_dir{"Config"};
        File::StringType engine_version;
        DumpSettings operations;
        bool show_help{};
    };

    auto command_line_help() -> std::string_view;
    auto parse_command_line(int argc, char** argv) -> CommandLineOptions;
    auto run_command_line(const CommandLineOptions& options) -> int;
    auto run_command_line_main(int argc, char** argv) -> int;
} // namespace RC::UVTD
