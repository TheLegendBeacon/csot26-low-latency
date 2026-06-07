#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <deque>
#include <map>
#include "strategy.hpp"

class Engine {
public:
    double get_tsc_frequency();
    void load_ticks(const std::filesystem::path& path);
    void run(std::string strategy_path);

private:
    std::deque<std::string> names;
    std::map<std::string, int, std::less<>> named;
    std::vector<csot::Tick> loaded_ticks;
};