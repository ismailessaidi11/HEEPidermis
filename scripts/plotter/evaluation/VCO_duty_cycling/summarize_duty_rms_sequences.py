# Copyright 2026 EPFL contributors
# SPDX-License-Identifier: Apache-2.0
#
# Summarize duty-cycle RMS CSVs across sequence-order validation runs.

import argparse
import csv
import math
from pathlib import Path
from statistics import mean, stdev
from typing import Dict, List


DEFAULT_CSV_DIR = Path(__file__).resolve().parent / "csv"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Aggregate duty-cycle RMS summaries across sequence CSV files."
    )
    parser.add_argument(
        "inputs",
        nargs="*",
        help="Input summary CSVs. Defaults to scripts/plotter/evaluation/VCO_duty_cycling/csv/sequence_*.csv",
    )
    parser.add_argument(
        "--out",
        default="duty_rms_cross_validation.csv",
        help="Output CSV name/path. Bare filenames are written under this evaluation folder's csv directory",
    )
    return parser.parse_args()


def output_path(path: str) -> Path:
    out = Path(path)
    if not out.is_absolute() and out.parent == Path("."):
        out = DEFAULT_CSV_DIR / out
    out.parent.mkdir(parents=True, exist_ok=True)
    return out


def input_paths(inputs: List[str]) -> List[Path]:
    if inputs:
        return [Path(item) for item in inputs]
    return sorted(DEFAULT_CSV_DIR.glob("sequence_*.csv"))


def sample_stdev(values: List[float]) -> float:
    return stdev(values) if len(values) > 1 else 0.0


def rms(values: List[float]) -> float:
    if not values:
        return math.nan
    return math.sqrt(sum(value * value for value in values) / len(values))


def read_rows(paths: List[Path]) -> Dict[int, List[dict]]:
    groups: Dict[int, List[dict]] = {}
    for path in paths:
        with path.open("r", newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                duty_code = int(row["duty_code"])
                row["_source"] = path.name
                groups.setdefault(duty_code, []).append(row)
    return groups


def summarize(groups: Dict[int, List[dict]]) -> List[dict]:
    output = []
    for duty_code in sorted(groups.keys(), reverse=True):
        rows = groups[duty_code]
        rms_values = [float(row["rms_nS"]) for row in rows]
        mean_errors = [float(row["mean_error_nS"]) for row in rows]
        sample_counts = [int(row["n_samples"]) for row in rows]
        duty_percent = float(rows[0]["duty_percent"])

        output.append(
            {
                "duty_code": duty_code,
                "duty_percent": duty_percent,
                "n_sequences": len(rows),
                "total_samples": sum(sample_counts),
                "mean_rms_nS": mean(rms_values),
                "stdev_rms_nS": sample_stdev(rms_values),
                "min_rms_nS": min(rms_values),
                "max_rms_nS": max(rms_values),
                "rms_of_mean_error_nS": rms(mean_errors),
                "mean_error_nS": mean(mean_errors),
                "stdev_mean_error_nS": sample_stdev(mean_errors),
                "sources": ";".join(row["_source"] for row in rows),
            }
        )
    return output


def write_summary(path: Path, rows: List[dict]) -> None:
    fieldnames = [
        "duty_code",
        "duty_percent",
        "n_sequences",
        "total_samples",
        "mean_rms_nS",
        "stdev_rms_nS",
        "min_rms_nS",
        "max_rms_nS",
        "rms_of_mean_error_nS",
        "mean_error_nS",
        "stdev_mean_error_nS",
        "sources",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def print_summary(rows: List[dict]) -> None:
    print("duty_code,duty_percent,n_sequences,total_samples,mean_rms_nS,stdev_rms_nS,mean_error_nS")
    for row in rows:
        print(
            f"{row['duty_code']},"
            f"{row['duty_percent']:.3f},"
            f"{row['n_sequences']},"
            f"{row['total_samples']},"
            f"{row['mean_rms_nS']:.6f},"
            f"{row['stdev_rms_nS']:.6f},"
            f"{row['mean_error_nS']:+.6f}"
        )


def main() -> int:
    args = parse_args()
    paths = input_paths(args.inputs)
    if not paths:
        print(f"No input CSVs found in {DEFAULT_CSV_DIR}")
        return 1

    rows = summarize(read_rows(paths))
    out = output_path(args.out)
    write_summary(out, rows)
    print_summary(rows)
    print(f"Wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
