#!/usr/bin/env python3
"""Bar chart: TPC-H SF=1 single-threaded wall-clock time per query, both engines."""

import re
from pathlib import Path
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "clean-results" / "baseline" / "tpch_sf1_3161190.out"
OUT = ROOT / "figures" / "sf1_runtimes.pdf"

QUERIES = ["q1", "q3", "q6", "q9", "q18"]
ENGINES = ["hyper", "vectorwise"]

ROW_RE = re.compile(r"^\s*(q\d+)\s+(hyper|vectorwise)\s*,")

def parse(path):
    times = {}
    for line in path.read_text().splitlines():
        m = ROW_RE.match(line)
        if not m:
            continue
        query, engine = m.group(1), m.group(2)
        fields = [f.strip() for f in line.split(",")]
        times[(query, engine)] = float(fields[1])
    return times

def main():
    times = parse(SRC)
    typer = [times[(q, "hyper")] for q in QUERIES]
    tw = [times[(q, "vectorwise")] for q in QUERIES]

    x = list(range(len(QUERIES)))
    width = 0.38

    fig, ax = plt.subplots(figsize=(7, 3.2))
    bars1 = ax.bar([i - width/2 for i in x], typer, width, label="Typer (compiling)")
    bars2 = ax.bar([i + width/2 for i in x], tw,    width, label="Tectorwise (vectorizing)")

    for bars in (bars1, bars2):
        for bar in bars:
            ax.annotate(f"{bar.get_height():.1f}",
                        xy=(bar.get_x() + bar.get_width()/2, bar.get_height()),
                        xytext=(0, 2), textcoords="offset points",
                        ha="center", va="bottom", fontsize=7)

    ax.set_xticks(x)
    ax.set_xticklabels([q.upper() for q in QUERIES])
    ax.set_xlabel("Query")
    ax.set_ylabel("Wall-clock time (ms)")
    ax.legend(loc="upper left", frameon=False)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)

    fig.tight_layout()
    OUT.parent.mkdir(exist_ok=True)
    fig.savefig(OUT)
    png = OUT.with_suffix(".png")
    fig.savefig(png, dpi=300)
    print(f"wrote {OUT.relative_to(ROOT)}")
    print(f"wrote {png.relative_to(ROOT)}")

if __name__ == "__main__":
    main()
