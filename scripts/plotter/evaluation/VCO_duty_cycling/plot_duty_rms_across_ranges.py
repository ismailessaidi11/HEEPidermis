# Copyright 2026 EPFL contributors
# SPDX-License-Identifier: Apache-2.0
#
# Plot duty-cycling RMS impact across cross-validated conductance ranges.

import argparse
import csv
import math
import re
from dataclasses import dataclass
from pathlib import Path
from statistics import mean, median, stdev
from typing import Dict, Iterable, List, Sequence


ROOT = Path(__file__).resolve().parent
DEFAULT_CSV_DIR = ROOT / "csv"
DEFAULT_FIG_DIR = ROOT / "figs"


@dataclass
class RangePoint:
    data_range: str
    range_sort_key: int
    source_file: str
    duty_code: int
    duty_percent: float
    mean_rms_nS: float
    stdev_rms_nS: float
    total_samples: int
    baseline_rms_nS: float

    @property
    def relative_rms_nS(self) -> float:
        return self.mean_rms_nS - self.baseline_rms_nS

    @property
    def relative_rms_pct(self) -> float:
        if self.baseline_rms_nS == 0.0:
            return math.nan
        return 100.0 * self.relative_rms_nS / self.baseline_rms_nS


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare duty-cycle mean RMS across cross-validated conductance datasets."
    )
    parser.add_argument(
        "inputs",
        nargs="*",
        help="Cross-validation CSV files. Defaults to csv/**/duty_rms_cross_validation_*.csv",
    )
    parser.add_argument("--csv-dir", default=str(DEFAULT_CSV_DIR), help="Root CSV directory")
    parser.add_argument("--fig-dir", default=str(DEFAULT_FIG_DIR), help="Figure output directory")
    parser.add_argument(
        "--normalized-csv",
        default="duty_rms_normalized_by_range.csv",
        help="Normalized per-dataset CSV output name/path",
    )
    parser.add_argument(
        "--summary-csv",
        default="duty_rms_relative_summary.csv",
        help="Cross-dataset summary CSV output name/path",
    )
    return parser.parse_args()


def resolve_output(path: str, default_dir: Path) -> Path:
    out = Path(path)
    if not out.is_absolute() and out.parent == Path("."):
        out = default_dir / out
    out.parent.mkdir(parents=True, exist_ok=True)
    return out


def range_sort_key(label: str) -> int:
    nums = re.findall(r"\d+", label)
    return int(nums[0]) if nums else 0


def discover_inputs(args: argparse.Namespace) -> List[Path]:
    if args.inputs:
        return [Path(item) for item in args.inputs]
    csv_dir = Path(args.csv_dir)
    return sorted(csv_dir.glob("**/duty_rms_cross_validation_*.csv"))


