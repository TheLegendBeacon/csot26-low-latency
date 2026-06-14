# Week 2 Project — Fastest Correct Cache Simulator

> **Mission:** Implement a **single `cache_sim.cpp`** that simulates a fixed two-level cache hierarchy over a huge memory trace, emits the exact reference counters, and does it **as fast as wall-clock allows**. Week 1 taught you to measure a hot loop; Week 2 is where you make one disappear into the cache. Same "fastest correct implementation" game, brand-new kernel — one with far more room to optimize: struct-of-arrays state, branchless set lookup, compile-time geometry, prefetch, SIMD tag compares.

---
# Hardware Notes

- Processor: Intel i7-13700HX (16 Cores: 8 P-cores, 8 E-cores)
- OS: CachyOS \w Linux Kernel Ver. 7.0.11
- RAM: 16GB
- It seems the processors having a big.LITTLE arch do not support AVX-512 instructions. Therefore, for local testing I had to use AVX2.
# Optimization Ideas
This implementation was built from the ground-up with AVX-512 in mind, because the judge VM `c7` had guaranteed AVX-512 capabilities to take advantage of. However, AVX-2 was added as a fallback because my PC does not support AVX-512\
The Control Flow mostly follows the exact order of steps laid out by the `CACHE_SPEC.md` file. But each check is optimized as follows:
## 1. The L1 (and L2) Hit Check
The entire bucket consists of 8 cache-line tags, each an 8-byte number. Therefore, the entire bucket together consists of 64 bytes or 512 bits, which can be loaded directly onto a 512-element buffer. By creating another 512-element buffer containing duplicated `target` variables (the element to check against), all eight numbers are checked at the same time.
This produces a `mask` object (1 if same, 0 if not.) Just finding the position of the 1 (if there was one) was sufficient to find the position of the matched element. \
The L2 check was performed similarly.
## 2. Updating the LRU Cache
The LRU cache for each bucket was stored in hexadecimal marking each byte for storing the rank of one element (higher is higher priority.) \
Updating: This was also done using AVX-2/SSE instructions using masks. First, the entire LRU cache for one bucket was loaded into a 128-bit buffer.
## 3. Getting the current LRU object
This is a piece of math magic; I have no clue how it works.