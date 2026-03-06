# CMPE-275 Mini 1 — Full Project Walkthrough

**Team:** Ashish (Phase 1), Seth (Phase 2), Shamathmika (Phase 3)
**Project:** Memory Overload — NYC Yellow Taxi TLC Data
**Language:** C++20, Python 3
**Build:** CMake ≥ 3.20, GCC ≥ 13 or Clang ≥ 16

---

## 1. What Needs to Be Done (Requirements Breakdown)

The requirements document (`doc/REQUIREMENT.md`) defines "Memory Overload" (v0.1a) — a research
project on memory utilization and concurrent processing. Here is the step-by-step breakdown:

### Phase 1 — Serial Baseline (Ashish)
1. Choose a dataset from NYC OpenData with **> 2 million records** and **> 12 GB** total size.
   - The team chose NYC Yellow Taxi TLC Trip Records (2020–2023), totaling ~39 million rows.
2. Design and implement a **C++ Object-Oriented** system to:
   - Consume and parse the CSV dataset.
   - Represent all fields as **primitive types** (int, double, int64_t, bool) — no strings in
     the record struct.
   - Provide a **library API** (abstraction, classes, facade pattern) for data reading and basic
     range searching.
   - Do **NOT use threads** — this is a serial baseline.
3. **Benchmark** the serial code across 10+ runs and document:
   - CSV load time.
   - Search query performance (fare range, distance range, etc.).
   - Parse success/failure rates.
   - This becomes the **baseline** for all subsequent phases.

### Phase 2 — Parallelization (Seth)
1. Apply **parallelization** (OpenMP, std::thread) to the Phase 1 code.
2. Implement parallel query execution over the loaded dataset.
3. Implement parallel CSV loading (split file into byte-range chunks, one thread per chunk).
4. Record results and compare them to the Phase 1 baseline.
5. Build a **benchmark harness** (BenchmarkRunner) that runs N iterations and computes
   avg/stddev/min/max.

### Phase 3 — Vectorization & SoA Optimization (Shamathmika)
1. Rewrite the data storage from **Array-of-Structs (AoS)** to **Object-of-Arrays (SoA)**.
   - Instead of `std::vector<TripRecord>`, use one `std::vector<T>` per field.
   - This allows the CPU to load only the relevant field's cache lines during a scan query.
2. Rewrite query engine to operate on SoA layout (`SoAQueryEngine`).
3. Apply OpenMP parallelism to SoA queries (serial mode used for pure layout comparison).
4. Implement two SoA loading strategies:
   - **Phase 3a** (`--soa`): Convert existing AoS → SoA via `TripDataSoA::from_aos()`.
     Limited to 2023.csv only — requires both AoS and SoA in memory simultaneously (2×N peak
     = ~23.8 GB for 3 CSVs, exceeds 16 GB RAM).
   - **Phase 3b** (`--soa-direct`): Load CSV directly into SoA columns via
     `TripDataSoA::from_csv()`. Single-pass, no intermediate AoS — peak memory = SoA only
     (~11.9 GB). Enables full 3-CSV run.
5. Benchmark and compare: Phase 1 (serial AoS) → Phase 2 (parallel AoS) → Phase 3b (SoA direct).
6. Generate comparison plots using Python (`python/plot_comparison.py`).

### Deliverables
- The code (submitted as .tar.gz).
- A report with results, supporting data, conclusions, citations.
- A one-page presentation slide (a single key finding, not a summary).

---

## 2. Application Entry Points

The project builds **two main executables** and **two test executables**:

| Executable | Source File | Purpose |
|---|---|---|
| `taxi_bench_full` | `src/benchmark_main.cpp` | Full CLI benchmark for all phases (all team) |
| `test_csv_reader` | `src/test_csv_reader.cpp` | CSV parsing smoke test (Ashish) |
| `test_query_engine` | `src/test_query_engine.cpp` | QueryEngine unit tests (Seth) |

All executables link against the shared static library **`taxi_core`**, which contains all business logic.

**Starting point for a full benchmark run (all phases):**
```bash
bash scripts/run_benchmark.sh ~/Downloads/taxi_data 10
```

