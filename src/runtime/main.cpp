#include "kss/bootstrap.hpp"

#include <iostream>

int main(int argc, char** argv) {
    return static_cast<int>(kss::runtime_cli(argc, argv, std::cout, std::cerr));
}
