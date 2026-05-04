#!/usr/bin/env python3
"""Generate runtime and memory-stall charts for selected new TPC-H queries."""

# Edit this list to choose which queries appear, and in what order.
QUERIES = ["q4", "q5", "q12", "q14", "q17"]

# Edit this list to choose which result files feed the charts. The first run is
# used for the runtime chart; all runs are used for the memory-stall chart.
RUNS = [
    ("1", "results/tpch_sf1_all.out"),
]

import argparse
import math
import re
from dataclasses import dataclass
from pathlib import Path

try:
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch
    from matplotlib.ticker import AutoMinorLocator, MaxNLocator
except ModuleNotFoundError as exc:
    raise SystemExit(
        "matplotlib is required to plot results; install it with "
        "`python3 -m pip install --user matplotlib` or load a Python "
        "environment that provides matplotlib."
    ) from exc

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RUNTIME_OUT = ROOT / "figures" / "sf1_new_query_runtimes.png"
DEFAULT_STALL_OUT = ROOT / "figures" / "new_query_memory_stalls.png"

ENGINES = [
    ("hyper", "Typer", "#F8766D"),
    ("vectorwise", "Tectorwise", "#00BFC4"),
]

STALL_COLORS = {
    "mem_stall": "#7570B3",
    "other": "#1B9E77",
}

FALLBACK_HEADER = [
    "name",
    "time",
    "CPUs",
    "IPC",
    "GHz",
    "Bandwidth",
    "cycles",
    "LLC-misses",
    "LLC-misses2",
    "l1-misses",
    "instr.",
    "br. misses",
    "all_rd",
    "br. misses",
    "stores",
    "loads",
    "mem_stall",
    "task-clock",
]

ROW_RE = re.compile(r"^\s*(q\d+)\s+(hyper|vectorwise)\s*,", re.IGNORECASE)
SF_RE = re.compile(r"sf(\d+)", re.IGNORECASE)


@dataclass(frozen=True)
class Record:
    query: str
    engine: str
    time_ms: float
    cycles: float
    mem_stall: float


def normalize_query(query):
    return query.strip().lower()


def split_fields(line):
    fields = [field.strip() for field in line.rstrip().split(",")]
    if fields and fields[-1] == "":
        fields.pop()
    return fields


def header_index(header, name):
    try:
        return header.index(name)
    except ValueError as exc:
        raise ValueError(f"missing column {name!r} in benchmark header") from exc


def parse_record(line, header):
    match = ROW_RE.match(line)
    if not match:
        return None

    fields = split_fields(line)
    query = normalize_query(match.group(1))
    engine = match.group(2).lower()
    return Record(
        query=query,
        engine=engine,
        time_ms=float(fields[header_index(header, "time")]),
        cycles=float(fields[header_index(header, "cycles")]),
        mem_stall=float(fields[header_index(header, "mem_stall")]),
    )


def parse(path):
    records = {}
    header = FALLBACK_HEADER

    for line in path.read_text().splitlines():
        fields = split_fields(line)
        if fields and fields[0] == "name":
            header = fields
            continue

        record = parse_record(line, header)
        if record is not None:
            records[(record.query, record.engine)] = record

    return records


def display_path(path):
    try:
        return path.relative_to(ROOT)
    except ValueError:
        return path


def resolve_path(path):
    path = Path(path)
    return path if path.is_absolute() else ROOT / path


def infer_scale_factor(path, index):
    match = SF_RE.search(path.name)
    if match:
        return match.group(1)
    return str(index + 1)


def configured_runs():
    return [(scale, resolve_path(path)) for scale, path in RUNS]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate new-query runtime and memory-stall charts."
    )
    parser.add_argument(
        "inputs",
        nargs="*",
        type=Path,
        help=(
            "Optional run_tpch .out files. If omitted, RUNS at the top of this "
            "file is used."
        ),
    )
    parser.add_argument(
        "--scale-factors",
        nargs="+",
        help="Labels for the input files, e.g. --scale-factors 1 3 10 30 100.",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_RUNTIME_OUT,
        help=f"Runtime PNG output path (default: {display_path(DEFAULT_RUNTIME_OUT)})",
    )
    parser.add_argument(
        "--stall-out",
        type=Path,
        default=DEFAULT_STALL_OUT,
        help=f"Memory-stall PNG output path (default: {display_path(DEFAULT_STALL_OUT)})",
    )
    parser.add_argument(
        "--queries",
        nargs="+",
        help="Optional subset/order; otherwise edit QUERIES at the top of this file.",
    )
    return parser.parse_args()