**Running individual phases manually:**
```bash
BIN=./build/bin/taxi_bench_full
DATA=~/Downloads/taxi_data

# Phase 1 — AoS serial baseline (3 CSVs)
"$BIN" "$DATA/2020.csv" "$DATA/2021.csv" "$DATA/2022.csv" --serial --runs 10 --output results/bench_phase1_local.csv

# Phase 2 — AoS parallel 8 threads (3 CSVs)
"$BIN" "$DATA/2020.csv" "$DATA/2021.csv" "$DATA/2022.csv" --threads 8 --runs 10 --output results/bench_phase2_local.csv

# Phase 3a — SoA from AoS (2023.csv only — memory constraint)
"$BIN" "$DATA/2023.csv" --soa --serial --runs 10 --output results/bench_phase3a_local.csv

# Phase 3b — SoA direct from CSV (3 CSVs)
"$BIN" "$DATA/2020.csv" "$DATA/2021.csv" "$DATA/2022.csv" --soa-direct --serial --runs 10 --output results/bench_phase3b_local.csv
```

---

## 3. Build System: `CMakeLists.txt`

**Author:** Ashish (initial), extended by Seth and Shamathmika.
**Requirement connection:** The requirement mandates CMake, GCC ≥ 13 or Clang ≥ 16, no Apple Clang.

### Key Blocks

| Section | What It Does |
|---|---|
| `cmake_minimum_required(VERSION 3.20)` | Enforces CMake 3.20+ |
| Compiler guard (lines 14–37) | Rejects `AppleClang`; enforces GCC ≥ 13 or Clang ≥ 16 with `-Wall -Wextra -pedantic` |
| `find_package(OpenMP)` | Finds OpenMP; if missing, builds serial-only |
| `add_library(taxi_core ...)` | Compiles all .cpp source files into a reusable static library |
| `add_executable(taxi_bench ...)` | Phase 1 harness entry point |
| `add_executable(taxi_bench_full ...)` | Phase 3 full CLI entry point |
| `add_executable(test_csv_reader ...)` | CSV parsing verification |
| `add_executable(test_query_engine ...)` | Query correctness verification |

---

## 4. Data Layer (Phase 1 — Ashish)

### 4.1 `include/taxi/TripRecord.hpp` + `src/TripRecord.cpp`

**Author:** Ashish
**Requirement connection:** Phase 1 step 1 — "represent the data's fields as their primitive types."

`TripRecord` is a plain-old-data (POD) struct with 17 fields, one per TLC CSV column:

| Field | C++ Type | CSV Column | Notes |
|---|---|---|---|
| `vendor_id` | `int` | VendorID | 1 = CMT, 2 = VeriFone |
| `pickup_timestamp` | `int64_t` | tpep_pickup_datetime | Seconds since Unix epoch |
| `dropoff_timestamp` | `int64_t` | tpep_dropoff_datetime | Seconds since Unix epoch |
| `passenger_count` | `int` | passenger_count | |
| `trip_distance` | `double` | trip_distance | Miles |
| `rate_code_id` | `int` | RatecodeID | |
| `store_and_fwd_flag` | `bool` | store_and_fwd_flag | Y→true, N→false |
| `pu_location_id` | `int` | PULocationID | Taxi Zone ID |
| `do_location_id` | `int` | DOLocationID | Taxi Zone ID |
| `payment_type` | `int` | payment_type | 1=Credit, 2=Cash |
| `fare_amount` | `double` | fare_amount | USD |
| `extra` | `double` | extra | |
| `mta_tax` | `double` | mta_tax | |
| `tip_amount` | `double` | tip_amount | |
| `tolls_amount` | `double` | tolls_amount | |
| `improvement_surcharge` | `double` | improvement_surcharge | |
| `total_amount` | `double` | total_amount | USD |

**`is_valid()`** (`src/TripRecord.cpp`) — validates a record by checking:
- `pickup_timestamp > 0`
- `dropoff_timestamp >= pickup_timestamp`
- `fare_amount >= 0`
- `total_amount >= 0`

Invalid records are discarded during CSV parsing to prevent bad data from polluting benchmarks.

---

### 4.2 `include/taxi/CsvReader.hpp` + `src/CsvReader.cpp`

**Author:** Ashish (initial streaming reader); extended by Seth (parallel chunk loading).
**Requirement connection:** Phase 1 — "design and code a process to consume the provided data set."

`CsvReader` is the core data ingest class. It reads TLC CSV files and produces `TripRecord` objects.

#### Class Interface

**`CsvReader(const std::string& filepath)`**
Constructor. Opens the file stream and sets `header_read_ = false`.

**`bool read_next(TripRecord& record)`**
Reads lines one at a time (streaming, not loading entire file). Skips the CSV header on first
call. Returns `false` at EOF. This is the key for Phase 1 memory efficiency — no giant buffer
is ever allocated; records arrive one at a time.

**`bool is_open() const`**
Checks if the underlying `std::ifstream` is open and readable.

**`Stats get_stats() const`**
Returns `{ rows_read, rows_parsed_ok, rows_discarded }` — tracked internally during parsing.

