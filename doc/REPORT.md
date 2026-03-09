# Mini 1 - NYC Taxi Dataset

Ashish Bhusal

()

Seth

()

FNU Shamathmika

018326464

## **1. Introduction**

The dataset is the **NYC Yellow Taxi Trip Records** (2020-2022) from the NYC TLC via NYC OpenData: 94,589,581 valid records parsed from 95,208,669 raw rows (99.35% success rate) across 12.0 GB of CSV files. A fourth year (2023, 37.9M records) was used for the Phase 3a memory-constrained experiment. The dataset's multi-year schema drift (17-19 column variants, two timestamp formats) and volume (~100M records, ~18 GB in-memory) ensure that design decisions carry measurable performance consequences.

Six analytical queries stress different access patterns across all phases:

- **Q1** (time-range lookup, indexed), **Q5** (combined time+fare+pax filter, indexed) - O(log N) binary search
- **Q2** (distance scan), **Q3** (fare scan), **Q4** (location scan), **Q6** (fare aggregation) - O(N) linear scans

Each query was benchmarked across 10 timed runs, with results averaged and standard deviations computed.

## **2. Design & Architecture**

The system was structured as a three-layer library with a unified command-line benchmark driver on top. The decision to expose all functionality through a static library (taxi_core) rather than a monolithic executable was deliberate: it allows each phase to share the same data structures, parser, and benchmarking infrastructure without code duplication, and it enables the benchmark driver (taxi_bench_full) to switch between execution modes via CLI flags rather than requiring separate binaries per phase.

### Data Layer

The base data structure is TripRecord, a plain-old-data struct with exactly 17 fields, all primitive types: int, int64_t, double, and bool. Timestamps are stored as int64_t seconds since Unix epoch, converting from the source CSV strings during parsing. No std::string fields appear in TripRecord, which keeps the struct size fixed at approximately 128 bytes and eliminates heap allocation per record. This design decision was intentional, since strings would introduce pointer indirection and variable-length heap allocations, destroying the contiguity of the array used in Phase 1/2.

### Parser

CsvReader is a streaming single-pass parser that processes one record at a time, keeping heap footprint constant. It handles both timestamp formats (YYYY-MM-DD and MM/DD/YYYY) and accepts rows with 17, 18, or 19 columns as the TLC schema changed across years. Each record is validated before acceptance, discarding 619,088 records (0.65%) due to invalid timestamps or negative fare amounts. A load_chunk() method added in Phase 2 accepts a byte range and parses independently, enabling parallel loading with no locking.

### Query Infrastructure

DatasetManager is the primary public API surface, implementing the facade pattern. It owns the std::vector<TripRecord> and exposes lazy-initialized query engine access through query_engine(), which builds a QueryEngine (including TimeIndex) on first call and caches it. TimeIndex is a sorted index built with std::stable_sort over record positions by pickup_timestamp, enabling O(log N) binary search for time-range queries. QueryEngine holds a const reference to the data vector (no copy is made) and executes all six queries with OpenMP parallelism in Phase 2. BenchmarkRunner is a header-only template that times any callable over N iterations using std::chrono::steady_clock, returning avg, stddev, min, and max. MetricsRecorder accumulates MetricRow entries and serializes them to a CSV file with a fixed schema, producing the results/benchmarks/bench_phase\*.csv files consumed by the Python plotting scripts.

### SoA Layer

TripDataSoA stores the same 17 fields as TripRecord but in 17 parallel std::vector containers, one per field. uint8_t is used for store_and_fwd_flag rather than bool to avoid std::vector<bool>'s bit-packing, which would prevent SIMD vectorization. SoAQueryEngine holds a const reference to TripDataSoA and reimplements all six queries operating directly on typed column arrays.

### Build System

CMake 3.20+ is required. The build rejects AppleClang and enforces GCC ≥ 13 or Clang ≥ 16 with -Wall -Wextra -pedantic. OpenMP is discovered with find_package(OpenMP); if absent, the code compiles in serial-only mode. All binaries link against the taxi_core static library, keeping compilation incremental.

## **3. Phase 1: Serial Baseline**

The goal was a correct, well-structured serial implementation that could serve as an unambiguous performance baseline. The primary challenge was producing a parser robust enough to handle four years of TLC data with schema differences across years without resorting to dynamically typed or string-heavy representations.

