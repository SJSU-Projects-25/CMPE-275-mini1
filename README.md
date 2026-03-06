# CMPE-275-mini1 — Memory Overload

**Course**: CMPE 275 — Distributed Application Development
**Focus**: Memory utilization and concurrent processing
**Dataset**: NYC TLC Yellow Taxi Trip Data (2020–2023)

---

## Overview

This project benchmarks three phases of memory-layout strategies for scan-heavy workloads on ~92.9 million taxi trip records (~12 GB CSV):

| Phase    | Strategy                     | Dataset                 | Peak RAM |
| -------- | ---------------------------- | ----------------------- | -------- |
| Phase 1  | AoS serial (baseline)        | 2020+2021+2022 (~92.9M) | ~11.9 GB |
| Phase 2  | AoS parallel (8 threads)     | 2020+2021+2022 (~92.9M) | ~11.9 GB |
| Phase 3a | SoA converted from AoS       | 2023.csv only (~37.9M)  | ~9.7 GB  |
| Phase 3b | SoA loaded directly from CSV | 2020+2021+2022 (~92.9M) | ~11.9 GB |

**Phase 3a is limited to 2023.csv only** because converting AoS→SoA requires both in memory simultaneously (2×N peak = ~23.8 GB for 3 CSVs — exceeds 16 GB RAM). Phase 3b fixes this with a direct CSV→SoA single-pass loader.

---

## Project Structure

```
.
├── CMakeLists.txt
├── include/
│   └── taxi/
│       ├── TripRecord.hpp          # Core data struct (primitive fields only)
│       ├── CsvReader.hpp           # Streaming CSV parser (RFC 4180)
│       ├── DatasetManager.hpp      # AoS loader + QueryEngine façade
│       ├── TripDataSoA.hpp         # SoA layout (17 parallel typed arrays)
│       ├── ParallelLoader.hpp      # Multi-threaded CSV loader for Phase 2
│       ├── BenchmarkRunner.hpp     # Timing harness (N runs, mean/stddev)
│       ├── MetricsRecorder.hpp     # CSV results writer
│       └── SoAQueryEngine.hpp      # Query engine for SoA layout
├── src/
│   ├── CsvReader.cpp
│   ├── DatasetManager.cpp
│   ├── MetricsRecorder.cpp
│   ├── ParallelLoader.cpp
│   ├── SoAQueryEngine.cpp          # Also implements TripDataSoA::from_aos/from_csv
│   └── benchmark_main.cpp          # Main benchmark executable (all 3 phases)
├── scripts/
│   ├── run_benchmark.sh            # Runs all 3 phases (3a+3b), logs to results/
│   ├── download_tlc_data.sh        # Downloads TLC data from NYC OpenData
│   └── validate_build.sh           # Quick build sanity check
├── python/
│   └── plot_comparison.py          # Generates comparison graphs from CSVs
└── results/                        # Benchmark output CSVs and log
    ├── bench_phase1_local.csv
    ├── bench_phase2_local.csv
    ├── bench_phase3a_local.csv
    ├── bench_phase3b_local.csv
    └── bench_local_run.log
```

---

## Prerequisites

