#!/bin/bash

###############################################################################
# run_benchmark.sh — Full 3-Phase Benchmark Runner
#
# Runs all benchmark phases sequentially and logs timestamped output to
# results/bench_local_run.log. Each phase also writes its own CSV.
#
# Usage:
#   bash scripts/run_benchmark.sh [DATA_DIR] [RUNS]
#
# Arguments:
#   DATA_DIR   Directory containing 2020.csv, 2021.csv, 2022.csv, 2023.csv
#              (default: ~/Downloads/taxi_data)
#   RUNS       Number of timed iterations per query (default: 10)
#
# Phases:
#   Phase 1  — AoS serial,           2020+2021+2022 (3 CSVs, ~92.9M records)
#   Phase 2  — AoS parallel (8T),    2020+2021+2022 (3 CSVs, ~92.9M records)
#   Phase 3a — SoA from AoS,         2023.csv only  (~37.9M records)
#   Phase 3b — SoA direct from CSV,  2020+2021+2022 (3 CSVs, ~92.9M records)
#
# Memory note:
#   Phase 1/2/3b peak ~12 GB. Phase 3a (AoS+SoA simultaneously) peaks ~9.7 GB.
#   Requires 16 GB RAM. Close other large apps before running.
###############################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DATA_DIR="${1:-$HOME/Downloads/taxi_data}"
RUNS="${2:-10}"

BIN="$PROJECT_ROOT/build/bin/taxi_bench_full"
BENCHMARKS="$PROJECT_ROOT/results/benchmarks"
PLOTS="$PROJECT_ROOT/results/plots"
LOG="$BENCHMARKS/bench_local_run.log"

# ---- Preflight checks -------------------------------------------------------

echo "================================================================="
echo "CMPE 275 Mini1 — Full 3-Phase Benchmark Runner"
echo "================================================================="
echo "Data dir   : $DATA_DIR"
echo "Runs       : $RUNS"
echo "Benchmarks : $BENCHMARKS"
echo "Plots      : $PLOTS"
echo "Log        : $LOG"
echo ""

if [ ! -f "$BIN" ]; then
    echo "ERROR: Binary not found: $BIN"
    echo "       Run: cmake --build build --target taxi_bench_full"
    exit 1
fi

REQUIRED_CSVS=("2020.csv" "2021.csv" "2022.csv" "2023.csv")
for csv in "${REQUIRED_CSVS[@]}"; do
    if [ ! -f "$DATA_DIR/$csv" ]; then
        echo "ERROR: Missing data file: $DATA_DIR/$csv"
        echo "       Run: bash scripts/download_tlc_data.sh"
        exit 1
    fi
done
echo "All data files found."

mkdir -p "$BENCHMARKS" "$PLOTS"

# ---- Prevent sleep ----------------------------------------------------------

if command -v caffeinate &>/dev/null; then
    caffeinate -i &
    CAFF_PID=$!
    echo "caffeinate started (PID $CAFF_PID) — system will not sleep."
    trap "kill $CAFF_PID 2>/dev/null; echo 'caffeinate stopped.'" EXIT
fi

# ---- Clear previous results -------------------------------------------------

echo "" > "$LOG"
rm -f "$BENCHMARKS/bench_phase1_local.csv" \
      "$BENCHMARKS/bench_phase2_local.csv" \
      "$BENCHMARKS/bench_phase3a_local.csv" \
      "$BENCHMARKS/bench_phase3b_local.csv"

# ---- Run phases -------------------------------------------------------------

{
  echo "=== Full 3-Phase Benchmark: $(date) ==="
  echo "Data dir : $DATA_DIR"
  echo "Runs/query: $RUNS"
  echo ""

  # ------------------------------------------------------------------
  # Phase 1: Array-of-Structs, serial, 3 CSVs
  # ------------------------------------------------------------------
  echo "--- Phase 1 (AoS serial, 3 CSVs) start: $(date) ---"
  "$BIN" \
    "$DATA_DIR/2020.csv" "$DATA_DIR/2021.csv" "$DATA_DIR/2022.csv" \
    --serial --runs "$RUNS" \
    --output "$BENCHMARKS/bench_phase1_local.csv"
  echo "--- Phase 1 done: $(date) ---"
  echo ""

  # ------------------------------------------------------------------
  # Phase 2: Array-of-Structs, parallel (8 threads), 3 CSVs
  # ------------------------------------------------------------------
  echo "--- Phase 2 (AoS parallel 8T, 3 CSVs) start: $(date) ---"
  "$BIN" \
    "$DATA_DIR/2020.csv" "$DATA_DIR/2021.csv" "$DATA_DIR/2022.csv" \
    --threads 8 --runs "$RUNS" \
    --output "$BENCHMARKS/bench_phase2_local.csv"
  echo "--- Phase 2 done: $(date) ---"
  echo ""

  # ------------------------------------------------------------------
  # Phase 3a: SoA from AoS conversion, serial, 2023.csv only
  # (limited to single CSV — AoS+SoA simultaneously = 2x peak memory)
  # ------------------------------------------------------------------
  echo "--- Phase 3a (SoA from AoS, 2023 only) start: $(date) ---"
  "$BIN" \
    "$DATA_DIR/2023.csv" \
    --soa --serial --runs "$RUNS" \
    --output "$BENCHMARKS/bench_phase3a_local.csv"
  echo "--- Phase 3a done: $(date) ---"
  echo ""

  # ------------------------------------------------------------------
  # Phase 3b: SoA direct from CSV (no intermediate AoS), serial, 3 CSVs
  # (single-pass load, peak memory = SoA only — fits in 16 GB RAM)
  # ------------------------------------------------------------------
  echo "--- Phase 3b (SoA direct CSV, 3 CSVs) start: $(date) ---"
  "$BIN" \
    "$DATA_DIR/2020.csv" "$DATA_DIR/2021.csv" "$DATA_DIR/2022.csv" \
    --soa-direct --serial --runs "$RUNS" \
    --output "$BENCHMARKS/bench_phase3b_local.csv"
  echo "--- Phase 3b done: $(date) ---"
  echo ""

  echo "=== ALL PHASES COMPLETE: $(date) ==="
  echo ""
  echo "Results written to:"
  echo "  $BENCHMARKS/bench_phase1_local.csv"
  echo "  $BENCHMARKS/bench_phase2_local.csv"
  echo "  $BENCHMARKS/bench_phase3a_local.csv"
  echo "  $BENCHMARKS/bench_phase3b_local.csv"

} 2>&1 | tee -a "$LOG"

# ---- Generate plots ---------------------------------------------------------

echo ""
echo "--- Generating comparison plots ---"
if command -v python3 &>/dev/null && \
   python3 -c "import pandas, matplotlib, numpy" &>/dev/null 2>&1; then
    python3 "$PROJECT_ROOT/python/plot_comparison.py" \
        --phase1 "$BENCHMARKS/bench_phase1_local.csv" \
        --phase2 "$BENCHMARKS/bench_phase2_local.csv" \
        --phase3 "$BENCHMARKS/bench_phase3b_local.csv" \
        --output "$PLOTS/" \
        --label "94.6M records | 2020+2021+2022"
    echo "Plots written to: $PLOTS/{query_times,speedup,load_time}.png"
else
    echo "WARNING: python3 or pandas/matplotlib/numpy not found — skipping plots."
    echo "         Install: pip3 install pandas matplotlib numpy"
fi
