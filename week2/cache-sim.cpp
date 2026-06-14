#include "cache_sim.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <emmintrin.h>
#include <immintrin.h>
#include <sys/types.h>

namespace {
#pragma GCC push_options
#pragma GCC target("avx512f,avx512vl")
#pragma GCC optimize("unroll-loops,O3")
class StubCacheSim final : public csot::CacheSim {
public:
    void on_init() override {
        std::fill(L1_lru, L1_lru+64, 0x0706050403020100ULL);
        std::fill(L2_lru, L2_lru+512, 0x0706050403020100ULL);

        for (int set = 0; set < 64; ++set) {
            for (int way = 0; way < 8; ++way) {
                L1[(set << 3) + way] = 0xFFFFFFFFFFFFFF00ULL | (~set & 63);
            }
        }
        for (int set = 0; set < 512; ++set) {
            for (int way = 0; way < 8; ++way) {
                L2[(set << 3) + way] = 0xFFFFFFFFFFFFFF00ULL | (~set & 511);
            }
        }
    }

    inline void update_lru1_cache(uint32_t bucket, int found) {
        uint64_t raw = L1_lru[bucket];
        uint32_t shift_bits = found << 3;
        __m128i lru1_string = _mm_cvtsi64_si128(raw);
        __m128i rank_register = _mm_set1_epi8((raw >> (shift_bits)) & 255);
        __m128i mask_lru1 = _mm_cmpgt_epi8(lru1_string, rank_register);
        __m128i added = _mm_add_epi8(lru1_string, mask_lru1);
        uint64_t result = _mm_cvtsi128_si64(added);
        result &= ~(0xFFULL << shift_bits);  
        result |= (7ULL << shift_bits);
        L1_lru[bucket] = result;
    }

    inline void update_lru2_cache(uint32_t bucket2, int found2) {
        uint64_t raw = L2_lru[bucket2];
        uint32_t shift_bits2 = found2 << 3;
        __m128i lru2_string = _mm_cvtsi64_si128(raw);
        __m128i rank_register2 = _mm_set1_epi8((raw >> shift_bits2) & 255);
        __m128i mask_lru2 = _mm_cmpgt_epi8(lru2_string, rank_register2);
        __m128i added2 = _mm_add_epi8(lru2_string, mask_lru2);
        uint64_t result2 = _mm_cvtsi128_si64(added2);
        result2 &= ~(0xFFULL << shift_bits2);  
        result2 |= (7ULL << shift_bits2);
        L2_lru[bucket2] = result2;
    }

    inline int search_L1(uint64_t b, uint32_t bucket) {
        int found = -1;

        #if defined(__AVX512F__) && defined(__AVX512VL__)
        __m512i addr = _mm512_set1_epi64(b);
        __m512i cache_line = _mm512_load_si512(reinterpret_cast<const __m512i*>(&L1[bucket << 3]));
        __mmask8 mask = _mm512_cmpeq_epi64_mask(addr, cache_line);
        int e = std::countr_zero(mask);
        if (e < 8) found = e;
        #else
        __m256i addr = _mm256_set1_epi64x(b);
        const __m256i* startaddr = reinterpret_cast<const __m256i*>(&L1[bucket * 8]);
        __m256i addr_lo = _mm256_load_si256(startaddr);
        __m256i addr_hi = _mm256_load_si256(startaddr + 1);

        __m256i cmp_lo = _mm256_cmpeq_epi64(addr, addr_lo);
        __m256i cmp_hi = _mm256_cmpeq_epi64(addr, addr_hi);
        uint32_t mask = (_mm256_movemask_pd(_mm256_castsi256_pd(cmp_hi)) << 4) | 
            _mm256_movemask_pd(_mm256_castsi256_pd(cmp_lo));
        int e = std::countr_zero(mask);
        if (e < 8) found = e;
        #endif
        return found;
    }

    inline int search_L2(uint64_t b, uint32_t bucket) {
        int found = -1;

        #if defined(__AVX512F__) && defined(__AVX512VL__)
        __m512i addr = _mm512_set1_epi64(b);
        __m512i cache_line = _mm512_load_si512(reinterpret_cast<const __m512i*>(&L2[bucket << 3]));
        __mmask8 mask = _mm512_cmpeq_epi64_mask(addr, cache_line);
        int e = std::countr_zero(mask);
        if (e < 8) found = e;
        #else
        __m256i addr = _mm256_set1_epi64x(b);
        const __m256i* startaddr = reinterpret_cast<const __m256i*>(&L2[bucket * 8]);
        __m256i addr_lo = _mm256_load_si256(startaddr);
        __m256i addr_hi = _mm256_load_si256(startaddr + 1);

        __m256i cmp_lo = _mm256_cmpeq_epi64(addr, addr_lo);
        __m256i cmp_hi = _mm256_cmpeq_epi64(addr, addr_hi);
        uint32_t mask = (_mm256_movemask_pd(_mm256_castsi256_pd(cmp_hi)) << 4) | 
            _mm256_movemask_pd(_mm256_castsi256_pd(cmp_lo));
        int e = std::countr_zero(mask);
        if (e < 8) found = e;
        #endif
        return found;
    }

