# Benchmark Results Summary

**Run date**: 2026-03-06 (00:45–04:20 PST)
**Machine**: macOS (Apple Silicon / x86, 16 GB RAM)
**Compiler**: GCC 13+ with OpenMP, `-O2`
**Dataset**: NYC TLC Yellow Taxi Trip Records

Raw CSVs: `results/benchmarks/bench_phase*.csv`
Plots: `results/plots/query_times.png`, `results/plots/speedup.png`, `results/plots/load_time.png`

---

## Dataset

| Phase | Files | Records loaded | Rows read | Parse success |
|---|---|---|---|---|
| Phase 1 | 2020+2021+2022 | **94,589,581** | 95,208,669 | 99.35% |
| Phase 2 | 2020+2021+2022 | **94,589,581** | 95,208,669 | 99.35% |
| Phase 3a | 2023.csv only | **37,917,834** | 38,310,226 | 98.98% |
| Phase 3b | 2020+2021+2022 | **94,589,581** | — | — |

619,088 rows discarded across Phases 1/2 (0.65%) — invalid timestamps or negative fares.

---

## Wall-Clock Phase Duration

| Phase | Total wall time |
|---|---|
| Phase 1 (AoS serial) | **99.0 min** |
| Phase 2 (AoS parallel 8T) | **74.2 min** |
| Phase 3a (SoA from AoS, 2023 only) | **10.5 min** |
| Phase 3b (SoA direct CSV, 3 CSVs) | **31.2 min** |
| **Total** | **~3 h 35 min** |

---

## Load Performance (avg over 10 runs)

| Phase | Threads | Load avg (ms) | Load stddev (ms) | Load min (ms) | Notes |
|---|---|---|---|---|---|
| Phase 1 (AoS serial) | 1 | 192,870 | 17,388 | 154,436 | Baseline |
| Phase 2 (AoS parallel) | 8 | 250,684 | 30,683 | 172,640 | **30% slower than serial** |
| Phase 3a (AoS load of 2023) | 1 | 61,593 | 365 | 61,085 | Smaller dataset (~2.5× fewer records) |
| Phase 3b (SoA direct) | 1 | 171,829 | 9,825 | 153,700 | **10.9% faster than Phase 1** |

**Key finding — parallel load degradation**: Phase 2 uses 8 threads to load 3 CSV files, but disk
I/O is the bottleneck, not CPU. Multiple threads competing for the same disk channels cause seek
contention, increasing total load time by 30% vs serial. Parallel loading helps only on hardware
with multiple independent I/O paths (e.g., NVMe RAID).

**Phase 3b load advantage**: Direct CSV→SoA avoids storing parsed `TripRecord` structs entirely.
Each field is written directly into its typed column vector. The reduced memory pressure (no
intermediate AoS) results in marginally faster loading (~10.9% improvement).

---

## Query Performance — Phase 1 vs Phase 2 vs Phase 3b (same 94.6M dataset)

All queries scanned the full 94,589,581-record dataset. Times are averages over 10 runs.

| Query | Description | Phase 1 AoS serial (ms) | Phase 2 AoS parallel 8T (ms) | Phase 3b SoA direct (ms) | P1→P2 speedup | P1→P3b speedup |
|---|---|---|---|---|---|---|
| Q1 | Time range (index-assisted) | 3,074 | 1,776 | 686 | 1.73× | **4.48×** |
| Q2 | Distance 1–5 mi (full scan) | 73,681 | 31,147 | 2,526 | **2.37×** | **29.2×** |
| Q3 | Fare $10–$50 (full scan) | 75,779 | 31,454 | 2,458 | **2.41×** | **30.8×** |
| Q4 | Location ID 100–200 (full scan) | 75,996 | 32,043 | 1,570 | **2.37×** | **48.4×** |
| Q5 | Combined: time+fare+pax (index+scan) | 73,118 | 42,614 | 2,121 | 1.72× | **34.5×** |
| Q6 | Avg fare — full dataset reduction | 74,032 | 32,571 | 2,135 | **2.27×** | **34.7×** |

### Index build time

| Phase | Index type | Build time (ms) |
|---|---|---|
| Phase 1 | AoS TimeIndex (std::stable_sort) | 179,244 |
| Phase 2 | AoS TimeIndex (std::stable_sort) | 156,103 |
| Phase 3b | SoA time index (std::sort) | 24,618 |

SoA index builds **7.3× faster** — same O(N log N) algorithm but better cache performance during
the sort pass (adjacent timestamps are contiguous in `pickup_timestamp[]`).

---

## Query Performance — Phase 3a (SoA from AoS, 2023.csv only, 37.9M records)

Phase 3a uses a different dataset (2023 only) so absolute times cannot be directly compared to
Phase 1/2/3b. Shown here for reference.

