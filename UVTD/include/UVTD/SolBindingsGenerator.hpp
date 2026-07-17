#pragma once

#include <optional>
#include <unordered_map>
#include <utility>

#include <File/File.hpp>
#include <UVTD/TypeContainer.hpp>

#ifdef _WIN32
#include <UVTD/Symbols.hpp>
#endif

namespace RC::UVTD
{
    class SolBindingsGenerator
    {
      private:
#ifdef _WIN32
        std::optional<Symbols> symbols;
#endif
        TypeContainer type_container;

      public:
        SolBindingsGenerator() = delete;

#ifdef _WIN32
        explicit SolBindingsGenerator(Symbols symbols) : symbols(std::move(symbols))
        {
        }
#endif

        explicit SolBindingsGenerator(TypeContainer container) : type_container(std::move(container))
        {
        }

      public:
#ifdef _WIN32
        auto generate_code() -> void;
#endif
        auto generate_files() -> void;

      public:
        static auto output_cleanup() -> void;
    };
} // namespace RC::UVTD