    inline int get_current_lru_L1(uint32_t bucket) {
        uint64_t current_lru_bucket = L1_lru[bucket];
        uint64_t has_zero = (current_lru_bucket - 0x0101010101010101ULL) & ~current_lru_bucket & 0x8080808080808080ULL; // HOW DOES THIS WORK?? LEARN LATER
        int current_lru_L1 = std::countr_zero(has_zero) >> 3;

        return current_lru_L1;
    }

    inline int get_current_lru_L2(uint32_t bucket) {
        uint64_t current_lru_bucket = L2_lru[bucket];
        uint64_t has_zero = (current_lru_bucket - 0x0101010101010101ULL) & ~current_lru_bucket & 0x8080808080808080ULL; // HOW DOES THIS WORK?? LEARN LATER
        int current_lru_L2 = std::countr_zero(has_zero) >> 3;

        return current_lru_L2;
    }

    inline int install_L2(uint64_t tag, bool dirty) {
        uint32_t bucket = tag & 511;
        int l2_lru_pos = get_current_lru_L2(bucket);
        uint64_t lru_index = (bucket << 3) + l2_lru_pos;
        int to_ret = L2_dirty[lru_index];
        L2[lru_index] = tag;
        L2_dirty[lru_index] = dirty;
        update_lru2_cache(bucket, l2_lru_pos);
        return to_ret;
    }

    inline int handle_L1_eviction(uint32_t bucket) {
        int l1_lru_pos = get_current_lru_L1(bucket);
        uint32_t lru1_index = (bucket << 3) + l1_lru_pos;
        int ret = 0;

        if (L1_dirty[lru1_index]) {
            uint64_t lru_tag = L1[lru1_index];
            uint32_t bucket_evicted = lru_tag & 511;

            int f2 = search_L2(lru_tag, bucket_evicted);
            if (f2 >= 0) {
                L2_dirty[(bucket_evicted << 3) + f2] = 1;
            } else {
                ret += install_L2(lru_tag, 1);
            }
        }

        return ret;
    }

    csot::CacheStats run(const csot::MemAccess* acc, std::size_t n) override {
        csot::CacheStats s{};

        int writes = 0;
        int l1_hits = 0;
        int l1_misses = 0;
        int l2_hits = 0;
        int l2_misses = 0;
        int dirty_writebacks = 0;

        for (std::size_t i = 0; i < n; ++i) {
            const csot::MemAccess& a = acc[i];
            writes += a.is_write;

            const uint64_t b = a.address >> 6;
            const uint32_t bucket = b & 63;
            int found = search_L1(b, bucket);

            if (found >= 0) [[likely]] {
                l1_hits++;
                update_lru1_cache(bucket, found);
                L1_dirty[(bucket << 3) + found] |= a.is_write;
                continue;
            }

            // L1 miss.
            l1_misses++;
            const int l1_lru_pos = get_current_lru_L1(bucket);
            const uint32_t lru1_index = (bucket << 3) + l1_lru_pos;

            const uint32_t bucket2 = b & 511;
            int found2 = search_L2(b, bucket2);

            if (found2 >= 0) {
                l2_hits++;
                update_lru2_cache(bucket2, found2);
                const uint32_t index2 = (bucket2 << 3) + found2;

                dirty_writebacks += handle_L1_eviction(bucket);
                L1[lru1_index] = L2[index2];
                L1_dirty[lru1_index] = a.is_write;
                update_lru1_cache(bucket, l1_lru_pos);
                continue;
            }

            l2_misses++;
            dirty_writebacks += install_L2(b, 0);
            dirty_writebacks += handle_L1_eviction(bucket);
            L1[lru1_index] = b;
            L1_dirty[lru1_index] = a.is_write;
            update_lru1_cache(bucket, l1_lru_pos);
        }
        s.writes = writes;
        s.reads = n - writes;
        s.l1_hits = l1_hits;
        s.l1_misses = l1_misses;
        s.l2_hits = l2_hits;
        s.l2_misses = l2_misses;
        s.dirty_writebacks = dirty_writebacks;
        return s;
    }

    private:
        alignas (64) uint64_t L1[512]{0};
        alignas (64) uint8_t L1_dirty[512]{0};
        alignas (64) uint64_t L1_lru[64];
        alignas (64) uint64_t L2[4096]{0};
        alignas (64) uint8_t L2_dirty[4096]{0};
        alignas (64) uint64_t L2_lru[512];
};

}  // namespace
#pragma GCC pop_options
extern "C" csot::CacheSim* create_cache_sim() {
    return new StubCacheSim();
}