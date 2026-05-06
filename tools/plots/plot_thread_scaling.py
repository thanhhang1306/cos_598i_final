#!/usr/bin/env python3
"""Two-panel line chart: per-query speedup vs thread count."""

import re
from pathlib import Path

import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
SRC = sorted((ROOT / "clean-results" / "baseline").glob("tpch_sf10_mt_*.out"))[-1]
OUT = ROOT / "figures" / "thread_scaling.pdf"

QUERIES = ["q1", "q3", "q6", "q9", "q18"]
ENGINES = [("hyper", "Typer"), ("vectorwise", "Tectorwise")]

THREAD_RE = re.compile(r"^### nrThreads=(\d+)")
ROW_RE = re.compile(r"^\s*(q\d+)\s+(hyper|vectorwise)\s*,")


def parse(path):
    times = {}  # (threads, query, engine) -> time_ms
    current_threads = None
    for line in path.read_text().splitlines():
        m = THREAD_RE.match(line)
        if m:
            current_threads = int(m.group(1))
            continue

        m = ROW_RE.match(line)
        if m and current_threads is not None:
            query, engine = m.group(1), m.group(2)
            fields = [f.strip() for f in line.split(",")]
            times[(current_threads, query, engine)] = float(fields[1])
    return times


def main():
    times = parse(SRC)
    threads = sorted({t for (t, _, _) in times.keys()})

    if not threads:
        raise SystemExit(f"no threading data found in {SRC}")
    if 1 not in threads:
        raise SystemExit(f"missing 1-thread baseline in {SRC}")

    fig, axes = plt.subplots(1, 2, figsize=(10, 3.6), sharey=True)

    for ax, (engine, label) in zip(axes, ENGINES):
        ax.plot(
            threads,
            threads,
            color="black",
            linewidth=0.8,
            linestyle=":",
            label="Ideal",
        )

        for q in QUERIES:
            baseline = times[(1, q, engine)]
            speedups = [baseline / times[(t, q, engine)] for t in threads]
            ax.plot(threads, speedups, marker="o", markersize=5, label=q.upper())

        # ax.set_xscale("log", base=2)
        ax.set_xlabel("Threads")
        ax.set_xticks(threads)
        ax.set_xticklabels(threads)
        ax.set_title(label)
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)

    axes[0].set_ylabel("Speedup vs 1 thread")
    axes[1].legend(loc="center left", bbox_to_anchor=(1.02, 0.5),
                   frameon=False, title="Query")

    fig.tight_layout()
    OUT.parent.mkdir(exist_ok=True)
    fig.savefig(OUT)
    png = OUT.with_suffix(".png")
    fig.savefig(png, dpi=300)
    print(f"wrote {OUT.relative_to(ROOT)}")
    print(f"wrote {png.relative_to(ROOT)}")
    print(f"threads plotted: {threads}")
    print(f"source: {SRC.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
