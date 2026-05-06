#!/usr/bin/env python3
"""Line chart: Tectorwise per-query runtime vs vector size, normalized to vec=1024."""

import re
from pathlib import Path
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
SRC = sorted((ROOT / "clean-results" / "baseline").glob("tpch_sf1_vec_*.out"))[-1]
OUT = ROOT / "figures" / "vec_sweep.pdf"

QUERIES = ["q1", "q3", "q6", "q9", "q18"]
NORM_VEC = 1024

VEC_RE = re.compile(r"^### vectorSize=(\d+)")
ROW_RE = re.compile(r"^\s*(q\d+)\s+vectorwise\s*,")

def parse(path):
    times = {}  # (vec_size, query) -> time_ms
    current_vec = None
    for line in path.read_text().splitlines():
        m = VEC_RE.match(line)
        if m:
            current_vec = int(m.group(1))
            continue
        m = ROW_RE.match(line)
        if m and current_vec is not None:
            query = m.group(1)
            fields = [f.strip() for f in line.split(",")]
            times[(current_vec, query)] = float(fields[1])
    return times

def main():
    times = parse(SRC)
    vec_sizes = sorted({v for (v, _) in times.keys()})

    fig, ax = plt.subplots(figsize=(7, 3.6))
    for q in QUERIES:
        baseline = times[(NORM_VEC, q)]
        ratios = [times[(v, q)] / baseline for v in vec_sizes]
        ax.plot(vec_sizes, ratios, marker="o", markersize=4, label=q.upper())

    ax.axhline(1.0, color="black", linewidth=0.6, linestyle=":")
    ax.axvline(NORM_VEC, color="black", linewidth=0.8, linestyle="--", alpha=0.7)
    ax.set_xscale("log", base=2)
    ax.set_ylim(0.6, 5.5)
    ax.set_xlabel("Vector size (elements)")
    ax.set_ylabel(f"Runtime relative to vecSize={NORM_VEC}")
    ax.set_xticks(vec_sizes)
    ax.set_xticklabels(vec_sizes, rotation=45, fontsize=8)
    ax.legend(loc="lower center", bbox_to_anchor=(0.5, 1.02),
              ncol=len(QUERIES), frameon=False)
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