**`static std::vector<TripRecord> load_chunk(path, byte_start, byte_end, out_stats)`** *(Seth)*
Static method used by `ParallelLoader`. Seeks to `byte_start`, discards the (possibly partial)
first line to avoid split-line bugs across chunk boundaries, then reads and parses until
`byte_end`. Each thread calls this independently with non-overlapping byte ranges — no locking
needed.

#### Private Methods

**`parse_line(const std::string& line, TripRecord& record)`**
Tokenizes a CSV line and fills a `TripRecord`. Handles:
- 17, 18, or 19 column variants (2020–2023 TLC data changed schema over years).
- MM/DD/YYYY and YYYY-MM-DD timestamp formats.
- Validates `record.is_valid()` before accepting.
- Returns `false` to discard bad rows without throwing.

**`split_csv_line(const std::string& line)`**
RFC 4180-compliant tokenizer. Handles quoted fields with embedded commas (e.g., address
fields). Returns a `std::vector<std::string>` of tokens.

**`parse_timestamp(const std::string& timestamp_str)`**
Converts timestamp strings to `int64_t` (seconds since Unix epoch). Supports both:
- `YYYY-MM-DD HH:MM:SS` — 2020 data format
- `MM/DD/YYYY HH:MM:SS` — 2021–2022 data format

---

### 4.3 `include/taxi/DatasetManager.hpp` + `src/DatasetManager.cpp`

**Author:** Ashish (initial), Seth (added `query_engine()` lazy init).
**Requirement connection:** Phase 1 — "provide features and data through a set of APIs for data
reading and basic range searching." This is the **facade** the requirement hints at.

`DatasetManager` is the main public library interface. It owns the in-memory
`std::vector<TripRecord>` and exposes search APIs.

#### Public Methods

**`void load_from_csv(const std::string& csv_path)`**
Instantiates a `CsvReader` and repeatedly calls `read_next()`, appending to `records_`.
Accumulates `load_stats_` for reporting. After loading, the `query_engine_` unique_ptr is reset
so it will be lazily rebuilt on next access.

**`const std::vector<TripRecord>& records() const`**
Direct read-only access to the underlying record vector. Used by `QueryEngine` (which takes a
const reference to avoid copying all data).

**`std::size_t size() const`**
Number of loaded records. Used in benchmark reporting to confirm correct load.

**`void clear()`**
Empties `records_` and resets the `query_engine_` unique_ptr.

**`std::vector<const TripRecord*> search_by_fare(double min_fare, double max_fare) const`** *(Phase 1 serial)*
Linear scan over all records. Returns pointers to matching records (avoids copying TripRecord
structs). O(N) — this is the serial baseline.

**`std::vector<const TripRecord*> search_by_distance(double min_dist, double max_dist) const`** *(Phase 1 serial)*
Linear scan by `trip_distance`. O(N).

**`std::vector<const TripRecord*> search_by_passenger_count(int min, int max) const`** *(Phase 1 serial)*
Linear scan by `passenger_count`. O(N).

**`QueryEngine& query_engine()`** *(Seth)*
Lazy initialization: creates and builds a `QueryEngine` on first call (builds `TimeIndex`),
then returns a reference to the cached instance. Subsequent calls skip the rebuild. Uses a
`std::unique_ptr<QueryEngine>` to own the engine.

**`LoadStats get_load_stats() const`**
Returns aggregate parse statistics from all `load_from_csv()` calls.

---

### 4.4 `include/taxi/TimeIndex.hpp` + `src/TimeIndex.cpp`

**Author:** Seth
**Requirement connection:** Phase 2 hint: "What types of searches do you expect?" — Time-range
queries need O(log N) lookup, not O(N) scan.

`TimeIndex` builds a sorted index of record positions (by `pickup_timestamp`) enabling
binary-search based time-range lookups.

#### Methods

**`void build(const std::vector<TripRecord>& records)`**
Fills `indices_` with `0..N-1`, then `std::stable_sort`s them by
`records[i].pickup_timestamp`. Sets `built_ = true`. Called once after all data is loaded;
cost is O(N log N).

**`std::pair<size_t, size_t> lookup(records, start_time, end_time) const`**
Binary searches `indices_` for the half-open range `[lo, hi)` where all records fall within
`[start_time, end_time]`. Returns index positions into `indices_` (not raw record positions).
Cost is O(log N) — dramatically faster than a linear scan for narrow time windows.

**`const std::vector<size_t>& sorted_indices() const`**
Returns the raw sorted index array (used by `QueryEngine` to iterate over time-filtered records).

**`bool is_built() const`**
Returns `built_` — used by `DatasetManager::query_engine()` to know if the index is ready.