def choose_runs(args):
    if not args.inputs:
        return configured_runs()

    paths = [resolve_path(path) for path in args.inputs]
    if args.scale_factors:
        if len(args.scale_factors) != len(paths):
            raise SystemExit("--scale-factors must have one label per input file")
        labels = args.scale_factors
    else:
        labels = [infer_scale_factor(path, index) for index, path in enumerate(paths)]

    return list(zip(labels, paths))


def validate(records_by_run, queries):
    missing = []
    for scale_factor, records in records_by_run.items():
        for query in queries:
            for engine, _, _ in ENGINES:
                if (query, engine) not in records:
                    missing.append(f"SF{scale_factor} {query} {engine}")

    if missing:
        raise SystemExit(
            "missing runtime rows in input: "
            + ", ".join(missing)
            + ". Edit QUERIES/RUNS or point the script at runs containing these rows."
        )


def save_png(fig, out):
    png = resolve_path(out).with_suffix(".png")
    png.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(png, dpi=300)
    print(f"wrote {display_path(png)}")


def plot_runtimes(records, queries, out):
    x = list(range(len(queries)))
    width = 0.38

    fig_width = max(5, 1.1 + 0.75 * len(queries))
    fig, ax = plt.subplots(figsize=(fig_width, 3.2))
    for engine_idx, (engine, label, color) in enumerate(ENGINES):
        offset = (engine_idx - 0.5) * width
        values = [records[(query, engine)].time_ms for query in queries]
        bars = ax.bar(
            [i + offset for i in x],
            values,
            width,
            label=label,
            color=color,
        )
        for bar in bars:
            if math.isnan(bar.get_height()):
                continue
            ax.annotate(
                f"{bar.get_height():.1f}",
                xy=(bar.get_x() + bar.get_width() / 2, bar.get_height()),
                xytext=(0, 2),
                textcoords="offset points",
                ha="center",
                va="bottom",
                fontsize=7,
            )

    ax.set_xticks(x)
    ax.set_xticklabels(queries)
    ax.set_ylabel("Runtime [ms]")
    ax.set_axisbelow(True)
    ax.grid(axis="y", color="#e6e6e6", linewidth=0.7)
    ax.legend(loc="upper left", frameon=False)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)

    fig.tight_layout()
    save_png(fig, out)
    plt.close(fig)


def row_cycle_max(records_by_run, scale_factors, query):
    values = [
        records_by_run[scale_factor][(query, engine)].cycles
        for scale_factor in scale_factors
        for engine, _, _ in ENGINES
    ]
    return max(values)


def style_strip(ax):
    ax.set_facecolor("#d9d9d9")
    ax.set_xticks([])
    ax.set_yticks([])
    for spine in ax.spines.values():
        spine.set_visible(True)
        spine.set_color("#333333")
        spine.set_linewidth(1.0)


