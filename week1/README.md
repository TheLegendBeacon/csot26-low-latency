# Week 1 Project — Fastest Correct Strategy

> **Mission:** Build a minimal C++ trading-platform runner that executes a **judge-defined strategy spec**, replays historical market ticks against it, verifies correctness, measures end-to-end latency per decision, and prints a summary. Over the next 4 weeks we will turn this into a multi-threaded, lock-free, networked, leaderboard-ranked beast. This week, just get the correct synchronous baseline walking.

---
# Hardware Notes

- Processor: Intel i7-13700HX (16 Cores: 8 P-cores, 8 E-cores)
- OS: CachyOS \w Linux Kernel Ver. 7.0.11
- RAM: 16GB

# Build Steps
Run `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` to compile. \
Run `taskset -c <core> ./build/quant_runner build/spec_strategy.so <path-to-csv>` to run, pinned to one (or more) core(s).

# Engine Implementation
A simple engine for testing strategies, just measures and shows p50, p90, p99, and p999 by storing deltas. Uses `__rdtsc()` for cycles measurement.

# Sample Output
```
Projects/low-latency/project via △ v4.3.3 
❯ taskset -c 3 ./build/quant_runner build/spec_strategy.so data/synthetic_large.csv
10000000 tick(s) loaded.
Running with 2.30394GHz.
count = 10000000
p50  = 19.1 ns
p90  = 19.1 ns
p99  = 19.97 ns
p999 = 21.7 ns
```

# Perf Stat Output
```
❯ taskset -c 3 perf stat ./build/quant_runner build/spec_strategy.so data/synthetic_large.csv
10000000 tick(s) loaded.
Running with 2.30398GHz.
count = 10000000
p50  = 19.1 ns
p90  = 19.97 ns
p99  = 20.83 ns
p999 = 25.17 ns

 Performance counter stats for './build/quant_runner build/spec_strategy.so data/synthetic_large.csv':

                 0      context-switches:u               #      0.0 cs/sec  cs_per_second     
                 0      cpu-migrations:u                 #      0.0 migrations/sec  migrations_per_second
            95,506      page-faults:u                    #  63146.9 faults/sec  page_faults_per_second
          1,512.44 msec task-clock:u                     #      1.0 CPUs  CPUs_utilized       
       1,37,80,113      cpu_core/branch-misses/u         #      0.3 %  branch_miss_rate       
    4,34,89,81,955      cpu_core/branches/u              #   2875.5 M/sec  branch_frequency   
    6,15,86,58,896      cpu_core/cpu-cycles/u            #      4.1 GHz  cycles_frequency     
   18,40,26,24,823      cpu_core/instructions/u          #      3.0 instructions  insn_per_cycle
     <not counted>      cpu_atom/branch-misses/u         #      nan %  branch_miss_rate         (0.00%)
     <not counted>      cpu_atom/branches/u              #      nan M/sec  branch_frequency     (0.00%)
     <not counted>      cpu_atom/cpu-cycles/u            #      nan GHz  cycles_frequency       (0.00%)
     <not counted>      cpu_atom/instructions/u          #      nan instructions  insn_per_cycle  (0.00%)
             TopdownL1 (cpu_core)                        #      4.6 %  tma_bad_speculation    
                                                         #     22.7 %  tma_frontend_bound     
                                                         #     27.6 %  tma_backend_bound      
                                                         #     45.1 %  tma_retiring           
             TopdownL1 (cpu_atom)                        #      nan %  tma_backend_bound        (0.00%)
                                                         #      nan %  tma_frontend_bound       (0.00%)
                                                         #      nan %  tma_bad_speculation    
                                                         #      nan %  tma_retiring             (0.00%)

       1.512901642 seconds time elapsed

       1.281962000 seconds user
       0.227215000 seconds sys
```

# Latency Results
According to the online judge,
- p50 = 23 ns
- p99 = 72 ns
- p999 = 96 ns
- Throughput = 18.28 M/s

# Optimization Ideas
## 1. DP (Welford Updates)
On each tick, the sample strategy sums the entirety of the rolling buffer again. This is repeated work - only one element in the array is modified every time. Instead, we can track the mean and the variance, and their incremental updates everytime an element is modified.\
**Post Deadline Optimization:** Using a rolling sum and rolling variance is faster, because the CPU can take advantage of parallel FP units to do multiple adds at once. (USING AVX2/AVX512 instructions.)

## 2. Better String Indexing
The sample strategy uses an unordered map to index through symbols. This seems optimal because an unordered map has an O(1) average lookup complexity. However, an unordered map introduces additional overhead (the string must be hashed, and then if a hash collision is present, a linear search must occur.) \
It turns out that a simple linear search over 64 elements is faster than an `std::unordered_map` lookup. (n needs to be in the high hundreds for an `std::unordered_map` to be faster.)

## 3. Comparing `std::string_view` pointers directly
In the initial unordered_map implementation, the map is indexed using strings directly. However, the tick provids a `std::string_view`, and the original implementation produced a string first to check. However, this requires memory allocation on the heap in the hot path. We can use a fact about the engine implementation to help here - **Ticker Strings are Interned.** Therefore, if two ticker strings are the same, then we can just compare their pointers, reducing what would be a multiple-byte-long check to just one of equality of pointer address numbers.

## 4. Eliminating all SQRTs and divisions
If we write all bounds in terms of variance, we can completely eliminate the need for the operations of square root and division. These are heavy operations that take more cycles than simple addition and multiplication operations.

## 5. Struct Reordering and Next-Element Caching
We reorder the struct `SymbolState`, keeping the cache-hot, newly touched elements at the top, so that they end up on one cache line together. While the cold mids array which needs a rare one-element touch stays at the bottom. \
**Post Deadline Optimization:** It is actually optimal to completely separate the mids array from the struct. This removes the burden of carrying the mids array from the cache-hot struct, allowing all 64 ticker symbols to exist completely on the L1 cache. \
**Caching the Next Element:** This takes advantage of the CPU's out-of-order execution paradigm. Without caching, there are multiple reads of `st.mids[st.head]`. If this particular element is not in the cache, it requires time to load into L1. This can stall the hot-path. \
However, if we cache the next element into `st.old` and update the cache only after using `st.old` everywhere, then the read does not block the continued execution of the hot path - the CPU can tell that `st.old` is not used again and thus keeps the read away for later, not blocking the execution of the hot-path.

## 6. Branch Prediction Hints
Adding `[[likely]]` and `[[unlikely]]` to branching code to reduce the rate of branch mispredictions.

## 7. Alleviating the return vector's allocation cost (ineffective)
**Hypothesis:** When returning an array like this (`{something}`), it dynamically allocates an array onto the heap and then copies it into the vector. The memory allocation may be avoided by pre-allocating a space for the return element to stay in (`order_buf_`). \
**Why it didn't work:** The compiler does this anyway, lol. (Read: RVO)