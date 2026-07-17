#include <iostream>

#include <UVTD/LinuxInteractive.hpp>

auto main(int argc, char** argv) -> int
{
    return RC::UVTD::run_linux_main(argc, argv, std::cin, std::cout, std::cerr);
}