**`std::size_t size() const`**
Returns `indices_.size()` — should equal number of loaded records after `build()`.

---

### 4.5 `include/taxi/QueryTypes.hpp`

**Author:** Seth
**Requirement connection:** Provides typed query parameter structs used by all query APIs —
clean abstraction layer between caller and engine.

This header-only file defines all query input and result types:

| Type | Fields | Purpose |
|---|---|---|
| `TimeRangeQuery` | `start_time`, `end_time` | Epoch-second range for Q1 |
| `NumericRangeQuery` | `min_val`, `max_val` | Double range for Q2 (distance), Q3 (fare) |
| `IntRangeQuery` | `min_val`, `max_val` | Int range for Q4 (location) |
| `CombinedQuery` | `time_range`, `distance_range`, `passenger_range` | Multi-predicate for Q5 |
| `QueryResult` | `records` (vector of const TripRecord*), `scanned` | AoS query result |
| `AggregationResult` | `sum`, `avg`, `count` | Q6 aggregation result |

---

## 5. Benchmarking Infrastructure (Phase 1–2 — Ashish/Seth)

### 5.1 `include/taxi/BenchmarkRunner.hpp`

**Author:** Seth
**Requirement connection:** Phase 1 step 2 — "collect multiple executions (10+) to create an average."

Header-only template class. The key method:

**`static RunStats time_n(F&& func, int n)`**
Times callable `func` over `n` iterations using `std::chrono::steady_clock` (monotonic, not
affected by system clock changes). Returns a `RunStats` struct:
- `avg_ms` — arithmetic mean
- `stddev_ms` — sample standard deviation (N-1 denominator for unbiased estimate)
- `min_ms`, `max_ms` — observed extremes
- `runs` — n

This is the statistical engine behind every benchmark number in the results CSVs. Any lambda,
function pointer, or functor can be passed.

---

### 5.2 `include/taxi/MetricsRecorder.hpp` + `src/MetricsRecorder.cpp`

**Author:** Shamathmika
**Requirement connection:** Phase 3 — benchmark harness for collecting and exporting all results
in a consistent, machine-readable format.

`MetricsRecorder` accumulates `MetricRow` entries and serializes them to CSV.

**`void record(const MetricRow& row)`**
Appends one row. A `MetricRow` contains: phase name, query label, dataset_size, thread count,
`RunStats` timing, match count, and an optional `extra_val` (e.g., aggregation sum for Q6).

**`void write_csv(const std::string& path) const`**
Writes all rows to a CSV file with header:
```
phase,query,dataset_size,threads,avg_ms,stddev_ms,min_ms,max_ms,runs,matches,extra_val
```
Creates parent directories if needed. Throws `std::runtime_error` if the file cannot be opened.
This produces the `results/bench_phase*.csv` files in the repo.

**`void print_summary() const`**
Prints a human-readable, column-aligned table to stdout for quick review during benchmark runs.

---

### 5.3 `src/benchmark_main.cpp` — Phase 1 Harness

**Author:** Ashish
**Requirement connection:** Phase 1 step 2 — baseline benchmark.

`main()`:
1. Expects a CSV path as `argv[1]`.
2. Times CSV loading with `DatasetManager::load_from_csv()` across 3 runs.
3. Times three serial searches (fare, distance, passenger count) across 10 runs each.
4. Prints success rates and records-per-second.

This produces the **Phase 1 baseline** numbers documented in `results/bench_phase1_real.csv`
(e.g., ~106 seconds to load 39M records, ~47s for a fare scan).

---

### 5.4 `src/test_csv_reader.cpp` — CSV Smoke Test

**Author:** Ashish
**Requirement connection:** Phase 1 validation — "verification using good practices."

`main()`:
1. Opens a CSV file (path from `argv[1]`).
2. Reads first 3 records and prints all 17 fields in human-readable form.
3. Reads remaining records and reports: total read, parsed OK, discarded, and parse success rate.

Confirms the parser handles all TLC CSV format variations (17–19 columns, both timestamp
formats) before running the full benchmark.

---

## 6. Phase 2 — Parallel Query Engine (Seth)

### 6.1 `include/taxi/QueryEngine.hpp` + `src/QueryEngine.cpp`

**Author:** Seth
**Requirement connection:** Phase 2 step 1 — "apply parallelization (OpenMP, threading) to your code."

`QueryEngine` is the Phase 2 parallel query engine. It holds a `const` reference to the AoS
data vector and owns a `TimeIndex`.

**Constructor: `QueryEngine(const std::vector<TripRecord>& data)`**
Stores a reference — does NOT copy data. Lightweight O(1) construction.

