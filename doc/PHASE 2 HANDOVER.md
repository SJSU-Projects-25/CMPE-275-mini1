# Phase 2 Handover Document

## Overview

Phase 2 parallelization is **COMPLETE** for the query engine layer. This document covers what was built on top of Phase 1, current architecture, and what remains for the benchmarking harness and Phase 3.

## What Was Built (Phase 2 Additions)

### New Components

1. **QueryEngine** (`include/taxi/QueryEngine.hpp`, `src/QueryEngine.cpp`)
   - 6 query methods, all parallelized with OpenMP
   - Holds a const reference to `DatasetManager`'s record vector (zero-copy)
   - Lazy-initialized via `DatasetManager::query_engine()`
   - Builds indexes on first access

2. **TimeIndex** (`include/taxi/TimeIndex.hpp`, `src/TimeIndex.cpp`)
   - Sorted permutation index on `pickup_timestamp`
   - `build()`: creates `std::vector<size_t>` sorted by timestamp using `std::iota` + `std::sort`
   - `lookup()`: O(log N) binary search via `std::lower_bound` / `std::upper_bound`
   - Returns `[begin, end)` index range into sorted indices
   - Self-reports build time in milliseconds

3. **QueryTypes** (`include/taxi/QueryTypes.hpp`)
   - `TimeRangeQuery` — epoch seconds, inclusive
   - `NumericRangeQuery` — double min/max (fare, distance)
   - `IntRangeQuery` — int min/max (location ID, passenger count)
   - `CombinedQuery` — time + distance + passenger predicates
   - `QueryResult` — vector of `const TripRecord*` + `scanned` count
   - `AggregationResult` — sum, avg, count

4. **QueryEngine Correctness Test** (`src/test_query_engine.cpp`)
   - Validates all 6 queries produce non-empty results
   - Uses data-driven ranges (scans for min/max timestamps at runtime)
   - Pass/fail reporting per query

### Modified Components

1. **DatasetManager** (`include/taxi/DatasetManager.hpp`, `src/DatasetManager.cpp`)
   - Added `query_engine()` accessor with lazy initialization
   - Builds indexes automatically on first call
   - Owns `std::unique_ptr<QueryEngine>`, reset on `clear()`

2. **CMakeLists.txt**
   - Added `find_package(OpenMP)` with fallback warning
   - Links `OpenMP::OpenMP_CXX` to `taxi_core`
   - Added `test_query_engine` target

### Unchanged Components

- **TripRecord** — same 17-field POD struct
- **CsvReader** — same streaming RFC 4180 parser
- **DatasetManager serial searches** — `search_by_fare()`, `search_by_distance()`, `search_by_passenger_count()` remain serial (preserved as Phase 1 baseline)
- **benchmark_main.cpp** — still benchmarks Phase 1 serial searches only

## Parallelization Strategy

### OpenMP Pattern Used

All 6 `QueryEngine` methods follow the same pattern:

```
#pragma omp parallel
{
    std::vector<const TripRecord*> local;     // thread-local accumulator
    local.reserve(data_.size() / (K * omp_get_num_threads()));

    #pragma omp for nowait schedule(static)
    for (size_t i = 0; i < N; ++i) {
        if (predicate(data_[i])) local.push_back(&data_[i]);
    }

    #pragma omp critical
    result.records.insert(result.records.end(), local.begin(), local.end());
}
```

**Key decisions:**
- `schedule(static)` for contiguous memory access (cache-friendly)
- `nowait` since each thread merges independently into result
- `#pragma omp critical` for the merge step (safe but serialized)
- `#if defined(_OPENMP)` guards everywhere for graceful serial fallback

### Per-Query Details

| Query | Method | Parallelization | Notes |
|-------|--------|----------------|-------|
| Q1: Time range | `search_by_time()` | OpenMP parallel scan with index fast-path | If TimeIndex is built: O(log N) lookup then parallel gather. If not: parallel linear scan. Small results (<10K) use serial path. |
| Q2: Distance | `search_by_distance()` | OpenMP parallel linear scan | Filters on `trip_distance` |
| Q3: Fare | `search_by_fare()` | OpenMP parallel linear scan | Filters on `total_amount` |
| Q4: Location | `search_by_location()` | OpenMP parallel linear scan | Filters on `pu_location_id` |
| Q5: Combined | `search_combined()` | TimeIndex narrowing + OpenMP parallel filter | Uses index to narrow time window first, then parallel-filters distance and passenger predicates |
| Q6: Aggregation | `aggregate_fare_by_time()` | `#pragma omp parallel for reduction(+:)` | Proper reduction pattern for sum accumulation — no critical section needed |

