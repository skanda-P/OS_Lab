#!/usr/bin/env python3
"""
plot_hot.py: Plots the Cumulative Distribution Function (CDF) of memory page accesses (Part I-B).
Usage: python3 plot_hot.py <hist_file1.csv> [hist_file2.csv ...]
"""

import sys
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 plot_hot.py <hist_file1.csv> [hist_file2.csv ...]")
        sys.exit(1)

    plt.figure(figsize=(7, 6))

    for path in sys.argv[1:]:
        if not os.path.exists(path):
            print(f"Skipping missing file: {path}")
            continue

        xs, ys = [], []
        with open(path, "r") as f:
            lines = f.read().splitlines()
            for line in lines[1:]: # Skip header
                if not line.strip(): continue
                parts = line.split(",")
                xs.append(float(parts[0]))
                ys.append(float(parts[1]))

        label_name = os.path.basename(path).replace(".csv", "")
        plt.plot([0.0] + xs, [0.0] + ys, label=label_name, linewidth=1.8)

    # 45-degree diagonal represents uniform access across all pages
    plt.plot([0.0, 1.0], [0.0, 1.0], "k--", label="Uniform Access (Baseline)", linewidth=1.2)

    plt.xlabel("Fraction of Touched Pages (Hottest First)", fontsize=11)
    plt.ylabel("Fraction of Total Memory Accesses", fontsize=11)
    plt.title("Memory Access Skewness: Hot-Page Cumulative Distribution", fontsize=12, fontweight="bold")
    plt.grid(True, linestyle="--", alpha=0.5)
    plt.legend(loc="lower right")

    output_img = "hot_pages_plot.png"
    plt.savefig(output_img, dpi=200, bbox_inches="tight")
    print(f"Plot saved successfully as '{output_img}'.")

if __name__ == "__main__":
    main()
