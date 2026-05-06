#!/usr/bin/env python3
"""Line chart: Tectorwise TPC-H runtime vs. vector size.

The input should be a run_tpch log from a vectorSize sweep, such as the output
from slurm/run_tpch_sf1_vec.sh. Times are normalized by each query's 1K
vector-size runtime.
"""

import argparse
import math
import re
from dataclasses import dataclass
from pathlib import Path

try:
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    from matplotlib.ticker import AutoMinorLocator, MaxNLocator, NullFormatter
except ModuleNotFoundError as exc:
    raise SystemExit(
        "matplotlib is required to plot results; activate the project venv "
        "or use `./.venv/bin/python` to run this script."
    ) from exc


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INPUT = ROOT / "results" / "tpch_sf1_new_vec.out"
DEFAULT_OUT = ROOT / "figures" / "ext_sf1_vector_sizes.png"

DEFAULT_QUERIES = ["q4", "q5", "q12", "q14", "q17"]
BASELINE_VECTOR_SIZE = 1024
DEFAULT_YMAX = 5.15

ROW_RE = re.compile(r"^\s*(q\d+)\s+(hyper|vectorwise)\s*,", re.IGNORECASE)
VECTOR_SIZE_RE = re.compile(r"###\s*vectorSize\s*=\s*(\d+)", re.IGNORECASE)
QUERY_RE = re.compile(r"^q?(\d+)(?:[vh])?$", re.IGNORECASE)

PAPER_COLORS = [
    "#F8766D",
    "#A3A500",
    "#00BF7D",
    "#00B0F6",
    "#E76BF3",
    "#E68613",
    "#619CFF",
    "#FF61C3",
]
PAPER_MARKERS = ["o", "^", "s", "+", "P", "D", "v", "x"]
PANEL_BG = "#FFFFFF"
GRID_MAJOR = "#D9D9D9"
GRID_MINOR = "#E6E6E6"


@dataclass(frozen=True)
class Record:
    query: str
    vector_size: int
    time_ms: float


def normalize_query(query):
    query = query.strip().lower()
    match = QUERY_RE.match(query)
    if not match:
        raise ValueError(f"invalid query {query!r}; use q4, 4, or 4v")
    return f"q{int(match.group(1))}"


def query_sort_key(query):
    return int(query[1:])


def display_query(query):
    return query.lower()


def split_fields(line):
    fields = [field.strip() for field in line.rstrip().split(",")]
    if fields and fields[-1] == "":
        fields.pop()
    return fields


def parse_time(line):
    fields = split_fields(line)
    if len(fields) < 2:
        raise ValueError(f"could not parse benchmark row: {line}")
    return float(fields[1])


def parse(path, fallback_vector_size):
    records = {}
    current_vector_size = None

    for line in path.read_text().splitlines():
        vector_match = VECTOR_SIZE_RE.search(line)
        if vector_match:
            current_vector_size = int(vector_match.group(1))
            continue

        row_match = ROW_RE.match(line)
        if not row_match:
            continue

        query = normalize_query(row_match.group(1))
        engine = row_match.group(2).lower()
        if engine != "vectorwise":
            continue

        vector_size = current_vector_size
        if vector_size is None:
            vector_size = fallback_vector_size
        if vector_size is None:
            raise SystemExit(
                f"{display_path(path)} has query rows before any vectorSize "
                "marker. Re-run with section headers, or pass "
                "--fallback-vector-size."
            )

        records[(query, vector_size)] = Record(
            query=query,
            vector_size=vector_size,
            time_ms=parse_time(line),
        )

    return records


def display_path(path):
    try:
        return path.relative_to(ROOT)
    except ValueError:
        return path


def resolve_path(path):
    path = Path(path)
    return path if path.is_absolute() else ROOT / path


def format_vector_size(value):
    if value >= 1024 * 1024 and value % (1024 * 1024) == 0:
        return f"{value // (1024 * 1024)}M"
    if value >= 1024 and value % 1024 == 0:
        return f"{value // 1024}K"
    return str(value)


def paper_major_ticks(max_vector_size):
    candidates = [1, 16, 256, 1024, 4096, 65536, 1048576]
    ticks = [tick for tick in candidates if tick <= max_vector_size]
    if max_vector_size not in ticks:
        ticks.append(max_vector_size)
    return ticks


def power_of_two_ticks(min_vector_size, max_vector_size):
    if min_vector_size <= 0:
        return []
    start = int(math.floor(math.log2(min_vector_size)))
    end = int(math.ceil(math.log2(max_vector_size)))
    ticks = [2**power for power in range(start, end + 1)]
    return [tick for tick in ticks if min_vector_size <= tick <= max_vector_size]


def validate(records, queries, baseline_vector_size):
    missing = [
        query
        for query in queries
        if (query, baseline_vector_size) not in records
    ]
    if missing:
        raise SystemExit(
            "missing 1K baseline rows for: "
            + ", ".join(missing)
            + f". This plot normalizes by vectorSize={baseline_vector_size}."
        )

    underspecified = [
        query
        for query in queries
        if len({size for q, size in records if q == query}) < 2
    ]
    if underspecified:
        raise SystemExit(
            "need at least two vector sizes for: "
            + ", ".join(underspecified)
            + ". Your one-off vectorSize=1024 run is only the baseline; "
            "run a vector-size sweep to draw the line chart."
        )

