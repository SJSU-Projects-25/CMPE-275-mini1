#!/usr/bin/env python3
"""
plot_scaling.py — Strong scaling analysis for CMPE-275 Mini 1.

Reads per-thread benchmark CSVs and produces:
  1. scaling_query_time.png  — avg query time vs thread count (log-log)
  2. scaling_speedup.png     — speedup vs thread count vs ideal linear
  3. scaling_soa_vs_aos.png  — AoS vs SoA at each thread count per query

Usage:
    python3 plot_scaling.py --output results/

It auto-discovers CSVs based on fixed naming convention in results/:
  bench_phase1_real.csv        → AoS serial (1 thread baseline)
  scaling_aos_t2.csv           → AoS 2 threads
  scaling_aos_t4.csv           → AoS 4 threads
  bench_phase2_real.csv        → AoS 8 threads
  scaling_soa_t2.csv           → SoA 2 threads
  scaling_soa_t4.csv           → SoA 4 threads
  bench_phase3_real.csv        → SoA 8 threads
"""

import argparse
import os
import sys

import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# Queries to include in scaling analysis (full linear scans only)
SCALE_QUERIES = ["Q2", "Q3", "Q4", "Q6"]

QUERY_LABELS = {
    "Q2": "Distance range",
    "Q3": "Fare range",
    "Q4": "Location filter",
    "Q6": "Fare aggregation",
}

# Colours per query
Q_COLORS = {
    "Q2": "#4C72B0",
    "Q3": "#DD8452",
    "Q4": "#55A868",
    "Q6": "#C44E52",
}

# Marker styles
AOS_MARKER = "o"
SOA_MARKER = "s"


def load_query_row(csv_path: str, query_id: str):
    """Return (avg_ms, stddev_ms) for one query from a CSV, or None if missing."""
    if not os.path.isfile(csv_path):
        return None
    df = pd.read_csv(csv_path)
    df.columns = df.columns.str.strip()
    row = df[df["query"] == query_id]
    if row.empty:
        return None
    return float(row["avg_ms"].iloc[0]), float(row["stddev_ms"].iloc[0])


def build_scaling_table(results_dir: str):
    """
    Returns:
      aos_data[query] = {thread_count: (avg_ms, std_ms), ...}
      soa_data[query] = {thread_count: (avg_ms, std_ms), ...}
    """
    aos_files = {
        1: os.path.join(results_dir, "bench_phase1_real.csv"),
        2: os.path.join(results_dir, "scaling_aos_t2.csv"),
        4: os.path.join(results_dir, "scaling_aos_t4.csv"),
        8: os.path.join(results_dir, "bench_phase2_real.csv"),
    }
    soa_files = {
        2: os.path.join(results_dir, "scaling_soa_t2.csv"),
        4: os.path.join(results_dir, "scaling_soa_t4.csv"),
        8: os.path.join(results_dir, "bench_phase3_real.csv"),
    }

    aos_data, soa_data = {}, {}
    for q in SCALE_QUERIES:
        aos_data[q], soa_data[q] = {}, {}
        for t, path in aos_files.items():
            result = load_query_row(path, q)
            if result:
                aos_data[q][t] = result
        for t, path in soa_files.items():
            result = load_query_row(path, q)
            if result:
                soa_data[q][t] = result

    return aos_data, soa_data


def save(fig, path):
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {path}")


# ── Plot 1: Query time vs thread count (log scale) ───────────────────────────

def plot_query_time(aos_data, soa_data, out_dir):
    fig, axes = plt.subplots(2, 2, figsize=(13, 9))
    axes = axes.flatten()

    for ax, q in zip(axes, SCALE_QUERIES):
        col = Q_COLORS[q]

        # AoS line
        if aos_data[q]:
            threads = sorted(aos_data[q])
            avgs    = [aos_data[q][t][0] for t in threads]
            stds    = [aos_data[q][t][1] for t in threads]
            ax.errorbar(threads, avgs, yerr=stds, marker=AOS_MARKER,
                        color=col, linestyle="-", capsize=4,
                        label=f"AoS (serial→parallel)", linewidth=2)

        # SoA line
        if soa_data[q]:
            threads = sorted(soa_data[q])
            avgs    = [soa_data[q][t][0] for t in threads]
            stds    = [soa_data[q][t][1] for t in threads]
            ax.errorbar(threads, avgs, yerr=stds, marker=SOA_MARKER,
                        color=col, linestyle="--", capsize=4,
                        label=f"SoA (parallel)", linewidth=2, alpha=0.8)

        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x)}"))
        ax.set_xticks([1, 2, 4, 8])
        ax.set_xlabel("Threads")
        ax.set_ylabel("Avg query time (ms, log)")
        ax.set_title(f"{q} — {QUERY_LABELS[q]}")
        ax.legend(fontsize=8)
        ax.grid(True, which="both", linestyle="--", alpha=0.4)

    fig.suptitle("Strong scaling — query time vs thread count\n(2022 TLC, 39.4M rows)", y=1.01)
    fig.tight_layout()
    save(fig, os.path.join(out_dir, "scaling_query_time.png"))


