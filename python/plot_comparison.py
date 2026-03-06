#!/usr/bin/env python3
"""
plot_comparison.py — benchmark visualisation for all three phases.

Reads CSV files produced by taxi_bench_full and generates:
  1. query_times.png    — avg ms per query, per phase, with stddev error bars
  2. speedup.png        — Phase1/PhaseX speedup ratio per query (>1 = faster)
  3. load_time.png      — serial vs parallel load time bar chart

Usage (2 phases — Phase 1 vs Phase 2):
    python3 plot_comparison.py \\
        --phase1 results/bench_phase1.csv \\
        --phase2 results/bench_phase2.csv \\
        --output results/

Usage (all 3 phases):
    python3 plot_comparison.py \\
        --phase1 results/bench_phase1.csv \\
        --phase2 results/bench_phase2.csv \\
        --phase3 results/bench_phase3.csv \\
        --output results/ --label "6.4M rows"
"""

import argparse
import os
import sys

import pandas as pd
import matplotlib
matplotlib.use("Agg")          # headless — no display required
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np


# ── colour palette ────────────────────────────────────────────────────────────
C1  = "#4C72B0"   # Phase 1 serial  (blue)
C2  = "#DD8452"   # Phase 2 parallel (orange)
C3  = "#55A868"   # Phase 3 SoA      (green)
ERR = "#333333"   # error bar colour


def load_csv(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)
    df.columns = df.columns.str.strip()
    return df


def save(fig: plt.Figure, path: str) -> None:
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {path}")


# ── Plot 1: query times bar chart ─────────────────────────────────────────────

def plot_query_times(phases: list, label: str, out_dir: str) -> None:
    """
    phases: list of (name, colour, DataFrame) tuples.
    Produces a grouped bar chart with one cluster per query.
    """
    # Keep only query rows, use the first phase's query list as canonical order
    filtered = [(name, col, df[df["query"] != "LOAD"].copy())
                for name, col, df in phases]

    queries = filtered[0][2]["query"].tolist()
    x       = np.arange(len(queries))
    n       = len(filtered)
    w       = 0.8 / n          # bar width so all bars fit in each cluster
    offsets = np.linspace(-(n - 1) * w / 2, (n - 1) * w / 2, n)

    fig, ax = plt.subplots(figsize=(max(10, 2 * len(queries)), 5))

    for (name, col, qdf), offset in zip(filtered, offsets):
        ax.bar(x + offset, qdf["avg_ms"], w,
               yerr=qdf["stddev_ms"], label=name, color=col,
               capsize=4, error_kw={"ecolor": ERR})

    ax.set_xlabel("Query")
    ax.set_ylabel("Average time (ms)")
    ax.set_title(f"Query performance — all phases\n{label}")
    ax.set_xticks(x)
    ax.set_xticklabels(queries)
    ax.legend()
    ax.yaxis.set_minor_locator(ticker.AutoMinorLocator())
    ax.grid(axis="y", linestyle="--", alpha=0.5)

    save(fig, os.path.join(out_dir, "query_times.png"))


# ── Plot 2: speedup ratio (baseline = Phase 1) ────────────────────────────────

def plot_speedup(phases: list, label: str, out_dir: str) -> None:
    """
    Computes Phase1/PhaseX speedup for every non-Phase-1 phase.
    One subplot per comparison.
    """
    baseline_name, _, df1 = phases[0]
    comparisons = phases[1:]   # everything except Phase 1

    if not comparisons:
        print("  (only one phase — skipping speedup.png)")
        return

    ncols = len(comparisons)
    fig, axes = plt.subplots(1, ncols, figsize=(7 * ncols, 4),
                             sharey=False, squeeze=False)

    _exclude = {"LOAD", "TOTAL_PHASE"}
    q1 = df1[~df1["query"].isin(_exclude)].set_index("query")

    for ax, (cmp_name, cmp_col, df2) in zip(axes[0], comparisons):
        q2     = df2[~df2["query"].isin(_exclude)].set_index("query")
        common = q1.index.intersection(q2.index)
        with np.errstate(divide="ignore", invalid="ignore"):
            speedup = q1.loc[common, "avg_ms"] / q2.loc[common, "avg_ms"]
        speedup = speedup.replace([np.inf, -np.inf], np.nan)

        colours = [cmp_col if s >= 1.0 else "#CC4444" for s in speedup]
        bars    = ax.bar(common, speedup, color=colours)
        ax.axhline(1.0, color="black", linewidth=0.8, linestyle="--",
                   label="No speedup (1×)")

        for bar, val in zip(bars, speedup):
            ax.text(bar.get_x() + bar.get_width() / 2,
                    bar.get_height() + 0.02,
                    f"{val:.2f}×",
                    ha="center", va="bottom", fontsize=9)

        ax.set_xlabel("Query")
        ax.set_ylabel(f"Speedup ({baseline_name} / {cmp_name})")
        ax.set_title(f"Speedup: {cmp_name} vs {baseline_name}\n{label}")
        ax.legend()
        ax.grid(axis="y", linestyle="--", alpha=0.5)

    fig.tight_layout()
    save(fig, os.path.join(out_dir, "speedup.png"))