**`double build_indexes()`**
Calls `time_index_.build(data_)`. Times and returns the build duration in ms. Must be called
before running Q1, Q5.

#### The 6 Queries

**Q1 — `search_by_time(const TimeRangeQuery& q) const`**
Uses `TimeIndex::lookup()` to binary-search the sorted index for the time range — O(log N + K)
where K = number of matches. This is the only query that benefits from the index; all others
are full scans. Very fast for narrow windows (e.g., a single day out of years of data).

**Q2 — `search_by_distance(const NumericRangeQuery& q) const`**
Parallel scan with `#pragma omp parallel for`. Each thread scans a contiguous portion of
`data_`, appending matches to a thread-local buffer. Buffers merged after implicit join.
Effective complexity: O(N/T) where T = thread count.

**Q3 — `search_by_fare(const NumericRangeQuery& q) const`**
Identical structure to Q2 but filters on `fare_amount`. O(N/T).

**Q4 — `search_by_location(const IntRangeQuery& q) const`**
Parallel scan filtering on `pu_location_id`. O(N/T).

**Q5 — `search_combined(const CombinedQuery& q) const`**
Multi-predicate parallel scan. First narrows via `TimeIndex` (O(log N)), then applies distance
and passenger count filters in parallel over the time-filtered candidate indices. Most selective
query — typically returns the fewest matches.

**Q6 — `aggregate_fare_by_time(const TimeRangeQuery& q) const`**
Parallel reduction over `fare_amount` for records in the time range. Uses
`#pragma omp parallel for reduction(+:sum)`. Returns `AggregationResult { sum, avg, count }`.
OpenMP handles the reduction variable safely across threads.

---

### 6.2 `include/taxi/ParallelLoader.hpp` + `src/ParallelLoader.cpp`

**Author:** Seth
**Requirement connection:** Phase 2 — parallel I/O loading (std::thread-based).

`ParallelLoader` splits a CSV file into N byte-range chunks and loads them concurrently using
`std::thread` (not OpenMP, since thread management here is structural rather than loop-level).

**`static Result load(const std::string& path, int num_threads)`**
1. Gets file size via `std::filesystem::file_size()`.
2. Divides `[0, file_size)` into `num_threads` equal byte-range chunks.
3. Launches one `std::thread` per chunk, each calling `CsvReader::load_chunk()` on its slice.
4. Joins all threads (waits for all to finish).
5. Merges all per-chunk `vector<TripRecord>` results using `std::move` + `reserve` to avoid
   copies during merge.
6. Aggregates parse stats and measures wall-clock time.

Returns a `Result` with: records, total parse stats, `load_time_ms`, `threads_used`.

Note: Parallel loading is I/O bound on most hardware. For the 39M-record dataset, the parallel
load was actually *slower* than serial due to disk seek contention — a key finding documented
in the results.

---

### 6.3 `src/test_query_engine.cpp` — Query Correctness Test

**Author:** Seth
**Requirement connection:** "validation and verification using good practices."

`main()`:
1. Loads a CSV (defaults to `data/test_sample.csv`).
2. Computes actual min/max timestamps from loaded data to guarantee Q1 will always find matches.
3. Runs all 6 queries (Q1–Q6) and prints match counts + timing for each.
4. Validates that Q6's result count equals total dataset size (sanity check for the time-range
   spanning all data).

---

## 7. Phase 3 — SoA Layout & Optimization (Shamathmika)

### 7.1 `include/taxi/TripDataSoA.hpp`

**Author:** Shamathmika
**Requirement connection:** Phase 3 step 1 — "rewriting your data classes into a vectorized
organization (Object-of-Arrays)."

`TripDataSoA` is the Phase 3 data structure. Instead of `vector<TripRecord>` (Array-of-Structs,
where each record is ~128 bytes of interleaved fields), each field gets its own contiguous
typed vector:

```
AoS (Phase 1/2):
  Record 0: [vendor_id | pickup_ts | dropoff_ts | pax | dist | ... | total]  <- 128 bytes
  Record 1: [vendor_id | pickup_ts | dropoff_ts | pax | dist | ... | total]  <- 128 bytes
  ...
  Scanning trip_distance: CPU loads a 64-byte cache line, uses 8 bytes (dist), wastes 56.

SoA (Phase 3):
  vendor_id[]:         [v0, v1, v2, v3, v4, v5, v6, v7, ...]   <- 4 bytes each
  pickup_timestamp[]:  [t0, t1, t2, t3, t4, t5, t6, t7, ...]   <- 8 bytes each
  trip_distance[]:     [d0, d1, d2, d3, d4, d5, d6, d7, ...]   <- 8 bytes each (8 per cache line!)
  fare_amount[]:       [f0, f1, f2, f3, f4, f5, f6, f7, ...]   <- 8 bytes each
  ...
  Scanning trip_distance: CPU loads a 64-byte cache line, uses all 8 doubles = 100% utilisation.
```