# ── Plot 2: Speedup vs thread count (vs AoS 1-thread baseline) ────────────────

def plot_speedup(aos_data, soa_data, out_dir):
    fig, ax = plt.subplots(figsize=(9, 6))

    threads_all = [1, 2, 4, 8]
    ideal = threads_all
    ax.plot(threads_all, ideal, "k--", linewidth=1, label="Ideal linear speedup")

    for q in SCALE_QUERIES:
        if not aos_data[q] or 1 not in aos_data[q]:
            continue
        baseline = aos_data[q][1][0]
        col = Q_COLORS[q]

        # AoS speedup
        aos_threads = sorted(t for t in aos_data[q] if t >= 1)
        aos_speedup = [baseline / aos_data[q][t][0] for t in aos_threads]
        ax.plot(aos_threads, aos_speedup, marker=AOS_MARKER,
                color=col, linestyle="-", linewidth=2,
                label=f"{q} AoS")

        # SoA speedup (vs same AoS-1thread baseline)
        if soa_data[q]:
            soa_threads = sorted(soa_data[q])
            soa_speedup = [baseline / soa_data[q][t][0] for t in soa_threads]
            ax.plot(soa_threads, soa_speedup, marker=SOA_MARKER,
                    color=col, linestyle="--", linewidth=2, alpha=0.8,
                    label=f"{q} SoA")

    ax.set_xscale("log", base=2)
    ax.set_yscale("log", base=2)
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x)}"))
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda y, _: f"{y:.0f}×"))
    ax.set_xticks([1, 2, 4, 8])
    ax.set_xlabel("Thread count")
    ax.set_ylabel("Speedup vs AoS 1-thread (log)")
    ax.set_title("Strong scaling speedup\n(2022 TLC, 39.4M rows — baseline = AoS serial)")
    ax.legend(fontsize=8, ncol=2)
    ax.grid(True, which="both", linestyle="--", alpha=0.4)

    save(fig, os.path.join(out_dir, "scaling_speedup.png"))


# ── Plot 3: AoS vs SoA side-by-side at each thread count ─────────────────────

def plot_aos_vs_soa(aos_data, soa_data, out_dir):
    """Grouped bar chart: for each query, show AoS and SoA at threads 2, 4, 8."""
    thread_counts = [2, 4, 8]
    n_threads     = len(thread_counts)
    queries       = SCALE_QUERIES
    n_queries     = len(queries)

    x   = np.arange(n_queries)
    w   = 0.12
    fig, ax = plt.subplots(figsize=(12, 6))

    # Colour gradient per thread count
    aos_colours = ["#9AB0D4", "#6A90BE", "#2255A4"]
    soa_colours = ["#A8D5B2", "#6EB888", "#217A3C"]

    for i, t in enumerate(thread_counts):
        aos_avgs = [aos_data[q].get(t, (np.nan, 0))[0] for q in queries]
        soa_avgs = [soa_data[q].get(t, (np.nan, 0))[0] for q in queries]

        offset_aos = (2 * i - n_threads + 0.5) * w - w * 0.5
        offset_soa = offset_aos + w

        ax.bar(x + offset_aos, aos_avgs, w, label=f"AoS {t}t",
               color=aos_colours[i], alpha=0.9)
        ax.bar(x + offset_soa, soa_avgs, w, label=f"SoA {t}t",
               color=soa_colours[i], alpha=0.9)

    ax.set_yscale("log")
    ax.set_xticks(x)
    ax.set_xticklabels([f"{q}\n{QUERY_LABELS[q]}" for q in queries])
    ax.set_ylabel("Avg query time (ms, log scale)")
    ax.set_title("AoS vs SoA query time at each thread count\n(2022 TLC, 39.4M rows)")
    ax.legend(fontsize=8, ncol=2)
    ax.grid(axis="y", linestyle="--", alpha=0.4)

    save(fig, os.path.join(out_dir, "scaling_soa_vs_aos.png"))


# ── entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Plot strong scaling analysis")
    parser.add_argument("--results", default="results/", help="Directory with benchmark CSVs")
    parser.add_argument("--output",  default="results/", help="Output directory for PNGs")
    args = parser.parse_args()

    if not os.path.isdir(args.results):
        print(f"ERROR: results directory not found: {args.results}", file=sys.stderr)
        sys.exit(1)

    os.makedirs(args.output, exist_ok=True)

    aos_data, soa_data = build_scaling_table(args.results)

    print("Building scaling charts...")
    plot_query_time(aos_data, soa_data, args.output)
    plot_speedup(aos_data, soa_data, args.output)
    plot_aos_vs_soa(aos_data, soa_data, args.output)
    print("Done.")


if __name__ == "__main__":
    main()