def load_points(paths: Sequence[Path]) -> List[RangePoint]:
    points: List[RangePoint] = []
    for path in paths:
        rows = []
        with path.open("r", newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                rows.append(row)

        if not rows:
            continue

        baseline = min(float(row["mean_rms_nS"]) for row in rows)
        data_range = path.parent.name
        sort_key = range_sort_key(data_range)

        for row in rows:
            points.append(
                RangePoint(
                    data_range=data_range,
                    range_sort_key=sort_key,
                    source_file=path.name,
                    duty_code=int(row["duty_code"]),
                    duty_percent=float(row["duty_percent"]),
                    mean_rms_nS=float(row["mean_rms_nS"]),
                    stdev_rms_nS=float(row["stdev_rms_nS"]),
                    total_samples=int(row["total_samples"]),
                    baseline_rms_nS=baseline,
                )
            )
    return points


def grouped_by_duty(points: Iterable[RangePoint]) -> Dict[int, List[RangePoint]]:
    groups: Dict[int, List[RangePoint]] = {}
    for point in points:
        groups.setdefault(point.duty_code, []).append(point)
    return groups


def grouped_by_range(points: Iterable[RangePoint]) -> Dict[str, List[RangePoint]]:
    groups: Dict[str, List[RangePoint]] = {}
    for point in points:
        groups.setdefault(point.data_range, []).append(point)
    return groups


def sample_stdev(values: Sequence[float]) -> float:
    return stdev(values) if len(values) > 1 else 0.0


def percentile(values: Sequence[float], pct: float) -> float:
    if not values:
        return math.nan
    sorted_values = sorted(values)
    if len(sorted_values) == 1:
        return sorted_values[0]
    pos = (len(sorted_values) - 1) * pct / 100.0
    low = math.floor(pos)
    high = math.ceil(pos)
    if low == high:
        return sorted_values[int(pos)]
    frac = pos - low
    return sorted_values[low] * (1.0 - frac) + sorted_values[high] * frac


def write_normalized_csv(path: Path, points: Sequence[RangePoint]) -> None:
    fields = [
        "data_range",
        "source_file",
        "duty_code",
        "duty_percent",
        "mean_rms_nS",
        "baseline_rms_nS",
        "relative_rms_nS",
        "relative_rms_pct",
        "stdev_rms_nS",
        "total_samples",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for point in sorted(points, key=lambda p: (p.range_sort_key, p.duty_code), reverse=False):
            writer.writerow(
                {
                    "data_range": point.data_range,
                    "source_file": point.source_file,
                    "duty_code": point.duty_code,
                    "duty_percent": f"{point.duty_percent:.6f}",
                    "mean_rms_nS": f"{point.mean_rms_nS:.6f}",
                    "baseline_rms_nS": f"{point.baseline_rms_nS:.6f}",
                    "relative_rms_nS": f"{point.relative_rms_nS:.6f}",
                    "relative_rms_pct": f"{point.relative_rms_pct:.6f}",
                    "stdev_rms_nS": f"{point.stdev_rms_nS:.6f}",
                    "total_samples": point.total_samples,
                }
            )


def summary_rows(points: Sequence[RangePoint]) -> List[dict]:
    rows = []
    for duty_code, group in sorted(grouped_by_duty(points).items(), key=lambda item: duty_percent_from_group(item[1])):
        rel = [point.relative_rms_nS for point in group]
        rel_pct = [point.relative_rms_pct for point in group]
        rows.append(
            {
                "duty_code": duty_code,
                "duty_percent": group[0].duty_percent,
                "n_ranges": len(group),
                "mean_relative_rms_nS": mean(rel),
                "median_relative_rms_nS": median(rel),
                "stdev_relative_rms_nS": sample_stdev(rel),
                "q1_relative_rms_nS": percentile(rel, 25.0),
                "q3_relative_rms_nS": percentile(rel, 75.0),
                "mean_relative_rms_pct": mean(rel_pct),
                "median_relative_rms_pct": median(rel_pct),
            }
        )
    return rows


def duty_percent_from_group(group: Sequence[RangePoint]) -> float:
    return group[0].duty_percent if group else math.nan


def write_summary_csv(path: Path, rows: Sequence[dict]) -> None:
    fields = [
        "duty_code",
        "duty_percent",
        "n_ranges",
        "mean_relative_rms_nS",
        "median_relative_rms_nS",
        "stdev_relative_rms_nS",
        "q1_relative_rms_nS",
        "q3_relative_rms_nS",
        "mean_relative_rms_pct",
        "median_relative_rms_pct",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: (f"{value:.6f}" if isinstance(value, float) else value) for key, value in row.items()})


def import_pyplot():
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit("matplotlib is required for plotting. Install it in the heepidermis environment.") from exc
    return plt


def duty_order(points: Sequence[RangePoint]) -> List[int]:
    return [
        duty_code
        for duty_code, group in sorted(
            grouped_by_duty(points).items(),
            key=lambda item: duty_percent_from_group(item[1]),
        )
    ]


def range_order(points: Sequence[RangePoint]) -> List[str]:
    groups = grouped_by_range(points)
    return [name for name, _ in sorted(groups.items(), key=lambda item: range_sort_key(item[0]))]


def plot_lines(points: Sequence[RangePoint], fig_dir: Path) -> None:
    plt = import_pyplot()
    duties = duty_order(points)
    duty_labels = [f"{grouped_by_duty(points)[d][0].duty_percent:g}%" for d in duties]
    by_range = grouped_by_range(points)

    fig, ax = plt.subplots(figsize=(8, 4.8))
    for data_range in range_order(points):
        group = {point.duty_code: point for point in by_range[data_range]}
        y = [group[duty].relative_rms_nS for duty in duties]
        ax.plot(duty_labels, y, marker="o", linewidth=1.0, alpha=0.35, label=data_range)

    by_duty = grouped_by_duty(points)
    y_mean = [mean([point.relative_rms_nS for point in by_duty[duty]]) for duty in duties]
    ax.plot(duty_labels, y_mean, marker="o", color="black", linewidth=2.5, label="mean")
    ax.set_xlabel("VCO duty cycle")
    ax.set_ylabel("Extra mean RMS vs best in same range (nS)")
    ax.set_title("Duty-cycle RMS penalty by conductance range")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=7, ncol=2)
    fig.tight_layout()
    fig.savefig(fig_dir / "duty_rms_relative_lines.png", dpi=200)
    plt.close(fig)


