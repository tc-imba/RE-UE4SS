#pragma once

#include <UVTD/PDBNameInfo.hpp>
#include <UVTD/TypeContainer.hpp>

namespace RC::UVTD
{
    enum class MemberVarsOutputPlatform
    {
        Default,
        Linux,
    };

    class MemberVarsOutputGenerator
    {
      private:
        PDBNameInfo m_generation_info;
        const TypeContainer& m_type_container;
        MemberVarsOutputPlatform m_platform{MemberVarsOutputPlatform::Default};

      public:
        MemberVarsOutputGenerator(PDBNameInfo generation_info,
                                  const TypeContainer& type_container,
                                  MemberVarsOutputPlatform platform = MemberVarsOutputPlatform::Default)
            : m_generation_info(std::move(generation_info)),
              m_type_container(type_container),
              m_platform(platform)
        {
        }

        auto generate_files() const -> void;
        static auto output_cleanup(MemberVarsOutputPlatform platform = MemberVarsOutputPlatform::Default) -> void;
    };
} // namespace RC::UVTD