def plot(records, queries, out, baseline_vector_size, ymax):
    sizes = sorted({size for query, size in records if query in queries})
    min_size = min(sizes)
    max_size = max(sizes)

    fig, ax = plt.subplots(figsize=(7.15, 3.0))
    fig.patch.set_facecolor("white")
    ax.set_facecolor(PANEL_BG)

    line_handles = []
    max_ratio = 1.0
    for idx, query in enumerate(queries):
        baseline = records[(query, baseline_vector_size)].time_ms
        query_sizes = sorted(size for q, size in records if q == query)
        ratios = [records[(query, size)].time_ms / baseline for size in query_sizes]
        max_ratio = max(max_ratio, max(ratios))

        color = PAPER_COLORS[idx % len(PAPER_COLORS)]
        marker = PAPER_MARKERS[idx % len(PAPER_MARKERS)]
        line, = ax.plot(
            query_sizes,
            ratios,
            color=color,
            linewidth=2.0,
            marker=marker,
            markersize=6.0,
            markeredgewidth=1.8,
            markerfacecolor=color,
            markeredgecolor=color,
            label=display_query(query),
            zorder=3,
        )
        line_handles.append(line)

    ax.set_xscale("log", base=2)
    ax.set_xlim(min_size / 1.35, max_size * 1.35)

    top = ymax if ymax is not None else max(4.15, max_ratio * 1.08)
    ax.set_ylim(0.72, top)

    major_ticks = paper_major_ticks(max_size)
    ax.set_xticks(major_ticks)
    major_labels = [format_vector_size(tick) for tick in major_ticks]
    if major_ticks and major_ticks[-1] == max_size:
        major_labels[-1] = "Max."
    ax.set_xticklabels(major_labels)

    minor_ticks = [
        tick for tick in power_of_two_ticks(min_size, max_size)
        if tick not in set(major_ticks)
    ]
    ax.set_xticks(minor_ticks, minor=True)
    ax.tick_params(axis="x", which="minor", length=0)
    ax.xaxis.set_minor_formatter(NullFormatter())

    ax.yaxis.set_major_locator(MaxNLocator(nbins=6))
    ax.yaxis.set_minor_locator(AutoMinorLocator(2))

    ax.grid(True, which="major", color=GRID_MAJOR, linewidth=1.6)
    ax.grid(True, which="minor", color=GRID_MINOR, linewidth=1.0)
    ax.set_axisbelow(True)

    ax.set_xlabel("Vector Size (Tuples)", fontsize=17)
    ax.set_ylabel("Time Relative to 1K", fontsize=17)
    ax.tick_params(axis="both", labelsize=13, width=2.0, length=5, colors="#4D4D4D")

    for spine in ax.spines.values():
        spine.set_color("#333333")
        spine.set_linewidth(1.8)

    query_handle = Line2D([], [], color="none", label="Query")
    handles = [query_handle] + line_handles
    legend = ax.legend(
        handles=handles,
        loc="upper center",
        bbox_to_anchor=(0.58, 0.91),
        ncol=len(handles),
        frameon=False,
        columnspacing=0.8,
        handlelength=1.4,
        handletextpad=0.4,
        borderpad=0.25,
        fontsize=12,
    )
    legend.get_texts()[0].set_fontsize(16)
    legend.get_texts()[0].set_color("black")

    fig.tight_layout(pad=0.35)
    png = out.with_suffix(".png")
    png.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(png, dpi=300)
    return png


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Plot Tectorwise vector-size sensitivity from run_tpch sweep logs, "
            "normalized by the 1K vector-size runtime."
        )
    )
    parser.add_argument(
        "inputs",
        nargs="*",
        type=Path,
        help=(
            "run_tpch .out file(s) from a vectorSize sweep. If omitted, "
            f"{display_path(DEFAULT_INPUT)} is used."
        ),
    )
    parser.add_argument(
        "--queries",
        nargs="+",
        default=DEFAULT_QUERIES,
        help=(
            "Queries to plot, in legend/order, e.g. --queries q4 q5 q12. "
            "You may also write 4 or 4v."
        ),
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_OUT,
        help=f"PNG output path (default: {display_path(DEFAULT_OUT)})",
    )
    parser.add_argument(
        "--baseline-vector-size",
        type=int,
        default=BASELINE_VECTOR_SIZE,
        help="Vector size used as the normalization baseline (default: 1024).",
    )
    parser.add_argument(
        "--fallback-vector-size",
        type=int,
        help=(
            "Use this vector size for logs that have query rows but no "
            "'### vectorSize=...' section marker."
        ),
    )
    parser.add_argument(
        "--ymax",
        type=float,
        default=DEFAULT_YMAX,
        help=f"Fixed y-axis maximum (default: {DEFAULT_YMAX}).",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    inputs = args.inputs or [DEFAULT_INPUT]
    inputs = [resolve_path(path) for path in inputs]
    out = resolve_path(args.out).with_suffix(".png")
    queries = [normalize_query(query) for query in args.queries]

    records = {}
    for path in inputs:
        if not path.exists():
            raise SystemExit(f"missing input file: {display_path(path)}")
        records.update(parse(path, args.fallback_vector_size))

    if not records:
        raise SystemExit("no Tectorwise query rows found in input log(s)")

    if not queries:
        queries = sorted({query for query, _ in records}, key=query_sort_key)

    validate(records, queries, args.baseline_vector_size)
    png = plot(records, queries, out, args.baseline_vector_size, args.ymax)

    print(f"read {', '.join(str(display_path(path)) for path in inputs)}")
    print(f"plotted {', '.join(queries)}")
    print(f"normalized by vectorSize={args.baseline_vector_size}")
    print(f"wrote {display_path(png)}")


if __name__ == "__main__":
    main()
