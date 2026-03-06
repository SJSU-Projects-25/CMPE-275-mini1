#!/usr/bin/env python3
"""
plot_scaling.py — Strong scaling analysis for CMPE-275 Mini 1.

Reads per-thread benchmark CSVs and produces:
  1. scaling_query_time.png  — avg query time vs thread count (log-log)
  2. scaling_speedup.png     — speedup vs thread count vs ideal linear
  3. scaling_soa_vs_aos.png  — AoS vs SoA at each thread count per query

Usage:
    python3 plot_scaling.py --output results/plots/scaling/

It auto-discovers CSVs based on fixed naming convention in results/benchmarks/scaling/:
  scaling_aos_t1.csv           → AoS 1 thread  (baseline)
  scaling_aos_t2.csv           → AoS 2 threads
  scaling_aos_t4.csv           → AoS 4 threads
  scaling_aos_t8.csv           → AoS 8 threads
  scaling_soa_t1.csv           → SoA 1 thread
  scaling_soa_t2.csv           → SoA 2 threads
  scaling_soa_t4.csv           → SoA 4 threads
  scaling_soa_t8.csv           → SoA 8 threads
"""

import argparse
import os
import sys

import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import matplotlib.lines as mlines
import numpy as np


# ── colour palette (shared with plot_comparison.py) ───────────────────────────
C1  = "#FDACAC"   # AoS / Phase 1   (light coral)
C2  = "#7C96AB"   # Phase 2 parallel (slate blue)
C3  = "#6F826A"   # SoA / Phase 3   (sage green)
ERR = "#735557"   # error bars       (dark mauve)

# AoS bar shades: lightest → darkest coral  (t=1, 2, 4, 8)
AOS_SHADES = ["#FEE5E5", "#FDACAC", "#E87070", "#C84040"]
# SoA bar shades: lightest → darkest sage   (t=1, 2, 4, 8)
SOA_SHADES = ["#C5D4C2", "#6F826A", "#4A6B4A", "#2A4A2A"]

# Per-query colours for the multi-line speedup plot
Q_COLORS = {
    "Q1": "#7C96AB",
    "Q2": "#FDACAC",
    "Q3": "#6F826A",
    "Q4": "#735557",
    "Q5": "#D97D55",
    "Q6": "#9B7EBD",
}

AOS_MARKER = "o"
SOA_MARKER = "s"

SCALE_QUERIES = ["Q1", "Q2", "Q3", "Q4", "Q5", "Q6"]

QUERY_LABELS = {
    "Q1": "Time range (indexed)",
    "Q2": "Distance range",
    "Q3": "Fare range",
    "Q4": "Location filter",
    "Q5": "Combined (index+scan)",
    "Q6": "Fare aggregation",
}

DATASET_LABEL = "94.6M records | 2020+2021+2022"


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
        1: os.path.join(results_dir, "scaling_aos_t1.csv"),
        2: os.path.join(results_dir, "scaling_aos_t2.csv"),
        4: os.path.join(results_dir, "scaling_aos_t4.csv"),
        8: os.path.join(results_dir, "scaling_aos_t8.csv"),
    }
    soa_files = {
        1: os.path.join(results_dir, "scaling_soa_t1.csv"),
        2: os.path.join(results_dir, "scaling_soa_t2.csv"),
        4: os.path.join(results_dir, "scaling_soa_t4.csv"),
        8: os.path.join(results_dir, "scaling_soa_t8.csv"),
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
    fig, axes = plt.subplots(2, 3, figsize=(15, 9))
    axes = axes.flatten()

    for ax, q in zip(axes, SCALE_QUERIES):
        # AoS line
        if aos_data[q]:
            threads = sorted(aos_data[q])
            avgs    = [aos_data[q][t][0] for t in threads]
            stds    = [aos_data[q][t][1] for t in threads]
            ax.errorbar(threads, avgs, yerr=stds, marker=AOS_MARKER,
                        color=C1, linestyle="-", capsize=4, ecolor=ERR,
                        linewidth=2)

        # SoA line
        if soa_data[q]:
            threads = sorted(soa_data[q])
            avgs    = [soa_data[q][t][0] for t in threads]
            stds    = [soa_data[q][t][1] for t in threads]
            ax.errorbar(threads, avgs, yerr=stds, marker=SOA_MARKER,
                        color=C3, linestyle="--", capsize=4, ecolor=ERR,
                        linewidth=2, alpha=0.9)

        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x)}"))
        ax.set_xticks([1, 2, 4, 8])
        ax.set_xlabel("Threads")
        ax.set_ylabel("Avg query time (ms, log)")
        ax.set_title(f"{q} — {QUERY_LABELS[q]}")
        legend_handles = [
            mlines.Line2D([], [], color=C1, marker=AOS_MARKER, linewidth=2, label="AoS"),
            mlines.Line2D([], [], color=C3, marker=SOA_MARKER, linestyle="--", linewidth=2, label="SoA"),
        ]
        ax.legend(handles=legend_handles, fontsize=8)
        ax.grid(True, which="both", linestyle="--", alpha=0.4)

    fig.suptitle(f"Strong scaling — query time vs thread count\n{DATASET_LABEL}", y=1.01)
    fig.tight_layout()
    save(fig, os.path.join(out_dir, "scaling_query_time.png"))