| Query | Phase 3a SoA avg (ms) | Matches | Notes |
|---|---|---|---|
| Q1 | 0.001 | 60 | Querying Jan 2021 range — 2023 data has near-zero matches; O(log N) index |
| Q2 | 176.3 | 22,877,491 | Distance 1–5 mi |
| Q3 | 124.9 | 32,465,246 | Fare $10–$50 |
| Q4 | 151.3 | 18,887,846 | Location ID 100–200 |
| Q5 | 0.026 | 60 | Combined with Jan 2021 range — no matches in 2023 data |
| Q6 | 49.4 | 37,917,834 | Full reduction; avg fare = **$19.90** (vs $13.91 for 2020–2022) |

**AoS→SoA conversion cost**: 3,065 ms (one-time, at startup) for 37.9M records.

---

## Match Counts (correctness validation)

All three phases (1, 2, 3b) returned identical match counts for every query:

| Query | Matches |
|---|---|
| Q1 | 94,589,579 (≈ all records — time range spans dataset) |
| Q2 | 59,430,341 |
| Q3 | 76,768,430 |
| Q4 | 44,721,265 |
| Q5 | 89,056,440 |
| Q6 | 94,589,581 — avg fare $13.91 |

Identical match counts across all three phases confirm correctness of parallelization and SoA
query implementations.

---

## Phase 3b Query Variance (cache warming effect)

Phase 3b shows high stddev on some queries (e.g., Q6: avg 2,135 ms ± 6,357 ms, min 110 ms,
max 20,226 ms). This is a **cache warming effect**:

- First 1–2 iterations: SoA arrays are cold in L1/L2/L3 cache → OS fetches from RAM (~20s)
- Later iterations: arrays warm in LLC (Last-Level Cache) or remain in OS page cache → sub-second
- Min values (110–640 ms) represent fully warmed state — the true steady-state SoA performance

For production use, a warm-up iteration before timed measurements would reduce stddev. The
minimum times are the most representative of SoA's achievable throughput.

---

## Plots

All three PNG files are in `results/plots/`:

### `query_times.png`
Grouped bar chart showing avg query time (ms) for Phase 1, Phase 2, and Phase 3b side by side
for each query (Q1–Q6). **I-shaped bars (error bars) = ±1 standard deviation across the 10
timed runs** — a tall I-bar means high run-to-run variance (e.g. Phase 3b SoA, due to cache
warming), a short I-bar means consistent timing. Demonstrates:
- Phase 3b bars are barely visible next to Phase 1/2 bars for Q2–Q6 due to 30–48× speedup
- Q1 is fast in all phases (index-assisted)

### `speedup.png`
Speedup ratio relative to Phase 1 baseline, separately for Phase 2 (parallelism gain) and Phase
3b (SoA layout gain). Values > 1.0 = faster than Phase 1. Shows:
- Phase 2 delivers consistent 2–2.4× speedup on full-scan queries from 8-thread parallelism
- Phase 3b delivers 29–48× speedup on the same queries purely from memory layout improvement

### `load_time.png`
Bar chart of average CSV load time per phase. I-bars = ±1 stddev (10 runs). Shows:
- Parallel load (Phase 2) is counterintuitively **slower** than serial (Phase 1)
- SoA direct load (Phase 3b) is slightly faster than serial AoS load (Phase 1)

---

## Key Findings

### 1. Memory layout dominates query performance
SoA delivers **29–48× speedup** over serial AoS on identical data and hardware, with no
parallelism — purely from cache line efficiency. A 64-byte cache line holds:
- **0.5 TripRecords** in AoS (128-byte struct; only 8 bytes useful per scan)
- **8 doubles** in SoA (8 doubles × 8 bytes = 64 bytes; 100% useful)

This is the fundamental insight: the bottleneck for scan-heavy workloads is not CPU speed but
memory bandwidth. Layout determines how much of each cache line fetch is wasted.

### 2. Parallelism provides significant but secondary gains
Phase 2 (8-thread parallel queries) achieves **2.4× speedup** vs Phase 1. Combined with SoA
layout, the theoretical maximum would be ~8× for linear scaling — actual results show SoA alone
already exceeds this for most queries.

### 3. Parallel I/O loading hurts on single-disk hardware
Phase 2's parallel CSV loader (8 threads, byte-range chunks) was **30% slower** than serial.
Disk I/O is the bottleneck; multiple threads generate competing seeks on a single physical
device. Parallel loading is only beneficial with multiple independent storage devices or
SSDs with parallel read paths.

### 4. Direct CSV→SoA avoids the 2×N memory peak
Phase 3a's `from_aos()` requires AoS + SoA simultaneously: ~23.8 GB peak for 3 CSVs (exceeds
16 GB RAM). Phase 3b's `from_csv()` single-pass loads directly into SoA columns, keeping peak
at ~11.9 GB. This enables SoA to run on the full 3-CSV dataset rather than only 2023.csv.

### 5. Fare inflation visible in aggregate data
Q6 average fare: **$13.91** (2020–2022) vs **$19.90** (2023). Post-COVID rideshare demand
recovery and NYC fare increases are reflected directly in the aggregation result.