The CsvReader initially handled only the 17-column 2020 schema. Support for the 2021-2022 18-column variant (an added congestion_surcharge field) and the 2021-2022 timestamp format (MM/DD/YYYY) was added after early parse failures produced very low success rates. After the parser was corrected to branch on column count and the first timestamp character, the final parse success rate stabilized at 99.35% across all three years. The remaining 0.65% of rows (619,088 records) were discarded due to invalid data: negative fare amounts, zero or negative timestamps, or dropoff times preceding pickup times, all of which are validated in TripRecord::is_valid().

The baseline benchmark loaded 94,589,581 records from three CSVs (2020.csv, 2021.csv, 2022.csv) serially using DatasetManager::load_from_csv(), which internally streams through CsvReader::read_next() one record at a time. The time index (TimeIndex::build()) was then constructed using std::stable_sort over all 94.6M record positions. Six queries were then executed 10 times each and averaged.

**Phase 1 performance results (10-run averages, 94.6M records):**

| **Metric**                  | **Value**               |
| --------------------------- | ----------------------- |
| CSV load time               | 192,870 ms (±17,388 ms) |
| Time index build            | 179,244 ms              |
| Q1 (time range, indexed)    | 3,074 ms                |
| Q2 (distance scan)          | 73,681 ms               |
| Q3 (fare scan)              | 75,779 ms               |
| Q4 (location scan)          | 75,996 ms               |
| Q5 (combined time+fare+pax) | 73,118 ms               |
| Q6 (fare aggregation)       | 74,032 ms               |

Q1's dramatically lower time (3,074 ms vs. ~74s for scan queries) confirms the time index works correctly via O(log N) binary search. Q2-Q6 are all linear scans bound by the same sequential traversal of ~11.9 GB of memory, hence their near-identical times (~73-76s). Load time stddev (±9%) reflects OS page cache variability across runs.

### Correctness Validation

Query correctness was verified via a standalone test harness (test_query_engine.cpp) that checks each query against pre-computed expected outputs. As a cross-implementation check, result counts from Phase 1, Phase 2 (8 threads), and Phase 3 (SoA) were compared on the full 94.6M-record dataset: all three returned identical match counts and aggregated values, confirming no correctness regressions from parallelism or the AoS->SoA rewrite.

## **4. Phase 2: Parallelization**

Phase 2 was implemented by Seth. Two distinct forms of parallelism were added: parallel CSV loading via std::thread and parallel query execution via OpenMP.

**Parallel loading** was implemented in ParallelLoader::load(). The approach divides the file into N equal byte-range chunks using std::filesystem::file_size(), spawns one std::thread per chunk, each calling CsvReader::load_chunk() on its byte range, then joins all threads and merges the resulting per-chunk std::vector<TripRecord> buffers using std::move and reserve to avoid unnecessary copies. The merge step is single-threaded: after joining, the vectors are appended sequentially.

The expectation was that 8 threads would reduce load time by approximately 8x. The actual result was the opposite: parallel loading with 8 threads took **250,684 ms on average - 30% slower than Phase 1's 192,870 ms serial load**. The minimum observed parallel load (172,640 ms) approached the serial minimum (154,436 ms) but never exceeded it. The explanation is straightforward: CSV loading is I/O-bound, not CPU-bound, so multiple threads simultaneously seeking to different byte offsets in the same file create contention that a single sequential read avoids entirely. This finding - that parallelizing I/O on single-device hardware is actively harmful - is one of the most practically important results of the project and is clearly visible in Figure 1.

![alt text](../results/plots/load_time.png)
Figure 1: Load Time Comparison (Serial vs. Parallel)

**Parallel query execution** was implemented with #pragma omp parallel for inside each of the six query methods in QueryEngine. Thread-local result buffers are populated independently and merged after the implicit OMP barrier. The #pragma omp parallel for reduction(+:sum) directive handles Q6's fare aggregation. The TimeIndex built during Phase 1 is reused without modification; Q1 and Q5 perform a serial binary search to locate the candidate index range, then iterate in parallel over the matching candidates.

**Phase 2 query results vs. Phase 1 (8 threads, 10 runs):**