**`std::size_t size() const`**
Returns `pickup_timestamp.size()` — the number of trips stored.

**`static TripDataSoA from_aos(const std::vector<TripRecord>& records)`**
Converts an AoS vector to SoA layout in a single O(N) pass. Pre-reserves all 17 vectors to
`records.size()` before the loop to avoid reallocations. This is a one-time conversion cost
paid at startup in Phase 3a benchmarks.
**Memory cost**: requires AoS + SoA simultaneously = 2×N peak. For 94.6M records this exceeds
16 GB RAM, so Phase 3a is limited to 2023.csv (~37.9M records, ~9.7 GB peak).

**`static TripDataSoA from_csv(const std::vector<std::string>& paths, std::size_t reserve_count = 0)`** *(Phase 3b)*
Single-pass direct load from one or more CSV files into SoA column vectors. No intermediate
`vector<TripRecord>` is ever built. Pre-reserves all 17 column vectors to `reserve_count`
(95,000,000 for the 3-CSV dataset) to avoid reallocations during streaming.
**Memory cost**: SoA only (~11.9 GB for 94.6M records) — enables full 3-CSV run within 16 GB RAM.
Implemented in `src/SoAQueryEngine.cpp` alongside `from_aos()`.

---

### 7.2 `include/taxi/SoAQueryEngine.hpp` + `src/SoAQueryEngine.cpp`

**Author:** Shamathmika
**Requirement connection:** Phase 3 — "optimize your code... Performance is the highest goal."

`SoAQueryEngine` implements the same 6 queries as `QueryEngine` but operates on `TripDataSoA`.
All full-scan queries are OpenMP-parallelized and access contiguous typed arrays for maximum
cache efficiency and SIMD auto-vectorization.

**Constructor: `SoAQueryEngine(const TripDataSoA& data)`**
Stores a const reference to the SoA data. No copy of the data.

**`double build_indexes()`**
Builds `time_sorted_idx_` — a vector of row indices sorted by `data_.pickup_timestamp[i]`.
Uses `std::sort` with a lambda comparator. Sets `indexed_ = true`. Must be called before Q1/Q5.

**`std::pair<size_t, size_t> time_lookup(start, end) const`** *(private)*
Binary searches `time_sorted_idx_` using `std::lower_bound` / `std::upper_bound` for the
epoch-second range. Returns `[lo, hi)` positions into the sorted index.

#### The 6 SoA Queries

**Q1 — `search_by_time(const TimeRangeQuery& q) const`**
Binary-searches `time_sorted_idx_` for the time window. Iterates only matching indices
(typically a tiny fraction of 39M records for a narrow date range). O(log N + K).

**Q2 — `search_by_distance(const NumericRangeQuery& q) const`**
`#pragma omp parallel for` over `data_.trip_distance[]` — a single contiguous `double` array
of N elements. The hardware prefetcher sees stride-1 access; the compiler can emit AVX2/AVX-512
SIMD instructions to compare 4–8 doubles per cycle. Cache line utilization: 8 doubles vs.
effectively 0.5 TripRecords per cache line in AoS — approximately 16x better.

**Q3 — `search_by_fare(const NumericRangeQuery& q) const`**
Same parallel scan structure as Q2 but over `data_.fare_amount[]`. Each cache line brings in
8 fare values to compare; in AoS each cache line brought in ~0.5 full records.

**Q4 — `search_by_location(const IntRangeQuery& q) const`**
Parallel scan of `data_.pu_location_id[]` (4-byte `int` array). A 64-byte cache line holds
16 ints vs. 0.5 TripRecords in AoS — 32x better cache utilization for this field.

**Q5 — `search_combined(const CombinedQuery& q) const`**
Time-filtered with `time_lookup`, then `#pragma omp parallel for` over the candidate index set.
For each candidate, checks `data_.trip_distance[i]` and `data_.passenger_count[i]` in separate
typed arrays. Two independent access patterns on two contiguous arrays.

**Q6 — `aggregate_fare_by_time(const TimeRangeQuery& q) const`**
Time-filtered, then `#pragma omp parallel for reduction(+:sum) reduction(+:cnt)` over
`data_.fare_amount[]` for matching indices. The reduction is fully auto-vectorizable by
GCC/Clang with AVX2 — 4 doubles summed per SIMD instruction per cycle. Returns
`AggregationResult { sum, avg=sum/cnt, count=cnt }`.

---

