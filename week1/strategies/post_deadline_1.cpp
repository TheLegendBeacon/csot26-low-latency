#include "strategy.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>


namespace {

inline constexpr std::size_t WINDOW = 64;
inline constexpr double ENTRY_Z = 2.0;
inline constexpr double EXIT_Z = 0.5;
inline constexpr double ENTRY_Z_SQUARED = ENTRY_Z * ENTRY_Z;
inline constexpr double EXIT_Z_SQUARED = EXIT_Z * EXIT_Z;

inline constexpr double EPSILON_STDDEV = 1e-9;
inline constexpr double EPSILON_VARIANCE = EPSILON_STDDEV * EPSILON_STDDEV;

inline constexpr double INV_WINDOW = 1.0 / 64.0;

struct alignas(32) SymbolState {
    std::uint32_t count = 0;
    std::uint32_t head = 0;
    std::int32_t position = 0;
    std::double_t next_cache = 0;
    std::double_t rolling_sum = 0;
    std::double_t rolling_square_sum = 0;
};

double mids[16][64];

class SpecStrategy : public csot::Strategy {
public:
    std::vector<csot::Order> on_tick(const csot::Tick& t) override {
        const int index = resolve(t.symbol);
        auto& st = state_[index];

        const double mid = (t.bid_px + t.ask_px) * 0.5;

        if (st.count < WINDOW) [[unlikely]] {
            mids[index][st.head] = mid;
            st.count++;
            st.head++;
            st.rolling_sum += mid;
            st.rolling_square_sum += mid * mid;
            st.next_cache = mid;
            if (st.count < WINDOW) return {};
        }

        st.rolling_sum += mid - st.next_cache;
        st.rolling_square_sum += mid*mid - st.next_cache * st.next_cache;

        const double mean = st.rolling_sum * INV_WINDOW;
        const double variance = st.rolling_square_sum * INV_WINDOW - mean * mean;
        st.head = (st.head + 1) & 63;
        st.next_cache = mids[index][st.head];

        if (variance < EPSILON_VARIANCE) [[unlikely]] {
            return {};
        }

        const double deviation = (mid - mean);
        const double zeff = deviation * deviation;
        const double entryconst = ENTRY_Z_SQUARED * variance;

        if (st.position == 0) [[likely]] {
            if (zeff >= entryconst) [[unlikely]] {
                return {csot::Order(
                    (deviation >= 0) ? csot::Order::Side::SELL : csot::Order::Side::BUY,
                    t.symbol,
                    (deviation >= 0) ? t.bid_px : t.ask_px,
                    1
                )};
            }
            return {};
        }
        const double exitconst = EXIT_Z_SQUARED * variance;
        if (zeff <= exitconst) [[unlikely]] {
            return {csot::Order{
                (st.position > 0) ? csot::Order::Side::SELL : csot::Order::Side::BUY,
                t.symbol,
                (st.position > 0) ? t.bid_px : t.ask_px,
                static_cast<std::uint32_t>((st.position > 0) ? st.position : -st.position)
            }};
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
    alignas(32) const char* string_lookup[64]; // just use the guaranteed live memory
    alignas(32) SymbolState state_[64];

    [[nodiscard]] __attribute__((always_inline))
    int resolve(const std::string_view& s) {
        const char* target = s.data();

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

extern "C" csot::Strategy* create_strategy() {
    return new SpecStrategy();
}