### Serial vs Parallel Comparison Points

The codebase preserves both code paths:

- **Serial baseline**: `DatasetManager::search_by_fare()`, `search_by_distance()`, `search_by_passenger_count()` in `src/DatasetManager.cpp`
- **Parallel (OpenMP)**: `QueryEngine::search_by_fare()`, `search_by_distance()`, etc. in `src/QueryEngine.cpp`
- **Serial fallback**: If OpenMP is not available at compile time, `QueryEngine` methods degrade to serial automatically via `#if defined(_OPENMP)` guards

## Code Structure (Updated)

```
include/taxi/
├── TripRecord.hpp       # Data structure (17 primitive fields)
├── CsvReader.hpp        # CSV parsing interface
├── DatasetManager.hpp   # Main API + lazy QueryEngine accessor
├── QueryEngine.hpp      # 6-query engine interface
├── QueryTypes.hpp       # Query parameter + result structs
└── TimeIndex.hpp        # Sorted timestamp index

src/
├── TripRecord.cpp            # Validation logic
├── CsvReader.cpp             # CSV parsing implementation
├── DatasetManager.cpp        # Serial searches + QueryEngine init
├── QueryEngine.cpp           # 6 queries with OpenMP (Phase 2)
├── TimeIndex.cpp             # Index build + binary search
├── benchmark_main.cpp        # Phase 1 benchmark (serial only)
├── test_query_engine.cpp     # QueryEngine correctness test
└── test_csv_reader.cpp       # CSV reader validation test

scripts/
├── download_tlc_data.sh      # Download full datasets
├── create_test_data.sh       # Create test samples
└── validate_build.sh         # Build validation
```

## How to Build and Run

### Build

```bash
cd build && cmake .. && cmake --build .
```

OpenMP is detected automatically. If not found, the build succeeds with a warning and all code runs serial.

### Run Correctness Test

```bash
./test_query_engine ../data/test_sample.csv
```

Expected output: 6 PASS lines, 0 failures.

### Run Existing Benchmark (Phase 1 serial only)

```bash
./bin/taxi_bench ../data/test_sample.csv 10
```

This currently only benchmarks `DatasetManager`'s serial searches — it does **not** benchmark `QueryEngine`.

## What's Missing / Next Steps

### For Person C: Benchmark Harness

The current `benchmark_main.cpp` is a Phase 1 artifact. It needs significant work:

1. **Benchmark `QueryEngine` queries** — the existing harness only exercises `DatasetManager::search_by_fare()` and `search_by_distance()` (serial). None of the 6 `QueryEngine` methods are benchmarked.

2. **`BenchmarkRunner` class** — encapsulate the timing logic. Currently it's a free function template `time_function_ms()`. A proper runner should:
   - Store per-run timings (not just accumulated total)
   - Compute min, max, mean, standard deviation
   - Support warmup runs (excluded from statistics)
   - Accept configurable iteration counts

3. **`MetricsRecorder` (CSV output)** — the assignment requires "tabular and graph formats." Currently all output is human-readable stdout. Need:
   - CSV export of per-run timings
   - Columns: query name, run number, elapsed ms, records scanned, matches found
   - Easy to import into Python/Excel for graphing

4. **Dataset runner CLI** — allow choosing which query to benchmark from the command line. Current harness runs hardcoded queries. Need:
   - Flag or argument to select query (e.g., `--query time`, `--query fare`, `--query all`)
   - Configurable query parameters (ranges)
   - Thread count control (`OMP_NUM_THREADS` or programmatic)

5. **Serial vs Parallel comparison** — the core value of Phase 2. Run the same logical query through:
   - `DatasetManager::search_by_fare()` (serial baseline)
   - `QueryEngine::search_by_fare()` (OpenMP parallel)
   - Report speedup ratio side-by-side