### 7.3 `src/benchmark_main.cpp` — Unified CLI Benchmark (all phases)

**Authors:** All team members (Ashish: Phase 1 base; Seth: Phase 2 parallel; Shamathmika: Phase 3 SoA + direct)
**Requirement connection:** Phase 3 — full benchmark harness comparing all phases in one unified tool.

This is the **primary benchmark driver** for the final report. `main()` parses CLI arguments:

```
<csv_files>       One or more CSV paths (positional — loaded in order)
--serial          Phase 1: serial AoS load + all 6 queries
--threads <N>     Phase 2: parallel AoS load with N threads + parallel queries
--soa             Phase 3a: serial AoS load, then AoS→SoA conversion, then SoA queries
--soa-direct      Phase 3b: direct CSV→SoA single-pass load, then SoA queries
--runs <N>        Iterations per query (default: 10)
--output <path>   CSV file to write MetricsRecorder results
```

**Phase 1 path** (`--serial`):
Loads all CSV files serially via `DatasetManager::load_from_csv()`. Times load with
`BenchmarkRunner::time_n()`. Builds `QueryEngine` (with `TimeIndex`). Runs all 6 queries
N times each. Tags results as "Phase1_serial".

**Phase 2 path** (`--threads N`):
Loads via `ParallelLoader::load()` using N threads. Sets `omp_set_num_threads(N)`. Builds
`QueryEngine`. Runs all 6 parallel queries N times. Tags as "Phase2_parallel".

**Phase 3a path** (`--soa --serial`):
Loads serially via `DatasetManager`. Converts to SoA with `TripDataSoA::from_aos()` (timed
separately). Builds `SoAQueryEngine`. Runs all 6 SoA queries N times. Tags as "Phase3_soa".

**Phase 3b path** (`--soa-direct --serial`):
Loads directly into SoA via `TripDataSoA::from_csv(csv_paths, 95000000)` — no intermediate AoS.
Builds `SoAQueryEngine`. Runs all 6 SoA queries N times. Tags as "Phase3_soa_direct".

After all runs, calls `recorder.write_csv(output_path)` and `recorder.print_summary()`.

---

## 8. Python Visualization (Phase 3 — Shamathmika)

### 8.1 `python/plot_comparison.py`

**Author:** Shamathmika
**Requirement connection:** Report requirement — "testing results (tabular and graph formats)."

Reads the `results/bench_phase*.csv` files and generates 3 PNG charts.

**`load_and_filter(path, phase_label)`**
Reads a benchmark CSV with `pandas.read_csv()` and tags all rows with the given phase label.

**`plot_query_times(df, output_dir)`**
Grouped bar chart with stddev error bars. X-axis: query labels (LOAD, Q1–Q6). Y-axis: avg_ms.
One bar group per phase. Colors: blue (Phase 1), orange (Phase 2), green (Phase 3).
Saves as `query_times.png`.

**`plot_speedup(df, output_dir)`**
Speedup ratio chart. For each query computes `Phase1_avg / PhaseX_avg`. Horizontal reference
line at 1.0. Shows how much faster Phase 2 and Phase 3 are vs. the serial baseline.
Saves as `speedup.png`.

**`plot_load_time(df, output_dir)`**
Bar chart comparing CSV load time across phases with thread count annotations.
Saves as `load_time.png`.

**`main()`**
Parses `--phase1`, `--phase2`, `--phase3` CSV paths and `--output` directory. Orchestrates
the 3 plots.

---

### 8.2 `python/plot_scaling.py`

**Author:** Shamathmika
**Requirement connection:** Report requirement — strong scaling analysis.

Reads benchmark CSVs collected at 1, 2, 4, 8 threads and generates scaling charts.

**`load_scaling_data(csv_paths, thread_counts)`**
Loads each CSV tagged with its thread count into one combined DataFrame.

**`plot_query_time_vs_threads(df, output_dir)`**
Log-log line chart: X-axis = thread count, Y-axis = avg_ms. One line per query (Q2, Q3, Q4,
Q6 — the full-scan queries). Shows how queries scale with thread count.
Saves as `scaling_query_time.png`.

**`plot_speedup_vs_threads(df, output_dir)`**
Speedup vs thread count with an "ideal linear" reference line (y = x). Shows parallel
efficiency — how close to linear scaling each query achieves.
Saves as `scaling_speedup.png`.

**`plot_soa_vs_aos(df_aos, df_soa, output_dir)`**
Grouped bar chart comparing AoS and SoA query times at 2 and 4 threads side by side.
Clearly quantifies the SoA cache advantage independent of thread count.
Saves as `scaling_soa_vs_aos.png`.

