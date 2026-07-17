#pragma once

#include <iosfwd>

namespace RC::UVTD
{
    auto run_linux_interactive(std::istream& input, std::ostream& output, std::ostream& errors) -> int;
    auto run_linux_main(int argc, char** argv, std::istream& input, std::ostream& output, std::ostream& errors) -> int;
} // namespace RC::UVTD
