#pragma once

#include <UVTD/MemberVarsOutputGenerator.hpp>
#include <UVTD/PDBNameInfo.hpp>
#include <UVTD/TypeContainer.hpp>

namespace RC::UVTD
{
    class UnrealVirtualGenerator
    {
      private:
        PDBNameInfo pdb_info;
        TypeContainer type_container;
        MemberVarsOutputPlatform output_platform;

      public:
        UnrealVirtualGenerator() = delete;

        explicit UnrealVirtualGenerator(const PDBNameInfo& pdb_info,
                                        TypeContainer container,
                                        MemberVarsOutputPlatform output_platform = MemberVarsOutputPlatform::Default)
            : pdb_info(pdb_info),
              type_container(std::move(container)),
              output_platform(output_platform)
        {
        }

      public:
        auto generate_files() -> void;

      public:
        static auto output_cleanup() -> void;
    };
} // namespace RC::UVTD