**`main()`**
Auto-discovers CSVs by naming convention (`bench_phase1_real.csv` for 1T,
`scaling_aos_t2.csv`, `scaling_aos_t4.csv`, etc.). Calls all three plot functions.

---

## 9. Benchmark Results Summary

All raw data is in `results/`. Full analysis is in `doc/RESULT_SUMMARY.md`.
Plots: `results/query_times.png`, `results/speedup.png`, `results/load_time.png`.

**Dataset**: 94,589,581 records from 2020+2021+2022 NYC Yellow Taxi CSVs (~12 GB on disk, ~11.9 GB in RAM).
Phase 3a uses 2023.csv only (37,917,834 records) due to 2×N peak memory constraint.

### Load performance (avg over 10 runs, 94.6M records)

| Phase | Threads | Load time (ms) | vs Phase 1 |
|---|---|---|---|
| Phase 1 (AoS serial) | 1 | 192,870 | baseline |
| Phase 2 (AoS parallel) | 8 | 250,684 | **−30% (slower)** — I/O bound |
| Phase 3b (SoA direct) | 1 | 171,829 | **+10.9% (faster)** |

### Query performance (avg over 10 runs, 94.6M records, serial load/query)

| Query | Phase 1 AoS (ms) | Phase 2 AoS 8T (ms) | Phase 3b SoA (ms) | P1→P2 | P1→P3b |
|---|---|---|---|---|---|
| Q1 (time, index) | 3,074 | 1,776 | 686 | 1.7× | **4.5×** |
| Q2 (distance scan) | 73,681 | 31,147 | 2,526 | 2.4× | **29×** |
| Q3 (fare scan) | 75,779 | 31,454 | 2,458 | 2.4× | **31×** |
| Q4 (location scan) | 75,996 | 32,043 | 1,570 | 2.4× | **48×** |
| Q5 (combined) | 73,118 | 42,614 | 2,121 | 1.7× | **34×** |
| Q6 (aggregation) | 74,032 | 32,571 | 2,135 | 2.3× | **35×** |

**Key finding:** SoA layout alone (Phase 3b, serial) delivers 29–48× speedup vs serial AoS
(Phase 1) on the same dataset — purely from cache line efficiency (8 doubles vs 0.5 TripRecords
per cache line). OpenMP parallelism (Phase 2) adds 2.4× on top of layout. Together: up to 48×
total improvement for scan-heavy queries.

---

## 10. File Map by Team Member

| File | Phase | Author |
|---|---|---|
| `include/taxi/TripRecord.hpp` | 1 | Ashish |
| `src/TripRecord.cpp` | 1 | Ashish |
| `include/taxi/CsvReader.hpp` | 1/2 | Ashish (interface), Seth (load_chunk) |
| `src/CsvReader.cpp` | 1/2 | Ashish (core parser), Seth (load_chunk impl) |
| `include/taxi/DatasetManager.hpp` | 1/2 | Ashish (interface), Seth (query_engine()) |
| `src/DatasetManager.cpp` | 1/2 | Ashish (load/search), Seth (lazy QE init) |
| `src/test_csv_reader.cpp` | 1 | Ashish |
| `include/taxi/QueryTypes.hpp` | 2 | Seth |
| `include/taxi/TimeIndex.hpp` | 2 | Seth |
| `src/TimeIndex.cpp` | 2 | Seth |
| `include/taxi/QueryEngine.hpp` | 2 | Seth |
| `src/QueryEngine.cpp` | 2 | Seth |
| `include/taxi/ParallelLoader.hpp` | 2 | Seth |
| `src/ParallelLoader.cpp` | 2 | Seth |
| `include/taxi/BenchmarkRunner.hpp` | 2 | Seth |
| `src/test_query_engine.cpp` | 2 | Seth |
| `include/taxi/TripDataSoA.hpp` | 3 | Shamathmika |
| `include/taxi/SoAQueryEngine.hpp` | 3 | Shamathmika |
| `src/SoAQueryEngine.cpp` | 3 | Shamathmika (`from_aos` + `from_csv` + all queries) |
| `include/taxi/MetricsRecorder.hpp` | 3 | Shamathmika |
| `src/MetricsRecorder.cpp` | 3 | Shamathmika |
| `src/benchmark_main.cpp` | 1/2/3 | All (unified CLI — Phase 1 base Ashish, Phase 2 Seth, Phase 3 Shamathmika) |
| `python/plot_comparison.py` | 3 | Shamathmika |
| `python/plot_scaling.py` | 3 | Shamathmika |
| `scripts/run_benchmark.sh` | 3 | Shamathmika |
| `CMakeLists.txt` | 1/2/3 | All (Ashish initial, extended each phase) |