| **Query**          | **Phase 1 (ms)** | **Phase 2 (ms)** | **Speedup** |
| ------------------ | ---------------- | ---------------- | ----------- |
| Q1 (indexed)       | 3,074            | 1,776            | 1.73x       |
| Q2 (distance scan) | 73,681           | 31,147           | 2.37x       |
| Q3 (fare scan)     | 75,779           | 31,454           | 2.41x       |
| Q4 (location scan) | 75,996           | 32,043           | 2.37x       |
| Q5 (combined)      | 73,118           | 42,614           | 1.72x       |
| Q6 (aggregation)   | 74,032           | 32,571           | 2.27x       |

Full-scan queries achieved ~2.4x speedup with 8 threads - far below the theoretical 8x - indicating memory bandwidth saturation as the binding constraint. Q5 (1.72x) and Q1 (1.73x) underperform because the serial binary search phase becomes a bottleneck. The strong-scaling analysis in Section 6 confirms these patterns in detail.

The full speedup comparison including Phase 3 SoA is visualized in Figure 3 (Section 6).

## **5. Phase 3: Optimization**

Phase 3 was implemented by Shamathmika. The central optimization was a structural rewrite of the data representation from Array-of-Structs (AoS) to Structure-of-Arrays (SoA), exposing a new TripDataSoA class and a corresponding SoAQueryEngine.

### Cache Line Analysis

In Phase 1/2, each TripRecord is approximately 128 bytes. A 64-byte CPU cache line holds 0.5 records. A query scanning trip_distance must load an entire cache line to access 8 bytes of useful data, wasting 56 bytes (87.5% waste). In TripDataSoA, trip_distance is stored as a contiguous std::vector<double>. A 64-byte cache line holds 8 doubles, all of which are useful. Cache line utilization rises from 6.25% to 100% for this field. For pu_location_id (a 4-byte int), SoA utilization is even better: 16 ints per cache line versus 0.5 TripRecords per line - a 32x improvement in bytes-useful-per-cache-line-fetch.

### Two Loading Strategies

Two SoA loading strategies were implemented. An initial in-memory AoS->SoA conversion (from_aos()) required both representations simultaneously (~23.8 GB peak), exceeding available RAM; it was run on 2023.csv only as a proof of concept and is discussed in the Failed Attempts section. The primary strategy, from_csv(), is a single-pass direct loader that writes each parsed field into its typed column vector without ever constructing a TripRecord, keeping peak memory at ~11.9 GB and load time at 171,829 ms - 10.9% faster than Phase 1's 192,870 ms.

**Phase 3a query results (SoA from AoS, 2023.csv only, 37.9M records, 10-run averages):**

| **Query** | **Avg (ms)** | **Matches** | **Notes**                                                            |
| --------- | ------------ | ----------- | -------------------------------------------------------------------- |
| Q1        | 0.001        | 60          | Jan 2021 time range - near-zero matches in 2023 data; O(log N) index |
| Q2        | 176.3        | 22,877,491  | Distance 1-5 miles                                                   |
| Q3        | 124.9        | 32,465,246  | Fare $10-$50                                                         |
| Q4        | 151.3        | 18,887,846  | Location ID 100-200                                                  |
| Q5        | 0.026        | 60          | Combined with Jan 2021 range - no matches in 2023 data               |
| Q6        | 49.4         | 37,917,834  | Full reduction; avg fare = **$19.90** (vs. $13.91 for 2020-2022)     |

Phase 3a times are not directly comparable to other phases (2.5x smaller dataset), but the sub-200 ms scan results confirm SoA's per-record efficiency. Q1/Q5 return near-zero times because the Jan 2021 query window has no 2023 data. AoS->SoA conversion cost: **3,065 ms** (O(N)).

_Phase 3 (--soa-direct): Direct CSV -> SoA._ TripDataSoA::from_csv() is a single-pass load that writes each parsed field directly into its typed column vector, never constructing a TripRecord. Column vectors are pre-reserved to 95,000,000 elements before parsing begins. Peak memory is SoA-only: approximately 11.9 GB for 94.6M records, enabling the full three-CSV dataset to be processed. Load time was 171,829 ms - 10.9% faster than Phase 1's serial AoS load of 192,870 ms, because skipping the intermediate TripRecord allocation reduces peak allocation pressure and memory-write volume.

