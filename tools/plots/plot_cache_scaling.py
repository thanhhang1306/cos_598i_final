#!/usr/bin/env python3
"""Line chart: per-tuple cycles vs scale factor, per query and engine."""

import re
from pathlib import Path
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "figures" / "cache_scaling.pdf"

# Scale factor -> .out file. Only includes SFs whose .out exists; missing
# entries are silently skipped so the script works with partial data.
SOURCES = {
    1:  "tpch_sf1_3161190.out",
    3:  "tpch_sf3_3170615.out",
    10: "tpch_sf10_3161193.out",
    30: None,  # filled in below if a tpch_sf30_*.out exists
}

# Auto-discover the latest tpch_sf30_*.out (so we don't hardcode jobid)
sf30_candidates = sorted((ROOT / "clean-results").glob("tpch_sf30_*.out"))
if sf30_candidates:
    SOURCES[30] = sf30_candidates[-1].name

QUERIES = ["q1", "q3", "q6", "q9", "q18"]
ENGINES = [("hyper", "Typer"), ("vectorwise", "Tectorwise")]

ROW_RE = re.compile(r"^\s*(q\d+)\s+(hyper|vectorwise)\s*,")

def parse_cycles(path):
    cycles = {}
    for line in path.read_text().splitlines():
        m = ROW_RE.match(line)
        if not m:
            continue
        q, e = m.group(1), m.group(2)
        fields = [f.strip() for f in line.split(",")]
        cycles[(q, e)] = float(fields[6])
    return cycles

def main():
    sf_data = {}  # sf -> {(query, engine): cycles}
    for sf, fname in SOURCES.items():
        if not fname:
            continue
        path = ROOT / "clean-results" / fname
        if not path.exists():
            continue
        sf_data[sf] = parse_cycles(path)

    sfs = sorted(sf_data.keys())
    if not sfs:
        raise SystemExit("no clean-results data found")

    fig, axes = plt.subplots(1, 2, figsize=(10, 3.6), sharey=True)
    for ax, (engine, label) in zip(axes, ENGINES):
        for q in QUERIES:
            ys = [sf_data[sf].get((q, engine)) for sf in sfs]
            ax.plot(sfs, ys, marker="o", markersize=5, label=q.upper())
        ax.set_xscale("log")
        ax.set_xlabel("Scale factor")
        ax.set_xticks(sfs)
        ax.set_xticklabels(sfs)
        ax.set_title(label)
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)

    axes[0].set_ylabel("Cycles per tuple")
    axes[1].legend(loc="center left", bbox_to_anchor=(1.02, 0.5),
                   frameon=False, title="Query")

    fig.tight_layout()
    OUT.parent.mkdir(exist_ok=True)
    fig.savefig(OUT)
    png = OUT.with_suffix(".png")
    fig.savefig(png, dpi=300)
    print(f"wrote {OUT.relative_to(ROOT)}")
    print(f"wrote {png.relative_to(ROOT)}")
    print(f"scale factors plotted: {sfs}")

if __name__ == "__main__":
    main()