6. **Statistical rigor** — the assignment requires 10+ runs with averages. Add:
   - Standard deviation
   - Confidence intervals (optional)
   - Outlier detection (optional)

### For Phase 3: SoA (Structure-of-Arrays)

Phase 3 requires rewriting the data layout from Array-of-Objects (`std::vector<TripRecord>`) to Object-of-Arrays (columnar). Here's how the work splits:

- **Person A** adapts `CsvReader` / `DatasetManager` to write into SoA column vectors (e.g., `std::vector<double> fare_amounts`, `std::vector<int64_t> pickup_timestamps`, ...) instead of `TripRecord` objects.
- **Person B** rewrites `QueryEngine` to operate on SoA arrays and optionally adds column-level indexes.
- **Person C** updates the harness to compare Phase 1 (serial AoO) vs Phase 2 (parallel AoO) vs Phase 3 (parallel SoA), isolating each optimization's impact.

### Interface for Phase 3 QueryEngine

The current `QueryEngine` constructor takes `const std::vector<TripRecord>&`. For Phase 3, consider a new constructor or a separate `SoAQueryEngine` that takes columnar arrays:

```cpp
SoAQueryEngine(
    const std::vector<int64_t>& pickup_timestamps,
    const std::vector<double>& trip_distances,
    const std::vector<double>& fare_amounts,
    const std::vector<int>& pu_location_ids,
    const std::vector<int>& passenger_counts,
    // ...
);
```

This allows benchmarking both engines with the same queries.

## Quick Reference: Using QueryEngine

```cpp
#include "taxi/DatasetManager.hpp"
#include "taxi/QueryEngine.hpp"
#include "taxi/QueryTypes.hpp"

DatasetManager mgr;
mgr.load_from_csv("data/test_sample.csv");

// Option 1: via DatasetManager (lazy init, builds indexes automatically)
auto& engine = mgr.query_engine();

// Option 2: standalone (manual control)
QueryEngine engine(mgr.records());
engine.build_indexes();

// Time range query
TimeRangeQuery tq{1541300000, 1541400000};
QueryResult result = engine.search_by_time(tq);
// result.records = vector of matching TripRecord pointers
// result.scanned = number of records examined

// Fare range query
NumericRangeQuery fq{10.0, 50.0};
QueryResult fare_result = engine.search_by_fare(fq);

// Combined query (time + distance + passengers)
CombinedQuery cq{{1541300000, 1541400000}, {1.0, 5.0}, {1, 4}};
QueryResult combined_result = engine.search_combined(cq);

// Aggregation
TimeRangeQuery aq{1541300000, 1541400000};
AggregationResult agg = engine.aggregate_fare_by_time(aq);
// agg.sum, agg.avg, agg.count
```

## Baseline Metrics (Phase 1)

From Phase 1 handover — these are the serial baselines to compare against:

| Operation | Test Data (10K) | Full Data (6.4M) |
|-----------|----------------|-------------------|
| Load CSV  | ~21ms          | ~14s             |
| Search fare (serial) | ~0.02ms | ~58ms     |
| Search distance (serial) | ~0.02ms | ~25ms |

**Phase 2 goal:** Achieve measurable speedup on the 6.4M dataset with OpenMP parallelization. The test dataset (10K) is too small — overhead will likely dominate. Focus benchmarking on the full dataset.

## Known Considerations

1. **Thread count**: OpenMP defaults to system core count. Control with `OMP_NUM_THREADS=N` environment variable. Testing with 1, 2, 4, 8 threads would make good comparison data.

2. **Small dataset overhead**: OpenMP has thread creation/synchronization overhead. For the 10K test dataset, parallel may be slower than serial. This is expected and worth documenting.

3. **Result ordering**: Parallel queries do not guarantee the same result order as serial. The `#pragma omp critical` merge interleaves thread-local results in nondeterministic order. If comparison tests need deterministic order, sort results by pointer address or record index.

4. **Index build cost**: `TimeIndex::build()` sorts 6.4M indices. This is a one-time cost amortized over many queries. Worth benchmarking separately.

5. **macOS OpenMP**: Requires `brew install libomp`. The system Clang does not ship with OpenMP support by default.
