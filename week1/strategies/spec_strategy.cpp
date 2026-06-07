#include "strategy.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

inline constexpr std::size_t WINDOW = 64;
inline constexpr double ENTRY_Z = 2.0;
inline constexpr double ENTRY_SQUARED_Z = 4.0;

inline constexpr double EXIT_Z = 0.5;
inline constexpr double EXIT_SQUARED_Z = 0.25;
inline constexpr double EPSILON_STDDEV = 1e-9;
inline constexpr double EPSILON_BOUND = (EPSILON_STDDEV*EPSILON_STDDEV*64);
inline constexpr double INV_WINDOW = 1.0 / 64.0;

struct alignas(32) SymbolState {
    std::double_t mean = 0;
    std::double_t nvariance = 0;
    std::double_t old = 0;
    std::uint32_t count = 0;
    std::uint32_t head = 0;
    std::int32_t position = 0;
    double mids[WINDOW]{};
};

#pragma GCC push_options
#pragma GCC optimize("fast-math")
class SpecStrategy : public csot::Strategy {
public:

    void on_init() override {
        order_buf_.reserve(1);
    }

    std::vector<csot::Order> on_tick(const csot::Tick& t) override {
        auto& st = state_[resolve(t.symbol)];

        const double mid = (t.bid_px + t.ask_px) * 0.5;
        if (st.count < WINDOW) [[unlikely]] {
            st.mids[st.head] = mid;
            ++st.count;
            const double newmean = st.mean + (mid-st.mean)/st.count;
            st.nvariance += (mid-st.mean)*(mid - newmean);
            st.mean = newmean;
        } else {
            const double diff = mid-st.old;
            const double newmean = st.mean + diff * INV_WINDOW;
            st.nvariance += diff* (mid-newmean + st.old-st.mean);
            st.mids[st.head] = mid;
            st.mean = newmean;
        }

        st.head = (st.head + 1) & 63;
        st.old = st.mids[st.head];
        if (st.count < WINDOW) [[unlikely]] {
            return {};
        }

        if (st.nvariance < EPSILON_BOUND) [[unlikely]] {
            return {};
        }

        //const double z = (mid - st.mean) / stddev;
        const double deviation = (mid - st.mean) * 8;
        const double zeff = deviation * deviation;
        const double entryconst = ENTRY_SQUARED_Z * st.nvariance;

        if (st.position == 0) [[likely]] {
            if (zeff >= entryconst) [[unlikely]] {
                const bool sell = deviation > 0;
                order_buf_.clear();
                order_buf_.emplace_back(
                    sell ? csot::Order::Side::SELL : csot::Order::Side::BUY,
                    t.symbol,
                    sell ? t.bid_px : t.ask_px,
                    1u
                );
                return order_buf_;
            }
            return {};
        }
        const double exitconst = EXIT_SQUARED_Z * st.nvariance;
        if (zeff <= exitconst) [[unlikely]] {
            const bool is_long = st.position > 0;
            order_buf_.clear();
            order_buf_.emplace_back(
                is_long ? csot::Order::Side::SELL : csot::Order::Side::BUY,
                t.symbol,
                is_long ? t.bid_px : t.ask_px,
                static_cast<std::uint32_t>(is_long ? st.position : -st.position)
            );
            return order_buf_;
        }

        return {};
    }

    void on_fill(const csot::Order& o, double, std::uint32_t fill_qty) override {
        auto& st = state_[resolve(o.symbol)];
        if (o.side == csot::Order::Side::BUY) {
            st.position += static_cast<std::int32_t>(fill_qty);
        } else {
            st.position -= static_cast<std::int32_t>(fill_qty);
        }
    }

private:
    uint32_t max_symbols = 0;
    alignas(64) const char* string_lookup[64]; // just use the guaranteed live memory
    alignas(64) SymbolState state_[64];
    std::vector<csot::Order> order_buf_;

    [[nodiscard]] __attribute__((always_inline, optimize("no-tree-vectorize")))
    int resolve(const std::string_view& s) {
        const char* target = s.data();

        #pragma GCC unroll 64
        for (uint32_t i = 0; i < max_symbols; i++) {
            if (target == string_lookup[i]) {
                return i;
            }
        }
        string_lookup[max_symbols] = target;
        max_symbols++;
        return max_symbols-1;
    }
};
}  // namespace
#pragma GCC pop_options

extern "C" csot::Strategy* create_strategy() {
    return new SpecStrategy();
}
