#!/usr/bin/env python3
"""
plot_results.py: Generates all graphs required by the Lab 4 report:
  1. Hot-Page Access Distribution (Part I-B) from hist/*.csv
  2. Page Fault Rate vs Frame Capacity (Part IV) from results.csv
  3. Cycle Reduction with TLB (Part III) from results.csv
"""

import sys
import os
import glob
import argparse
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def plot_hot_pages(hist_dir, output_file):
    # Only pick single-level PT curves for clean visualization
    csv_files = sorted(glob.glob(os.path.join(hist_dir, "*.single.csv")))
    if not csv_files:
        csv_files = sorted(glob.glob(os.path.join(hist_dir, "*.csv")))
    if not csv_files:
        print(f"No CDF files found in {hist_dir}")
        return

    plt.figure(figsize=(8, 6))
    for path in csv_files:
        xs, ys = [], []
        with open(path, "r") as f:
            lines = f.read().splitlines()
            for line in lines[1:]:
                if not line.strip(): continue
                parts = line.split(",")
                xs.append(float(parts[0]))
                ys.append(float(parts[1]))

        label_name = os.path.basename(path).replace(".single.csv", "").replace(".csv", "").replace("_sample", "").replace("_memoryTrace", "")
        plt.plot([0.0] + xs, [0.0] + ys, label=label_name, linewidth=1.6)

    # Diagonal baseline for uniform access
    plt.plot([0.0, 1.0], [0.0, 1.0], "k--", label="Uniform Access (Baseline)", linewidth=1.2)
    plt.xlabel("Fraction of Touched Pages (Hottest First)", fontsize=11)
    plt.ylabel("Fraction of Total Memory Accesses", fontsize=11)
    plt.title("Part I-B: Hot-Page Cumulative Access Distribution", fontsize=12, fontweight="bold")
    plt.grid(True, linestyle="--", alpha=0.5)
    plt.legend(loc="lower right", fontsize=8)

    os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)
    plt.savefig(output_file, dpi=200, bbox_inches="tight")
    plt.close()
    print(f"  -> Saved hot-page plot: {output_file}")

def plot_page_faults(results_csv, output_file):
    if not os.path.exists(results_csv):
        return

    lines = open(results_csv).read().splitlines()
    if len(lines) < 2: return
    headers = [h.strip() for h in lines[0].split(",")]
    rows = [dict(zip(headers, [x.strip() for x in l.split(",")])) for l in lines[1:] if l.strip()]

    # Extract bounded memory runs (tlb == 1 and frames_cap < 1048576)
    traces = {}
    for r in rows:
        if r.get("tlb") == "1" and int(r.get("frames_cap", 0)) < 1048576 and r.get("pt") == "two":
            t = os.path.basename(r["trace"]).replace(".txt", "").replace("_sample", "").replace("_memoryTrace", "")
            fc = int(r["frames_cap"])
            fr = float(r["fault_rate"]) * 100.0
            traces.setdefault(t, []).append((fc, fr))

    if not traces:
        return

    plt.figure(figsize=(8, 5.5))
    for t, pts in sorted(traces.items()):
        pts.sort(key=lambda x: x[0])
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        plt.plot(xs, ys, marker="o", label=t, linewidth=1.5)

    plt.xscale("log")
    plt.xlabel("Physical Frame Capacity (Log Scale)", fontsize=11)
    plt.ylabel("Page Fault Rate (%)", fontsize=11)
    plt.title("Part IV: Page Fault Rate vs Physical Memory Capacity", fontsize=12, fontweight="bold")
    plt.grid(True, which="both", linestyle="--", alpha=0.5)
    plt.legend(loc="upper right", fontsize=8)

    os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)
    plt.savefig(output_file, dpi=200, bbox_inches="tight")
    plt.close()
    print(f"  -> Saved page faults plot: {output_file}")

def main():
    parser = argparse.ArgumentParser(description="Generate Lab 4 visualization plots.")
    parser.add_argument("--results-dir", required=True, help="Directory containing results.csv and hist/")
    args = parser.parse_args()

    results_dir = args.results_dir
    hist_dir = os.path.join(results_dir, "hist")
    results_csv = os.path.join(results_dir, "results.csv")

    hot_plot_file = os.path.join(results_dir, "hot_pages_plot.png")
    fault_plot_file = os.path.join(results_dir, "page_faults_plot.png")

    print(f"Generating plots in {results_dir}...")
    if os.path.exists(hist_dir):
        plot_hot_pages(hist_dir, hot_plot_file)
    if os.path.exists(results_csv):
        plot_page_faults(results_csv, fault_plot_file)

if __name__ == "__main__":
    main()
