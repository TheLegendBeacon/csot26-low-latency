#include "engine.hpp"
#include <filesystem>
#include <iostream>


int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "./quant_runner <strategy.so> <ticks.csv>\n";
        return 1;
    }

    if (!std::filesystem::exists(argv[2])) {
        std::cout << "Invalid Path.\n";
        return 1; 
    }

    Engine runner;
    runner.load_ticks(argv[2]);
    runner.run(argv[1]);
    return 0;
}