- **CMake** 3.20 or newer
- **C++ Compiler**: GCC 13+ or Clang 16+ (not Apple's Xcode clang)
- **OpenMP** (included with GCC; on macOS: `brew install libomp`)
- **RAM**: 16 GB minimum. Close other large applications before running.
- **Disk**: ~15 GB for data files (gitignored)

---

## Building

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build the benchmark binary
cmake --build build --target taxi_bench_full

# Verify
ls build/bin/taxi_bench_full
```

---

## Data

Download the NYC TLC Yellow Taxi Trip Data for 2020–2023:

```bash
bash scripts/download_tlc_data.sh
```

Place the files as:

```
~/Downloads/taxi_data/
    2020.csv    (~3.1 GB)
    2021.csv    (~3.8 GB)
    2022.csv    (~5.1 GB)
    2023.csv    (~4.7 GB)
```

**Do not commit data files** — they are gitignored.

---

## Running All 3 Phases

```bash
bash scripts/run_benchmark.sh [DATA_DIR] [RUNS]
```

Defaults: `DATA_DIR=~/Downloads/taxi_data`, `RUNS=10`.

This runs all 3 phases (including both Phase 3a and 3b) sequentially, logs timestamped output to `results/bench_local_run.log`, and writes per-phase CSVs to `results/`. System sleep is prevented via `caffeinate` (macOS).

**Estimated total runtime**: 4–8 hours (depends on hardware).

### Running Individual Phases

```bash
BIN=./build/bin/taxi_bench_full
DATA=~/Downloads/taxi_data

# Phase 1 — AoS serial baseline
"$BIN" "$DATA/2020.csv" "$DATA/2021.csv" "$DATA/2022.csv" \
  --serial --runs 10 --output results/bench_phase1_local.csv

# Phase 2 — AoS parallel (8 threads)
"$BIN" "$DATA/2020.csv" "$DATA/2021.csv" "$DATA/2022.csv" \
  --threads 8 --runs 10 --output results/bench_phase2_local.csv

# Phase 3a — SoA converted from AoS (2023.csv only — memory constraint)
"$BIN" "$DATA/2023.csv" \
  --soa --serial --runs 10 --output results/bench_phase3a_local.csv

# Phase 3b — SoA loaded directly from CSV (all 3 CSVs, single-pass)
"$BIN" "$DATA/2020.csv" "$DATA/2021.csv" "$DATA/2022.csv" \
  --soa-direct --serial --runs 10 --output results/bench_phase3b_local.csv
```

---

## Architecture

### Memory Layouts

#### Array-of-Structs (AoS) — Phases 1 & 2

```cpp
struct TripRecord {          // 128 bytes per record
    int vendor_id;
    int64_t pickup_timestamp;
    int64_t dropoff_timestamp;
    int passenger_count;
    double trip_distance;
    // ... 12 more fields
};
std::vector<TripRecord> records;  // contiguous in memory
```

Cache behavior: scanning `trip_distance` loads 128-byte struct per record but uses only 8 bytes → 0.5 records per 64-byte cache line = 94% cache waste per scan.

#### Structure-of-Arrays (SoA) — Phases 3a & 3b

```cpp
struct TripDataSoA {
    std::vector<double> trip_distance;      // one vector per field
    std::vector<double> fare_amount;
    std::vector<int64_t> pickup_timestamp;
    // ... 14 more parallel vectors
};
```

Cache behavior: scanning `trip_distance` fills cache lines with 8 doubles = 8 useful values per line → 100% cache utilization for that field. Enables compiler SIMD auto-vectorization (SSE/AVX).

### SoA Loading Strategies

**`from_aos()`** (Phase 3a): Converts existing `vector<TripRecord>` → SoA. Requires both layouts in memory simultaneously → 2×N peak. Limited to 2023.csv (~37.9M records, ~9.7 GB peak).

**`from_csv()`** (Phase 3b): Single-pass directly from CSV into SoA column vectors. Pre-reserves 95M rows per column. No intermediate AoS → peak memory = SoA only (~11.9 GB). Allows running on all 3 CSVs.

### Component Summary

| Component         | Role                                                         |
| ----------------- | ------------------------------------------------------------ |
| `TripRecord`      | Data struct — 128 bytes, all primitive types                 |
| `CsvReader`       | Streaming RFC 4180 CSV parser, handles 17–19-column variants |
| `DatasetManager`  | AoS loader, multi-CSV accumulation, QueryEngine façade       |
| `ParallelLoader`  | Splits CSV files across N threads for Phase 2 load           |
| `TripDataSoA`     | SoA layout with `from_aos()` and `from_csv()` loaders        |
| `SoAQueryEngine`  | Scan queries over SoA columns; OpenMP-ready                  |
| `BenchmarkRunner` | Runs a callable N times, computes mean and stddev            |
| `MetricsRecorder` | Writes timing results to CSV for analysis                    |

---

## Queries Benchmarked (Q1–Q6)

All queries are scan-based range operations over the full dataset:

| Query | Description                               | Fields scanned     |
| ----- | ----------------------------------------- | ------------------ |
| Q1    | Trips in a time window (Jan 2021)         | `pickup_timestamp` |
| Q2    | Trips with distance 1–5 miles             | `trip_distance`    |
| Q3    | Trips with fare $10–$50                   | `fare_amount`      |
| Q4    | Trips from pickup location ID 100–200     | `pu_location_id`   |
| Q5    | Combined: time + fare + passenger count   | 3 fields           |
| Q6    | Aggregate: average fare over full dataset | `fare_amount`      |

Q6 is a full-dataset reduction — no early exit, maximally stresses memory bandwidth.

---

## Results Output

Each phase writes a CSV to `results/` and the run script auto-generates plots on completion:

```
results/
    bench_phase1_local.csv      # Phase 1 timing data
    bench_phase2_local.csv      # Phase 2 timing data
    bench_phase3a_local.csv     # Phase 3a timing data
    bench_phase3b_local.csv     # Phase 3b timing data
    bench_local_run.log         # Full timestamped run log
    query_times.png             # Grouped bar chart: avg ms per query per phase
    speedup.png                 # Speedup ratios vs Phase 1 baseline
    load_time.png               # CSV load time comparison across phases
```

Plots are generated automatically at the end of `scripts/run_benchmark.sh`.
To regenerate them manually from existing CSVs:

```bash
python3 python/plot_comparison.py \
    --phase1 results/bench_phase1_local.csv \
    --phase2 results/bench_phase2_local.csv \
    --phase3 results/bench_phase3b_local.csv \
    --output results/ \
    --label "94.6M records | 2020+2021+2022 | Phase 3b = SoA direct from CSV"
```

---

## Memory Notes

- Phase 1/2/3b peak ~11.9 GB (AoS only or SoA only for 92.9M records)
- Phase 3a peak ~9.7 GB (AoS 4.8 GB + SoA 4.9 GB for 37.9M records)
- Do **not** run multiple benchmark processes simultaneously — combined RAM will OOM-kill
- On macOS, pre-reserving vectors allocates virtual memory backed by swap; keep reserve close to actual record count (95M for this dataset)

---

## Troubleshooting

**Binary not found:**

```bash
cmake --build build --target taxi_bench_full
```

**OOM / system thrashing:**

- Close all other large applications
- Restart to clear swap before running
- Check `RUNS` arg — default 10 is correct
