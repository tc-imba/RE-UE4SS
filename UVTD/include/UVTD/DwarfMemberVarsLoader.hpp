#pragma once

#include <filesystem>
#include <vector>

#include <UVTD/TypeContainer.hpp>

namespace RC::UVTD
{
    struct DwarfLoadOptions
    {
        bool members{};
        bool vtables{};
    };

    struct DwarfTypeRequest
    {
        File::StringType configured_name;
        std::vector<File::StringType> metadata_names;
        SymbolNameInfo name_info;
    };

    class DwarfMemberVarsLoader
    {
        std::filesystem::path m_input;
        std::vector<DwarfTypeRequest> m_requests;
        DwarfLoadOptions m_options;

      public:
        DwarfMemberVarsLoader(std::filesystem::path input,
                              std::vector<DwarfTypeRequest> requests,
                              DwarfLoadOptions options = {.members = true, .vtables = false});

        auto load() const -> TypeContainer;
        static auto requests_from_config(DwarfLoadOptions options = {.members = true, .vtables = false}) -> std::vector<DwarfTypeRequest>;
    };
} // namespace RC::UVTD
