#!/bin/bash

###############################################################################
# run_scaling_benchmark.sh — Strong Scaling Benchmark Runner
#
# Produces per-thread CSVs for AoS and SoA-direct at t=1,2,4,8 threads,
# then generates scaling plots via python/plot_scaling.py.
#
# Strategy for t=1 and t=8 endpoints:
#   - If the main benchmark CSVs already exist (bench_phase1_local.csv,
#     bench_phase2_local.csv, bench_phase3b_local.csv), they are copied
#     into results/benchmarks/scaling/ to avoid re-running ~3.5 h of work.
#   - Otherwise they are run fresh.
#   - Intermediate thread counts (t=2, t=4 for both AoS and SoA) are always
#     run unless their scaling CSVs already exist.
#
# Usage:
#   bash scripts/run_scaling_benchmark.sh [DATA_DIR] [RUNS]
#
# Arguments:
#   DATA_DIR   Directory containing 2020.csv, 2021.csv, 2022.csv
#              (default: ~/Downloads/taxi_data)
#   RUNS       Timed iterations per query (default: 10)
#
# Output:
#   results/benchmarks/scaling/scaling_aos_t{1,2,4,8}.csv
#   results/benchmarks/scaling/scaling_soa_t{1,2,4,8}.csv
#   results/benchmarks/scaling/bench_scaling.log
#   results/plots/scaling/scaling_query_time.png
#   results/plots/scaling/scaling_speedup.png
#   results/plots/scaling/scaling_soa_vs_aos.png
#
# Memory note:
#   Each run loads ~18 GB (AoS) or ~12 GB (SoA) for 94.6M records.
#   Close other large apps. 8 GB RAM machines will use heavy swap.
###############################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DATA_DIR="${1:-$HOME/Downloads/taxi_data}"
RUNS="${2:-10}"

BIN="$PROJECT_ROOT/build/bin/taxi_bench_full"
BENCHMARKS="$PROJECT_ROOT/results/benchmarks"
SCALING="$BENCHMARKS/scaling"
PLOTS="$PROJECT_ROOT/results/plots/scaling"
LOG="$SCALING/bench_scaling.log"

# ---- Preflight checks -------------------------------------------------------

echo "================================================================="
echo "CMPE 275 Mini1 — Strong Scaling Benchmark Runner"
echo "================================================================="
echo "Data dir   : $DATA_DIR"
echo "Runs       : $RUNS"
echo "Scaling dir: $SCALING"
echo "Plots dir  : $PLOTS"
echo "Log        : $LOG"
echo ""

if [ ! -f "$BIN" ]; then
    echo "ERROR: Binary not found: $BIN"
    echo "       Run: cmake --build build --target taxi_bench_full"
    exit 1
fi

REQUIRED_CSVS=("2020.csv" "2021.csv" "2022.csv")
for csv in "${REQUIRED_CSVS[@]}"; do
    if [ ! -f "$DATA_DIR/$csv" ]; then
        echo "ERROR: Missing data file: $DATA_DIR/$csv"
        exit 1
    fi
done
echo "All data files found."

mkdir -p "$SCALING" "$PLOTS"
echo "" > "$LOG"

# ---- Prevent sleep ----------------------------------------------------------

if command -v caffeinate &>/dev/null; then
    caffeinate -i &
    CAFF_PID=$!
    echo "caffeinate started (PID $CAFF_PID)"
    trap "kill $CAFF_PID 2>/dev/null; echo 'caffeinate stopped.'" EXIT
fi

# ---- Helper: run or copy a benchmark ----------------------------------------
#
# run_or_copy <src_csv> <dest_csv> <description> <binary_args...>
#
#   If dest_csv already exists  → skip entirely.
#   Elif src_csv exists         → copy src_csv to dest_csv (reuse prior run).
#   Else                        → run the binary with the remaining args.
#
run_or_copy() {
    local src="$1"; shift
    local dst="$1"; shift
    local desc="$1"; shift
    # remaining args are passed to the binary

    if [ -f "$dst" ]; then
        echo "  [skip] $desc — already exists: $(basename "$dst")"
        return
    fi

    if [ -n "$src" ] && [ -f "$src" ]; then
        echo "  [copy] $desc — reusing $(basename "$src")"
        cp "$src" "$dst"
        {
          echo "--- $desc (copied from $(basename "$src")) ---"
          cat "$dst"
          echo ""
        } >> "$LOG"
        return
    fi

    echo "  [run]  $desc — starting: $(date)"
    {
      echo "--- $desc (new run): $(date) ---"
      caffeinate -i "$BIN" \
        "$DATA_DIR/2020.csv" "$DATA_DIR/2021.csv" "$DATA_DIR/2022.csv" \
        "$@" --runs "$RUNS" --output "$dst"
      echo "--- $desc done: $(date) ---"
      echo ""
    } 2>&1 | tee -a "$LOG"
}

# ---- AoS benchmarks ---------------------------------------------------------

echo ""
echo "=== AoS benchmarks ==="

run_or_copy \
    "$BENCHMARKS/bench_phase1_local.csv" \
    "$SCALING/scaling_aos_t1.csv" \
    "AoS t=1 (serial)" \
    --serial

run_or_copy \
    "" \
    "$SCALING/scaling_aos_t2.csv" \
    "AoS t=2" \
    --threads 2

run_or_copy \
    "" \
    "$SCALING/scaling_aos_t4.csv" \
    "AoS t=4" \
    --threads 4

run_or_copy \
    "$BENCHMARKS/bench_phase2_local.csv" \
    "$SCALING/scaling_aos_t8.csv" \
    "AoS t=8 (parallel)" \
    --threads 8

# ---- SoA benchmarks ---------------------------------------------------------

echo ""
echo "=== SoA-direct benchmarks ==="

run_or_copy \
    "$BENCHMARKS/bench_phase3b_local.csv" \
    "$SCALING/scaling_soa_t1.csv" \
    "SoA t=1 (serial)" \
    --soa-direct --serial

run_or_copy \
    "" \
    "$SCALING/scaling_soa_t2.csv" \
    "SoA t=2" \
    --soa-direct --threads 2

run_or_copy \
    "" \
    "$SCALING/scaling_soa_t4.csv" \
    "SoA t=4" \
    --soa-direct --threads 4

run_or_copy \
    "" \
    "$SCALING/scaling_soa_t8.csv" \
    "SoA t=8" \
    --soa-direct --threads 8

# ---- Generate plots ---------------------------------------------------------

echo ""
echo "=== Generating scaling plots ==="

if command -v python3 &>/dev/null && \
   python3 -c "import pandas, matplotlib, numpy" &>/dev/null 2>&1; then
    python3 "$PROJECT_ROOT/python/plot_scaling.py" \
        --results "$SCALING/" \
        --output  "$PLOTS/"
    echo "Plots written to: $PLOTS/"
else
    echo "WARNING: python3 or pandas/matplotlib/numpy not found — skipping plots."
    echo "         Install: pip3 install pandas matplotlib numpy"
fi

echo ""
echo "=== SCALING BENCHMARK COMPLETE: $(date) ==="
echo ""
echo "CSVs : $SCALING/"
echo "Plots: $PLOTS/"
echo "Log  : $LOG"
