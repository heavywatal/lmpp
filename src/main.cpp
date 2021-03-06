/*! @file main.cpp
    @brief Only defines tiny main()
*/
#include "program.hpp"
#include <iostream>
#include <stdexcept>

//! Just instantiate and run Program
int main(int argc, char* argv[]) {
    std::vector<std::string> arguments(argv + 1, argv + argc);
    try {
        likeligrid::Program program(arguments);
        program.run();
    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
