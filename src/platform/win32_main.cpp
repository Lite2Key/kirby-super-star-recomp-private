#include "kss/bootstrap.hpp"
#include "kss/native_host.hpp"

#include <iostream>

int main(int argc, char** argv) {
    auto platform = kss::make_win32_host_platform();
    return static_cast<int>(
        kss::runtime_cli(argc, argv, std::cout, std::cerr, platform.get()));
}
