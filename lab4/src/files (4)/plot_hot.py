#!/usr/bin/env python3
"""Plot hot-page CDFs (Part I-B). Usage: python3 plot_hot.py hist/*.single.csv"""
import sys, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.figure(figsize=(6, 5))
for path in sys.argv[1:]:
    xs, ys = [], []
    for line in open(path).read().splitlines()[1:]:
        x, y = line.split(","); xs.append(float(x)); ys.append(float(y))
    plt.plot([0] + xs, [0] + ys, label=os.path.basename(path).split(".")[0])
plt.plot([0, 1], [0, 1], "k--", label="uniform access")
plt.xlabel("fraction of touched pages (hottest first)")
plt.ylabel("fraction of all memory accesses")
plt.title("Do a few pages receive most accesses?")
plt.grid(alpha=.3); plt.legend()
plt.savefig("hot_pages.png", dpi=150, bbox_inches="tight")
print("saved hot_pages.png")
