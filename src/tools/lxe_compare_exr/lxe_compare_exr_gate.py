#!/usr/bin/env python3
"""Run lxe_compare_exr and fail when selected metrics exceed a gate."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys


def run_compare(compare_tool: Path, reference: Path, candidate: Path) -> dict[str, object]:
    result = subprocess.run(
        [
            str(compare_tool),
            "--reference",
            str(reference),
            "--candidate",
            str(candidate),
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    if result.returncode != 0:
        raise RuntimeError(f"lxe_compare_exr failed with code {result.returncode}")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"lxe_compare_exr did not emit JSON: {error}") from error


def number_at_path(metrics: dict[str, object], path: str) -> float:
    value: object = metrics
    for part in path.split("."):
        if not isinstance(value, dict) or part not in value:
            raise RuntimeError(f"compare metrics missing field: {path}")
        value = value[part]
    if not isinstance(value, (int, float)):
        raise RuntimeError(f"compare metrics field is not numeric: {path}")
    return float(value)


def linear_ratio(metrics: dict[str, object], threshold: float) -> float:
    linear_l1 = metrics.get("linearL1")
    if not isinstance(linear_l1, dict):
        raise RuntimeError("compare metrics missing linearL1")
    ratios = linear_l1.get("similarPixelRatios")
    if not isinstance(ratios, list):
        raise RuntimeError("compare metrics missing linearL1.similarPixelRatios")
    for item in ratios:
        if not isinstance(item, dict):
            continue
        item_threshold = item.get("threshold")
        ratio = item.get("ratio")
        if isinstance(item_threshold, (int, float)) and abs(item_threshold - threshold) < 1.0e-6:
            if not isinstance(ratio, (int, float)):
                raise RuntimeError(f"linear L1 ratio at {threshold} is not numeric")
            return float(ratio)
    raise RuntimeError(f"compare metrics missing linear L1 ratio at threshold {threshold}")


def list_number(value: object, label: str, index: int) -> float:
    if not isinstance(value, list) or len(value) <= index:
        raise RuntimeError(f"compare metrics missing {label}[{index}]")
    item = value[index]
    if not isinstance(item, (int, float)):
        raise RuntimeError(f"compare metrics {label}[{index}] is not numeric")
    return float(item)


def check_gate(metrics: dict[str, object], args: argparse.Namespace) -> list[str]:
    failures: list[str] = []

    mean_abs_error = number_at_path(metrics, "meanAbsError")
    if mean_abs_error > args.max_mean_abs_error:
        failures.append(
            f"meanAbsError {mean_abs_error:.6g} > {args.max_mean_abs_error:.6g}"
        )

    rmse = number_at_path(metrics, "rmse")
    if rmse > args.max_rmse:
        failures.append(f"rmse {rmse:.6g} > {args.max_rmse:.6g}")

    ratio = linear_ratio(metrics, args.linear_l1_threshold)
    if ratio < args.min_linear_l1_ratio:
        failures.append(
            "linear L1 similar ratio "
            f"@{args.linear_l1_threshold:g} {ratio:.6g} < {args.min_linear_l1_ratio:.6g}"
        )

    coverage = metrics.get("coverage")
    if not isinstance(coverage, dict):
        failures.append("coverage metrics missing")
        return failures

    reference_lit = number_at_path(metrics, "coverage.referenceLitPixels")
    candidate_lit = number_at_path(metrics, "coverage.candidateLitPixels")
    if reference_lit < args.min_reference_lit_pixels:
        failures.append(
            f"referenceLitPixels {reference_lit:.0f} < {args.min_reference_lit_pixels:.0f}"
        )
    if candidate_lit < args.min_candidate_lit_pixels:
        failures.append(
            f"candidateLitPixels {candidate_lit:.0f} < {args.min_candidate_lit_pixels:.0f}"
        )
    if reference_lit > 0.0:
        lit_ratio = candidate_lit / reference_lit
        if lit_ratio < args.min_lit_pixel_ratio or lit_ratio > args.max_lit_pixel_ratio:
            failures.append(
                f"candidate/reference lit ratio {lit_ratio:.6g} outside "
                f"[{args.min_lit_pixel_ratio:.6g}, {args.max_lit_pixel_ratio:.6g}]"
            )

    reference_centroid = coverage.get("referenceCentroid")
    candidate_centroid = coverage.get("candidateCentroid")
    centroid_dx = abs(
        list_number(candidate_centroid, "coverage.candidateCentroid", 0)
        - list_number(reference_centroid, "coverage.referenceCentroid", 0)
    )
    centroid_dy = abs(
        list_number(candidate_centroid, "coverage.candidateCentroid", 1)
        - list_number(reference_centroid, "coverage.referenceCentroid", 1)
    )
    if centroid_dx > args.max_centroid_delta or centroid_dy > args.max_centroid_delta:
        failures.append(
            f"centroid delta ({centroid_dx:.3g}, {centroid_dy:.3g}) exceeds "
            f"{args.max_centroid_delta:.3g}px"
        )

    return failures


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compare-tool", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--max-mean-abs-error", type=float, default=0.05)
    parser.add_argument("--max-rmse", type=float, default=0.15)
    parser.add_argument("--linear-l1-threshold", type=float, default=0.4)
    parser.add_argument("--min-linear-l1-ratio", type=float, default=0.90)
    parser.add_argument("--min-reference-lit-pixels", type=float, default=8000.0)
    parser.add_argument("--min-candidate-lit-pixels", type=float, default=8000.0)
    parser.add_argument("--min-lit-pixel-ratio", type=float, default=0.75)
    parser.add_argument("--max-lit-pixel-ratio", type=float, default=1.35)
    parser.add_argument("--max-centroid-delta", type=float, default=8.0)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        metrics = run_compare(args.compare_tool, args.reference, args.candidate)
        failures = check_gate(metrics, args)
    except Exception as error:
        print(f"lxe_compare_exr_gate error: {error}", file=sys.stderr)
        return 1

    if failures:
        print("lxe_compare_exr_gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 2
    print("lxe_compare_exr_gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
