#!/usr/bin/env python3
"""Heatmap: SIMD ablation effect relative to scalar baseline."""

import argparse
import re
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.colors import TwoSlopeNorm


ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "clean-results" / "baseline"
OUT_DIR = ROOT / "figures"

QUERIES = ["q1", "q3", "q6", "q9", "q18"]
CONFIGS = [
    ("scalar-baseline", "0. Scalar"),
    ("hash-only", "1. hash-only"),
    ("join-only", "2. join-only"),
    ("sel-only", "3. sel-only"),
    ("proj-only", "4. proj-only"),
    ("hash+join", "5. hash+join"),
    ("all-simd-on", "6. all-simd-on"),
]

SECTION_RE = re.compile(r"^### ([^(]+)")
ROW_RE = re.compile(r"^\s*(q\d+)\s+vectorwise\s*,")
SF_RE = re.compile(r"tpch_sf(\d+)_simd_.*\.out$")


def latest_result(sf):
    matches = sorted(RESULTS.glob(f"tpch_sf{sf}_simd_*.out"))
    if not matches:
        raise SystemExit(f"no SIMD result file found for SF{sf}")
    return matches[-1]


def parse(path, metric):
    values = {}  # (config, query) -> metric value
    current_config = None
    metric_idx = None

    for line in path.read_text().splitlines():
        section = SECTION_RE.match(line)
        if section:
            current_config = section.group(1).strip()
            continue

        fields = [field.strip() for field in line.split(",")]
        if fields and fields[0] == "name":
            try:
                metric_idx = fields.index(metric)
            except ValueError:
                available = ", ".join(fields[1:])
                raise SystemExit(
                    f"metric '{metric}' not found in {path.name}; available: {available}"
                )
            continue

        row = ROW_RE.match(line)
        if row and current_config is not None and metric_idx is not None:
            query = row.group(1)
            if query in QUERIES:
                values[(current_config, query)] = float(fields[metric_idx])

    return values


def sf_from_path(path):
    match = SF_RE.match(path.name)
    if not match:
        raise SystemExit(f"could not infer scale factor from {path.name}")
    return int(match.group(1))


def format_pct(value):
    if abs(value) < 0.05:
        return "0.0"
    return f"{value:+.1f}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--metric",
        default="cycles",
        help="CSV metric column to plot; default reproduces the provided table",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=OUT_DIR / "simd_ablation_heatmap.pdf",
        help="output PDF path",
    )
    parser.add_argument(
        "files",
        nargs="*",
        type=Path,
        help="optional SIMD result files; defaults to latest SF1 and SF10 files",
    )
    args = parser.parse_args()
    out = args.out if args.out.is_absolute() else ROOT / args.out

    sources = args.files or [latest_result(1), latest_result(10)]
    sources = sorted(sources, key=sf_from_path)

    parsed = [(sf_from_path(path), path, parse(path, args.metric)) for path in sources]
    columns = [(sf, query) for sf, _, _ in parsed for query in QUERIES]

    data = []
    for config, _ in CONFIGS:
        row = []
        for sf, _, values in parsed:
            for query in QUERIES:
                baseline = values[("scalar-baseline", query)]
                value = values[(config, query)]
                row.append((value / baseline - 1.0) * 100.0)
        data.append(row)

    max_abs = max(abs(value) for row in data for value in row)
    norm = TwoSlopeNorm(vmin=-max_abs, vcenter=0.0, vmax=max_abs)

    fig_width = max(7.0, 0.58 * len(columns) + 2.1)
    fig, ax = plt.subplots(figsize=(fig_width, 4.2))
    image = ax.imshow(data, cmap="RdBu_r", norm=norm, aspect="auto")

    ax.set_xticks(range(len(columns)))
    ax.set_xticklabels(
        [f"SF{sf}\n{query.upper()}" for sf, query in columns],
        fontsize=8,
    )
    ax.set_yticks(range(len(CONFIGS)))
    ax.set_yticklabels([label for _, label in CONFIGS], fontsize=9)
    ax.set_title(f"SIMD ablation: {args.metric} change vs scalar baseline")

    for x in range(len(columns)):
        ax.axvline(x - 0.5, color="white", linewidth=0.8)
    for y in range(len(CONFIGS)):
        ax.axhline(y - 0.5, color="white", linewidth=0.8)

    for y, row in enumerate(data):
        for x, value in enumerate(row):
            color = "white" if abs(value) > max_abs * 0.55 else "black"
            ax.text(x, y, format_pct(value), ha="center", va="center",
                    fontsize=7.5, color=color)

    cbar = fig.colorbar(image, ax=ax, fraction=0.032, pad=0.02)
    cbar.set_label("% change vs scalar baseline")

    ax.tick_params(length=0)
    for spine in ax.spines.values():
        spine.set_visible(False)

    fig.tight_layout()
    out.parent.mkdir(exist_ok=True)
    fig.savefig(out)
    png = out.with_suffix(".png")
    fig.savefig(png, dpi=300)

    print(f"wrote {out.relative_to(ROOT)}")
    print(f"wrote {png.relative_to(ROOT)}")
    print("sources:")
    for _, path, _ in parsed:
        print(f"  {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
