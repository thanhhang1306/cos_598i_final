#!/usr/bin/env python3
"""Generate a runtime chart for selected TPC-H queries from the apple results."""

QUERIES = ["q1", "q3", "q6", "q9", "q18", "q5", "q10", "q11", "q12", "q13"]
BOLD_QUERIES = {"q1", "q3", "q6", "q9", "q18"}

RUNS = [
    ("apple", "clean-results/apple_results.out"),
]

import argparse
import math
import re
from dataclasses import dataclass
from pathlib import Path

try:
    import matplotlib.pyplot as plt
except ModuleNotFoundError as exc:
    raise SystemExit(
        "matplotlib is required to plot results; install it with "
        "`python3 -m pip install --user matplotlib` or load a Python "
        "environment that provides matplotlib."
    ) from exc

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RUNTIME_OUT = ROOT / "figures" / "apple_query_runtimes.png"

ENGINES = [
    ("hyper", "Typer", "#F8766D"),
    ("vectorwise", "Tectorwise", "#00BFC4"),
]

FALLBACK_HEADER = ["name", "time", "CPUs", "IPC", "GHz"]

ROW_RE = re.compile(r"^\s*(q\d+)\s+(hyper|vectorwise)\s*,", re.IGNORECASE)


@dataclass(frozen=True)
class Record:
    query: str
    engine: str
    time_ms: float


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
    return Record(
        query=normalize_query(match.group(1)),
        engine=match.group(2).lower(),
        time_ms=float(fields[header_index(header, "time")]),
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


def configured_runs():
    return [(label, resolve_path(path)) for label, path in RUNS]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate apple runtime chart for selected TPC-H queries."
    )
    parser.add_argument(
        "inputs",
        nargs="*",
        type=Path,
        help="Optional run_tpch .out files. If omitted, RUNS at the top of this file is used.",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_RUNTIME_OUT,
        help=f"Runtime PNG output path (default: {display_path(DEFAULT_RUNTIME_OUT)})",
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
    return [(path.stem, resolve_path(path)) for path in args.inputs]


def validate(records, queries):
    missing = []
    for query in queries:
        for engine, _, _ in ENGINES:
            if (query, engine) not in records:
                missing.append(f"{query} {engine}")
    if missing:
        raise SystemExit(
            "missing runtime rows in input: "
            + ", ".join(missing)
            + ". Edit QUERIES or point the script at a run containing these rows."
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
    for tick in ax.get_xticklabels():
        if tick.get_text() in BOLD_QUERIES:
            tick.set_fontweight("bold")
    ax.set_ylabel("Runtime [ms]")
    ax.set_axisbelow(True)
    ax.grid(axis="y", color="#e6e6e6", linewidth=0.7)
    ax.legend(loc="upper left", frameon=False)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)

    fig.tight_layout()
    save_png(fig, out)
    plt.close(fig)


def main():
    args = parse_args()
    runs = choose_runs(args)
    if not runs:
        raise SystemExit("no input runs configured")

    queries = [normalize_query(query) for query in (args.queries or QUERIES)]
    label, path = runs[0]
    records = parse(path)
    if not records:
        raise SystemExit(f"no query runtime rows found in {display_path(path)}")
    print(f"read {label}: {display_path(path)}")

    validate(records, queries)
    plot_runtimes(records, queries, args.out)
    print(f"plotted {', '.join(queries)}")


if __name__ == "__main__":
    main()