def plot_memory_stalls(records_by_run, scale_factors, queries, out):
    nrows = len(queries)
    ncols = len(ENGINES)
    fig_width = max(5.8, 2.3 + 0.5 * len(scale_factors) * ncols)
    fig_height = max(3.8, 1.1 + 0.72 * nrows)
    fig = plt.figure(figsize=(fig_width, fig_height))
    grid = fig.add_gridspec(
        nrows + 1,
        ncols + 1,
        width_ratios=[1] * ncols + [0.12],
        height_ratios=[0.30] + [1] * nrows,
        wspace=0.07,
        hspace=0.13,
    )

    for col_idx, (_, engine_label, _) in enumerate(ENGINES):
        strip_ax = fig.add_subplot(grid[0, col_idx])
        style_strip(strip_ax)
        strip_ax.text(0.5, 0.5, engine_label, ha="center", va="center", fontsize=9)

    corner_ax = fig.add_subplot(grid[0, ncols])
    corner_ax.axis("off")

    x = list(range(len(scale_factors)))
    bar_width = 0.78
    for row_idx, query in enumerate(queries):
        y_max = max(1, row_cycle_max(records_by_run, scale_factors, query) * 1.12)
        for col_idx, (engine, engine_label, _) in enumerate(ENGINES):
            ax = fig.add_subplot(grid[row_idx + 1, col_idx])
            mem_stall = [
                records_by_run[scale_factor][(query, engine)].mem_stall
                for scale_factor in scale_factors
            ]
            other = [
                max(
                    records_by_run[scale_factor][(query, engine)].cycles
                    - records_by_run[scale_factor][(query, engine)].mem_stall,
                    0,
                )
                for scale_factor in scale_factors
            ]

            ax.bar(
                x,
                other,
                width=bar_width,
                color=STALL_COLORS["other"],
                edgecolor="none",
                linewidth=0,
            )
            ax.bar(
                x,
                mem_stall,
                width=bar_width,
                bottom=other,
                color=STALL_COLORS["mem_stall"],
                edgecolor="none",
                linewidth=0,
            )

            ax.set_ylim(0, y_max)
            ax.set_axisbelow(True)
            ax.set_xlim(-0.5, len(scale_factors) - 0.5)
            ax.set_xticks(x)
            ax.yaxis.set_major_locator(MaxNLocator(nbins=4, min_n_ticks=4))
            ax.yaxis.set_minor_locator(AutoMinorLocator(2))
            ax.grid(axis="y", which="major", color="#d9d9d9", linewidth=0.45)
            ax.grid(axis="y", which="minor", color="#eeeeee", linewidth=0.30)
            ax.grid(axis="x", which="major", color="#d9d9d9", linewidth=0.45)
            ax.tick_params(axis="both", labelsize=8, colors="#4d4d4d")
            ax.tick_params(axis="y", which="minor", length=0)
            for spine in ax.spines.values():
                spine.set_visible(True)
                spine.set_color("#333333")
                spine.set_linewidth(0.9)

            if col_idx == 0:
                ax.tick_params(axis="y", labelsize=8)
            else:
                ax.tick_params(axis="y", left=False, labelleft=False)

            if row_idx == nrows - 1:
                ax.set_xticklabels(scale_factors)
            else:
                ax.tick_params(axis="x", bottom=False, labelbottom=False)

        strip_ax = fig.add_subplot(grid[row_idx + 1, ncols])
        style_strip(strip_ax)
        strip_ax.text(
            0.5,
            0.5,
            query,
            rotation=-90,
            ha="center",
            va="center",
            fontsize=9,
        )

    legend_handles = [
        Patch(color=STALL_COLORS["mem_stall"], label="Memory Stall Cycles"),
        Patch(color=STALL_COLORS["other"], label="Other Cycles"),
    ]
    fig.legend(
        handles=legend_handles,
        loc="upper center",
        ncol=2,
        frameon=False,
        fontsize=10,
        handlelength=1.4,
        handleheight=0.8,
        bbox_to_anchor=(0.5, 0.995),
    )
    fig.supylabel("Cycles / Tuple", x=0.03, fontsize=11)
    fig.supxlabel("Data Size (TPC-H Scale Factor)", y=0.03, fontsize=11)
    fig.subplots_adjust(left=0.12, right=0.92, bottom=0.13, top=0.88)
    save_png(fig, out)
    plt.close(fig)


def main():
    args = parse_args()
    runs = choose_runs(args)
    if not runs:
        raise SystemExit("no input runs configured")

    queries = [normalize_query(query) for query in (args.queries or QUERIES)]
    records_by_run = {}
    for scale_factor, path in runs:
        records = parse(path)
        if not records:
            raise SystemExit(f"no query runtime rows found in {display_path(path)}")
        records_by_run[str(scale_factor)] = records
        print(f"read SF{scale_factor}: {display_path(path)}")

    scale_factors = [str(scale_factor) for scale_factor, _ in runs]
    validate(records_by_run, queries)

    runtime_records = records_by_run[scale_factors[0]]
    plot_runtimes(runtime_records, queries, args.out)
    plot_memory_stalls(records_by_run, scale_factors, queries, args.stall_out)
    print(f"plotted {', '.join(queries)}")


if __name__ == "__main__":
    main()