### uint8_t for Boolean Field

store_and_fwd_flag is stored as uint8_t rather than bool. std::vector<bool> uses bit-packing, preventing SIMD vectorization of loops that touch this field. uint8_t preserves one-byte-per-element layout, allowing GCC's auto-vectorizer to emit AVX2 instructions when processing this column.

### SoA Index Build

`SoAQueryEngine::build_indexes()` constructs a `time_sorted_idx_` vector using std::sort with a lambda comparator over `data_.pickup_timestamp[]`. Because timestamps are a contiguous typed array, the sort's comparison operations access memory sequentially and benefit from prefetching. The result: SoA index build completed in **24,618 ms - 7.3x faster than Phase 1's 179,244 ms** for the same O(N log N) algorithm.

### Summary of Phase 3 Optimizations

Each optimization contributes independently: (1) direct CSV->SoA loading saves 10.9% load time, (2) SoA index build is 7.3x faster than AoS for the same O(N log N) sort, and (3) column-contiguous layout delivers 29-48x query speedup from cache-line utilization alone. The full cross-phase comparison is presented in Section 6. Q6 exhibits a bimodal warm/cold cache distribution (±6,357 ms stddev) due to the working set exceeding physical RAM.

## **6. Results & Analysis**

**Cross-Phase Query Comparison:**

The complete picture across all three phases reveals that memory layout change (AoS -> SoA) contributed an order of magnitude more speedup than parallelism alone for scan-heavy queries. Phase 2's 8-thread parallelism achieves 2.4x on scan queries; Phase 3's serial SoA achieves 29-48x on the same queries. SoA serial outperforms 8-thread AoS parallel by 12-20x for every full-scan query (Figure 2).

![alt text](../results/plots/query_times.png)
Figure 2: Cross-Phase Query Times

| **Query** | **Phase 1 (ms)** | **Phase 2 8T (ms)** | **Phase 3 SoA (ms)** | **P1->P2** | **P1->P3** |
| --------- | ---------------- | ------------------- | -------------------- | --------- | --------- |
| Q1        | 3,074            | 1,776               | 686                  | 1.73x     | 4.48x     |
| Q2        | 73,681           | 31,147              | 2,526                | 2.37x     | 29.2x     |
| Q3        | 75,779           | 31,454              | 2,458                | 2.41x     | 30.8x     |
| Q4        | 75,996           | 32,043              | 1,570                | 2.37x     | 48.4x     |
| Q5        | 73,118           | 42,614              | 2,121                | 1.72x     | 34.5x     |
| Q6        | 74,032           | 32,571              | 2,135                | 2.27x     | 34.7x     |

![alt text](../results/plots/speedup.png)
Figure 3: Cross-Phase Speedup (Phase 2 parallelism vs. Phase 3 SoA, relative to Phase 1 baseline)

**Strong Scaling (t = 1, 2, 4, 8 threads, 10-run averages, ms):**

| **Query** | **AoS t=1** | **AoS t=2** | **AoS t=4** | **AoS t=8** | **AoS 1->8** | **SoA t=1** | **SoA t=2** | **SoA t=4** | **SoA t=8** | **SoA 1->8** |
| --------- | ----------- | ----------- | ----------- | ----------- | ----------- | ----------- | ----------- | ----------- | ----------- | ----------- |
| Q2        | 73,681      | 56,699      | 40,806      | 31,147      | 2.37x       | 2,526       | 1,551       | 1,400       | 1,090       | 2.32x       |
| Q3        | 75,779      | 57,967      | 40,763      | 31,454      | 2.41x       | 2,458       | 1,596       | 1,366       | 1,568       | 1.57x       |
| Q4        | 75,996      | 57,116      | 39,446      | 32,043      | 2.37x       | 1,570       | 1,102       | 984         | 826         | 1.90x       |
| Q5        | 73,118      | 57,535      | 56,340      | 42,614      | 1.72x       | 2,121       | 1,936       | 1,952       | 1,900       | 1.12x       |
| Q6        | 74,032      | 53,845      | 38,977      | 32,571      | 2.27x       | 2,135       | 931         | 687         | 661         | 3.23x       |

![alt text](../results/plots/scaling/scaling_query_time.png)
Figure 4: Strong Scaling Query Times (AoS and SoA)

