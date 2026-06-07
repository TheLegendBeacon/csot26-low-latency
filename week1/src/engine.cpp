#include "strategy.hpp"
#include "engine.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <dlfcn.h>
#include <filesystem>
#include <charconv>
#include <fstream>
#include <ios>
#include <iostream>
#include <limits>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>
#include <chrono>
#include <x86intrin.h>

#define all(x) x.begin(), x.end()

double Engine::get_tsc_frequency() {
    auto start_time = std::chrono::steady_clock::now();
    _mm_lfence();
    uint64_t start_cycles = __rdtsc();
    _mm_lfence();

    auto end_time = start_time;
    while ((end_time-start_time) < std::chrono::milliseconds(100)) {
        end_time = std::chrono::steady_clock::now();
    }
    _mm_lfence();
    uint64_t end_cycles = __rdtsc();
    _mm_lfence();

    uint64_t elapsed_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time-start_time).count();
    uint64_t elapsed_cycles = end_cycles-start_cycles;

    return static_cast<double>(elapsed_cycles) / static_cast<double>(elapsed_time);
}

void Engine::load_ticks(const std::filesystem::path& path) {
    std::ifstream file(path);
    file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::string line;
    while (std::getline(file, line)) {
        csot::Tick t;
        auto tokens = std::ranges::views::split(line, ',');

        auto iter = tokens.begin();
        std::from_chars(all(std::string_view(all((*iter)))), t.timestamp_ns);
        iter++;

        auto name_view = std::string_view((*iter).begin(), (*iter).end());
        auto it = named.find(name_view);
        if (it == named.end()) {
            names.push_back(std::string(name_view));
            named.emplace(names.back(), names.size()-1);
            t.symbol = names.back();
        } else {
            t.symbol = names[it->second];
        }

        iter++;
        std::from_chars(all(std::string_view(all((*iter)))), t.bid_px);

        iter++;
        std::from_chars(all(std::string_view(all((*iter)))), t.ask_px);

        iter++;
        std::from_chars(all(std::string_view(all((*iter)))), t.bid_qty);

        iter++;
        std::from_chars(all(std::string_view(all((*iter)))), t.ask_qty);

        loaded_ticks.push_back(t);
    }
    std::cout << loaded_ticks.size() << " tick(s) loaded.\n";
}

void Engine::run(std::string strategy_path) {
    double ghz = get_tsc_frequency();
    std::cout << "Running with " << ghz << "GHz.\n";

    std::vector<u_int32_t> deltas(loaded_ticks.size());
    void* h = dlopen(strategy_path.data(), RTLD_NOW);
    auto  make = (csot::Strategy*(*)())dlsym(h, "create_strategy");
    csot::Strategy* strategy = make();
    strategy->on_init();

    size_t tickcount = 0;
    for (const auto& tick: loaded_ticks) {
        _mm_lfence();
        auto c1 = __rdtsc();
        _mm_lfence();
        std::vector<csot::Order> orders = strategy->on_tick(tick);
        _mm_lfence();
        auto c2 = __rdtsc();
        if (!orders.empty()) {
            for (const auto& o: orders) {
                double fill_px = (o.side == csot::Order::Side::BUY) ? tick.ask_px : tick.bid_px;
                uint32_t fill_qty = std::min(o.qty, (o.side == csot::Order::Side::BUY) ? tick.ask_qty : tick.bid_qty);
            
                if (fill_qty > 0) {
                    strategy->on_fill(o, fill_px, fill_qty);
                }
            }
        }
        deltas[tickcount] = c2 - c1;
        tickcount++;
    }
    sort(all(deltas));
    std::cout << "count = " << deltas.size() << '\n'
        << "p50  = " << round(deltas[tickcount * 0.50]/ghz * 100)/100 << " ns\n"
        << "p90  = " << round(deltas[tickcount * 0.90]/ghz * 100)/100 << " ns\n"
        << "p99  = " << round(deltas[tickcount * 0.99]/ghz * 100)/100 << " ns\n"
        << "p999 = " << round(deltas[tickcount * 0.999]/ghz * 100)/100 << " ns\n";
}