# ── Plot 3: load time ─────────────────────────────────────────────────────────

def plot_load_time(phases: list, label: str, out_dir: str) -> None:
    rows = []
    for name, col, df in phases:
        load_row = df[df["query"] == "LOAD"]
        if load_row.empty:
            continue
        thr = int(load_row["threads"].iloc[0])
        rows.append({
            "label":  f"{name}\n({thr} thread{'s' if thr > 1 else ''})",
            "avg_ms": float(load_row["avg_ms"].iloc[0]),
            "std_ms": float(load_row["stddev_ms"].iloc[0]),
            "colour": col,
        })

    if not rows:
        print("  (no LOAD rows found — skipping load_time.png)")
        return

    fig, ax = plt.subplots(figsize=(max(6, 2 * len(rows)), 4))
    labels  = [r["label"]  for r in rows]
    avgs    = [r["avg_ms"] for r in rows]
    stds    = [r["std_ms"] for r in rows]
    colours = [r["colour"] for r in rows]

    bars = ax.bar(labels, avgs, yerr=stds, color=colours, capsize=6,
                  error_kw={"ecolor": ERR}, width=0.4)

    for bar, val in zip(bars, avgs):
        ax.text(bar.get_x() + bar.get_width() / 2,
                bar.get_height() + max(stds) * 0.05 if stds else 0,
                f"{val:.1f} ms",
                ha="center", va="bottom", fontsize=10)

    # Annotate speedup relative to first bar (Phase 1 baseline)
    if len(avgs) > 1 and avgs[0] > 0:
        for i in range(1, len(avgs)):
            sp = avgs[0] / avgs[i] if avgs[i] > 0 else float("nan")
            ax.annotate(f"({sp:.2f}× vs Phase 1)",
                        xy=(i, avgs[i]),
                        xytext=(0, -22), textcoords="offset points",
                        ha="center", fontsize=8, color="#555555")

    ax.set_ylabel("Average load time (ms)")
    ax.set_title(f"CSV load time — all phases\n{label}")
    ax.grid(axis="y", linestyle="--", alpha=0.5)

    save(fig, os.path.join(out_dir, "load_time.png"))


# ── entry point ───────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Plot Phase 1 / Phase 2 / Phase 3 benchmarks")
    parser.add_argument("--phase1",  required=True, help="Phase 1 CSV file")
    parser.add_argument("--phase2",  required=True, help="Phase 2 CSV file")
    parser.add_argument("--phase3",  default="",    help="Phase 3 CSV file (optional)")
    parser.add_argument("--output",  default="results/", help="Output directory")
    parser.add_argument("--label",   default="",    help="Dataset label for titles")
    args = parser.parse_args()

    required = [args.phase1, args.phase2]
    if args.phase3:
        required.append(args.phase3)

    for p in required:
        if not os.path.isfile(p):
            print(f"ERROR: file not found: {p}", file=sys.stderr)
            sys.exit(1)

    os.makedirs(args.output, exist_ok=True)

    df1 = load_csv(args.phase1)
    df2 = load_csv(args.phase2)

    phases = [
        ("Phase 1 (serial)",   C1, df1),
        ("Phase 2 (parallel)", C2, df2),
    ]

    if args.phase3:
        df3 = load_csv(args.phase3)
        phases.append(("Phase 3 (SoA)", C3, df3))
        print(f"Loaded Phase 3: {len(df3)} rows from {args.phase3}")

    label = args.label or "  |  ".join(
        f"P{i+1}: {p}" for i, p in enumerate([args.phase1, args.phase2]
                                              + ([args.phase3] if args.phase3 else [])))

    print(f"Loaded Phase 1: {len(df1)} rows from {args.phase1}")
    print(f"Loaded Phase 2: {len(df2)} rows from {args.phase2}")
    print(f"Writing charts to: {args.output}")

    plot_query_times(phases, label, args.output)
    plot_speedup(phases, label, args.output)
    plot_load_time(phases, label, args.output)

    print("Done.")


if __name__ == "__main__":
    main()