**AoS (Figure 4):** Scan queries reach 2.3-2.4x at t=8, consistent with memory bandwidth saturation. Q5 reaches only 1.72x and barely improves from t=2->t=4 (1.02x) due to the serial index lookup bottleneck.

**SoA (Figure 4):** Lower absolute scaling (1.9-3.2x) because per-thread work is already short (1-2.5s at t=1), making OMP overhead non-negligible. Notable anomalies: Q3 regresses at t=8 (1,568 ms > t=4's 1,366 ms) from swap I/O contention with 8 concurrent threads; SoA Q1 degrades monotonically from 686 ms (t=1) to ~1,100 ms (t=8) as competing random access through the index exhausts TLB and cache entries.

**Layout vs. parallelism:** Comparing AoS t=8 against SoA t=1 directly (Figure 5), one thread with the right layout outperforms eight threads with the wrong layout by **12-20x** for every scan query - the project's most significant finding.

![alt text](../results/plots/scaling/scaling_soa_vs_aos.png)
Figure 5: SoA t=1 vs. AoS t=8 Comparison

### Hardware Caveat

All results were collected on a machine with 8 GB of physical RAM. The AoS working set (~18 GB) and even the SoA working set (~12 GB) substantially exceed physical RAM, resulting in 16+ GB of swap usage confirmed by Activity Monitor during every benchmark run. This creates an asymmetric effect: AoS queries access strided/random positions across 18 GB of virtual memory, generating frequent page faults with each cache line fetch; SoA queries scan contiguous column arrays with stride-1 access patterns that the OS prefetcher can partially satisfy even from swap. The 29-48x speedup magnitudes reported here are therefore likely inflated relative to a machine with 32 GB RAM where neither layout would page-fault heavily. On adequate hardware, the gap is estimated at 10-20x for full-scan queries based on the ratio of cache-line utilization improvement alone.

## **7. Failed Attempts**

1. **Parallel loading degradation** (see Section 4 for details): We hypothesized 8-thread parallel CSV loading would provide near-linear speedup. Instead it was 30% slower than serial loading (250,684 ms vs. 192,870 ms). CSV parsing is I/O-bound, not CPU-bound; multiple threads seeking to different byte offsets in the same file create contention that a single sequential read avoids. The lesson: always profile the actual bottleneck before applying parallelism.

2. **Phase 3a memory constraint** (see Section 5): Converting all 94.6M AoS records to SoA in-memory requires both representations simultaneously (~23.8 GB peak), which exceeded the 8 GB physical RAM and caused severe OOM pressure. We solved this by limiting Phase 3a to 2023.csv only and implementing a direct CSV-to-SoA loader that never constructs intermediate AoS, reducing peak memory to ~11.9 GB. This was a failure to account for peak-vs-average memory when planning.

3. **SoA Q1 threading regression** (see Section 6): We assumed OMP parallelism on SoA Q1 would be neutral or beneficial, as it was for other queries. Instead, Q1 time increased monotonically from 686 ms (t=1) to ~1,100 ms (t=8). The index-based query follows non-contiguous sorted positions, so multiple threads create competing cache and TLB pressure without prefetcher benefit. General principle: parallel random access is worse than serial random access when access patterns are unpredictable.

4. **Index build time underestimation**: Phase 1's AoS index build (179,244 ms) took nearly as long as the CSV load itself (192,870 ms). We did not expect an O(N log N) sort to be this slow, but at 94.6M elements the sort's random access pattern across 128-byte interleaved structs is memory-access-bound. SoA's contiguous timestamp array reduced this to 24,618 ms (7.3x faster), confirming that even sorts are cache-sensitive at scale.

## **8. Conclusions & Recommendations**

The data supports one central conclusion: **for scan-heavy analytical workloads over large datasets, memory layout is the dominant performance variable, exceeding CPU-thread parallelism by an order of magnitude.** A single thread with SoA layout outperforms 8 threads with AoS layout by 12-20x for every full-scan query, while Phase 2's 8-thread parallelism achieves only 2.4x. The mechanism (cache line utilization) is discussed in Section 5.

Parallelism provides smaller-scale benefits bounded by Amdahl's Law: Q5's serial index bottleneck limits AoS scaling to 1.02x between t=2 and t=4, and SoA Q1 actively degrades with more threads (discussed in Section 6). Parallelism helps when the bottleneck is compute, not memory bandwidth or serialized index access. The parallel load failure (-30% at 8 threads, Section 4) further demonstrates that thread parallelism applied to I/O-bound operations causes net harm.

### Recommendations for future work

1. **Rethink the SoA index**: Q1 and Q5 use an indirection array that reintroduces random access, limiting SoA cache efficiency (4.48x vs. 29-48x for scans) and causing negative thread scaling. A B-tree over epoch bins or a sorted-positions cache could preserve O(log N) lookup with sequential access.
2. **Benchmark on adequate hardware**: As noted in the Hardware Caveat (Section 6), results on a 32 GB machine would separate the layout advantage from swap-pressure amplification.
3. **Replace parallel loader with mmap**: Memory-mapped I/O allows the OS to prefetch sequentially without thread contention.

## **9. Individual Contributions**

**Ashish** designed and implemented the Phase 1 serial foundation: the core data structure with validation, the streaming CSV parser with RFC 4180 compliance, dual timestamp format handling, multi-schema support (17-19 columns across years), and the dataset management facade with the initial benchmark harness.

**Jianan** designed and implemented the Phase 2 parallel layer: the query engine with OpenMP parallelism for all six queries, the sorted time index with O(log N) binary search, the parallel file loader with byte-range chunking, typed query parameter definitions, the benchmark timing framework, and the correctness validation test harness.

**Shamathmika** designed and implemented the Phase 3 optimization layer: the Structure-of-Arrays data representation, both AoS-to-SoA and direct CSV-to-SoA loading strategies, the SoA query engine with OpenMP and SIMD-friendly column access, structured metrics recording, and the unified benchmark driver. Shamathmika also wrote the Python visualization and shell orchestration scripts, and ran the strong-scaling benchmarks (~6 additional hours of compute).

---

## **10. Citations & References**

NYC Taxi and Limousine Commission. _TLC Trip Record Data - Yellow Taxi Trip Records, 2020-2023._ NYC Open Data. https://data.cityofnewyork.us/browse?q=taxi&sortBy=relevance&pageSize=20. Accessed March 2026.

ISO/IEC 14882:2020. _Programming Languages - C++._ International Organization for Standardization, 2020.

[cppreference.com](http://cppreference.com/). _std::stable_sort, std::sort, std::lower_bound, std::upper_bound, std::chrono::steady_clock, std::vector._ [https://en.cppreference.com](https://en.cppreference.com/). Accessed March 2026.

OpenMP Architecture Review Board. _OpenMP Application Programming Interface, Version 5.2._ https://www.openmp.org/specifications. 2021.

Drepper, Ulrich. _What Every Programmer Should Know About Memory._ Red Hat, Inc., 2007. https://people.freebsd.org/~lstewart/articles/cpumemory.pdf.

Patterson, David A. and Hennessy, John L. _Computer Organization and Design: The Hardware/Software Interface, RISC-V Edition._ Morgan Kaufmann, 2020. (Chapter 5: Large and Fast: Exploiting Memory Hierarchy.)

Amdahl, Gene M. "Validity of the Single Processor Approach to Achieving Large Scale Computing Capabilities." _AFIPS Conference Proceedings_, 1967. doi:10.1145/1465482.1465560.

CMake. _CMake Documentation, Version 3.20+._ Kitware, Inc. https://cmake.org/documentation. Accessed March 2026.

GCC Team. _GCC 13 Release Notes - Auto-Vectorization and OpenMP Support._ Free Software Foundation, 2023. https://gcc.gnu.org/gcc-13/changes.html.

Hunter, J. D. "Matplotlib: A 2D Graphics Environment." _Computing in Science & Engineering_, vol. 9, no. 3, 2007, pp. 90-95. doi:10.1109/MCSE.2007.55.

McKinney, Wes. "Data Structures for Statistical Computing in Python." _Proceedings of the 9th Python in Science Conference (SciPy 2010)_, 2010. (pandas library.)

Harris, C. R. et al. "Array Programming with NumPy." _Nature_, vol. 585, 2020, pp. 357-362. doi:10.1038/s41586-020-2649-2.