def plot_box(points: Sequence[RangePoint], fig_dir: Path) -> None:
    plt = import_pyplot()
    duties = duty_order(points)
    by_duty = grouped_by_duty(points)
    values = [[point.relative_rms_nS for point in by_duty[duty]] for duty in duties]
    labels = [f"{by_duty[duty][0].duty_percent:g}%" for duty in duties]

    fig, ax = plt.subplots(figsize=(7, 4.6))
    ax.boxplot(values, labels=labels, showmeans=True)
    ax.set_xlabel("VCO duty cycle")
    ax.set_ylabel("Extra mean RMS vs best in same range (nS)")
    ax.set_title("Cross-dataset spread of duty-cycle RMS penalty")
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(fig_dir / "duty_rms_relative_boxplot.png", dpi=200)
    plt.close(fig)


def plot_heatmap(points: Sequence[RangePoint], fig_dir: Path) -> None:
    plt = import_pyplot()
    duties = duty_order(points)
    ranges = range_order(points)
    by_range = grouped_by_range(points)
    matrix = []
    for data_range in ranges:
        group = {point.duty_code: point for point in by_range[data_range]}
        matrix.append([group[duty].relative_rms_nS for duty in duties])

    fig, ax = plt.subplots(figsize=(7.2, max(3.5, 0.45 * len(ranges) + 1.5)))
    image = ax.imshow(matrix, aspect="auto", cmap="viridis")
    ax.set_xticks(range(len(duties)))
    ax.set_xticklabels([f"{grouped_by_duty(points)[duty][0].duty_percent:g}%" for duty in duties])
    ax.set_yticks(range(len(ranges)))
    ax.set_yticklabels(ranges)
    ax.set_xlabel("VCO duty cycle")
    ax.set_ylabel("Conductance dataset")
    ax.set_title("Extra mean RMS by range and duty cycle")
    cbar = fig.colorbar(image, ax=ax)
    cbar.set_label("Extra mean RMS (nS)")
    fig.tight_layout()
    fig.savefig(fig_dir / "duty_rms_relative_heatmap.png", dpi=200)
    plt.close(fig)


def print_summary(rows: Sequence[dict]) -> None:
    print("duty_percent,n_ranges,median_extra_rms_nS,mean_extra_rms_nS,iqr_extra_rms_nS")
    for row in rows:
        iqr = row["q3_relative_rms_nS"] - row["q1_relative_rms_nS"]
        print(
            f"{row['duty_percent']:.3f},"
            f"{row['n_ranges']},"
            f"{row['median_relative_rms_nS']:.6f},"
            f"{row['mean_relative_rms_nS']:.6f},"
            f"{iqr:.6f}"
        )


def main() -> int:
    args = parse_args()
    inputs = discover_inputs(args)
    if not inputs:
        print(f"No cross-validation CSVs found under {args.csv_dir}")
        return 1

    points = load_points(inputs)
    if not points:
        print("No rows found in input CSVs")
        return 1

    csv_dir = Path(args.csv_dir)
    fig_dir = Path(args.fig_dir)
    fig_dir.mkdir(parents=True, exist_ok=True)

    normalized_csv = resolve_output(args.normalized_csv, csv_dir)
    summary_csv = resolve_output(args.summary_csv, csv_dir)
    rows = summary_rows(points)
    write_normalized_csv(normalized_csv, points)
    write_summary_csv(summary_csv, rows)
    plot_lines(points, fig_dir)
    plot_box(points, fig_dir)
    plot_heatmap(points, fig_dir)
    print_summary(rows)
    print(f"Wrote {normalized_csv}")
    print(f"Wrote {summary_csv}")
    print(f"Wrote figures under {fig_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