# ── Plot 2: Speedup vs thread count (vs AoS 1-thread baseline) ────────────────

def plot_speedup(aos_data, soa_data, out_dir):
    fig, (ax_aos, ax_soa) = plt.subplots(1, 2, figsize=(16, 6), sharey=False)

    threads_all = [1, 2, 4, 8]
    for ax in (ax_aos, ax_soa):
        ax.plot(threads_all, threads_all, "k--", linewidth=1, label="Ideal linear")

    for q in SCALE_QUERIES:
        if not aos_data[q] or 1 not in aos_data[q]:
            continue
        baseline = aos_data[q][1][0]
        col = Q_COLORS[q]

        # Left: AoS speedup
        aos_threads = sorted(aos_data[q])
        aos_speedup = [baseline / aos_data[q][t][0] for t in aos_threads]
        ax_aos.plot(aos_threads, aos_speedup, marker=AOS_MARKER,
                    color=col, linestyle="-", linewidth=2, label=q)

        # Right: SoA speedup (vs same AoS-1thread baseline)
        if soa_data[q]:
            soa_threads = sorted(soa_data[q])
            soa_speedup = [baseline / soa_data[q][t][0] for t in soa_threads]
            ax_soa.plot(soa_threads, soa_speedup, marker=SOA_MARKER,
                        color=col, linestyle="-", linewidth=2, label=q)

    fmt_x = ticker.FuncFormatter(lambda x, _: f"{int(x)}")
    fmt_y = ticker.FuncFormatter(lambda y, _: f"{y:.0f}×")

    for ax, title in ((ax_aos, "AoS — parallel scaling"),
                      (ax_soa, "SoA — parallel scaling")):
        ax.set_xscale("log", base=2)
        ax.set_yscale("log", base=2)
        ax.xaxis.set_major_formatter(fmt_x)
        ax.yaxis.set_major_formatter(fmt_y)
        ax.set_xticks([1, 2, 4, 8])
        ax.set_xlabel("Thread count")
        ax.legend(fontsize=8, ncol=2)
        ax.grid(True, which="both", linestyle="--", alpha=0.4)
        ax.set_title(title)

    ax_aos.set_ylabel("Speedup vs AoS 1-thread (log)")
    fig.suptitle(f"Strong scaling speedup — baseline = AoS serial (1 thread)\n{DATASET_LABEL}")
    fig.tight_layout()
    save(fig, os.path.join(out_dir, "scaling_speedup.png"))


# ── Plot 3: AoS vs SoA side-by-side at each thread count ─────────────────────

def plot_aos_vs_soa(aos_data, soa_data, out_dir):
    """Grouped bar chart: for each query, show AoS and SoA at threads 1, 2, 4, 8."""
    thread_counts = [1, 2, 4, 8]
    n_threads     = len(thread_counts)
    queries       = SCALE_QUERIES
    n_queries     = len(queries)

    x   = np.arange(n_queries)
    w   = 0.09
    fig, ax = plt.subplots(figsize=(14, 6))

    for i, t in enumerate(thread_counts):
        aos_avgs = [aos_data[q].get(t, (np.nan, 0))[0] for q in queries]
        soa_avgs = [soa_data[q].get(t, (np.nan, 0))[0] for q in queries]

        offset_aos = (2 * i - n_threads + 0.5) * w - w * 0.5
        offset_soa = offset_aos + w

        ax.bar(x + offset_aos, aos_avgs, w, label=f"AoS {t}t",
               color=AOS_SHADES[i], alpha=0.9, edgecolor="#888888", linewidth=0.5)
        ax.bar(x + offset_soa, soa_avgs, w, label=f"SoA {t}t",
               color=SOA_SHADES[i], alpha=0.9, edgecolor="#888888", linewidth=0.5)

    ax.set_yscale("log")
    ax.set_xticks(x)
    ax.set_xticklabels([f"{q}\n{QUERY_LABELS[q]}" for q in queries])
    ax.set_ylabel("Avg query time (ms, log scale)")
    ax.set_title(f"AoS vs SoA query time at each thread count\n{DATASET_LABEL}")
    ax.legend(fontsize=8, ncol=2)
    ax.grid(axis="y", linestyle="--", alpha=0.4)

    save(fig, os.path.join(out_dir, "scaling_soa_vs_aos.png"))


# ── entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Plot strong scaling analysis")
    parser.add_argument("--results", default="results/benchmarks/scaling/",
                        help="Directory with benchmark CSVs")
    parser.add_argument("--output",  default="results/plots/scaling/",
                        help="Output directory for PNGs")
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
