#!/usr/bin/env python3

import argparse
import json
import pathlib
import struct
import subprocess
import sys


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--binary-dir", required=True)
    parser.add_argument("--renderer", required=True)
    return parser.parse_args()


def run_render(args, source_dir, output_base, scene_path, profile, width, height):
    output_base.parent.mkdir(parents=True, exist_ok=True)

    command = [
        args.renderer,
        "--scene",
        str(source_dir / scene_path),
        "--profile",
        profile,
        "--width",
        str(width),
        "--height",
        str(height),
        "--samples",
        "1",
        "--max-bounce",
        "1",
        "--out",
        str(output_base),
    ]
    completed = subprocess.run(
        command,
        cwd=source_dir,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout)
        raise RuntimeError(f"offline render failed: {scene_path}")
    return completed.stdout


def validate_visible_rgba32f(output_base, width, height, *, min_max, min_mean, min_lit_pixels, min_center_mean):

    raw_path = output_base.with_suffix(".rgba32f")
    exr_path = output_base.with_suffix(".exr")
    png_path = output_base.with_suffix(".png")
    metadata_path = output_base.with_suffix(".json")
    for path in (raw_path, exr_path, png_path, metadata_path):
        if not path.exists() or path.stat().st_size == 0:
            raise RuntimeError(f"missing offline output: {path}")

    data = raw_path.read_bytes()
    expected_bytes = width * height * 4 * 4
    if len(data) != expected_bytes:
        raise RuntimeError(f"raw output byte size mismatch: {len(data)}")

    values = struct.unpack("<" + "f" * (len(data) // 4), data)
    rgb_values = [max(0.0, value) for index, value in enumerate(values) if index % 4 != 3]
    max_rgb = max(rgb_values)
    mean_rgb = sum(rgb_values) / len(rgb_values)
    lit_pixels = 0
    center_sum = 0.0
    center_count = 0
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 4
            r, g, b = values[offset : offset + 3]
            if max(r, g, b) > 0.02:
                lit_pixels += 1
            if width // 4 <= x < width * 3 // 4 and height // 4 <= y < height * 3 // 4:
                center_sum += max(0.0, r) + max(0.0, g) + max(0.0, b)
                center_count += 3

    center_mean = center_sum / max(center_count, 1)
    if (
        max_rgb < min_max
        or mean_rgb < min_mean
        or lit_pixels < min_lit_pixels
        or center_mean < min_center_mean
    ):
        raise RuntimeError(
            "offline smoke produced no visible RGB result: "
            f"max={max_rgb:.6f} mean={mean_rgb:.6f} "
            f"lit_pixels={lit_pixels} center_mean={center_mean:.6f}\n"
        )

    metadata = json.loads(metadata_path.read_text())
    if metadata.get("width") != width or metadata.get("height") != height:
        raise RuntimeError(f"metadata extent mismatch: {metadata_path}")

    return {
        "max_rgb": max_rgb,
        "mean_rgb": mean_rgb,
        "lit_pixels": lit_pixels,
        "center_mean": center_mean,
    }


def main() -> int:
    args = parse_args()
    source_dir = pathlib.Path(args.source_dir)
    binary_dir = pathlib.Path(args.binary_dir)
    output_dir = binary_dir / "test-output" / "offline_rt_cli_smoke"

    helmet_width = 192
    helmet_height = 192
    helmet_output = output_dir / "helmet_raytrace_direct"
    helmet_stdout = run_render(
        args,
        source_dir,
        helmet_output,
        "assets/scenes/generated/helmet_standard_pbr.scene.yaml",
        "raytrace",
        helmet_width,
        helmet_height,
    )
    helmet_stats = validate_visible_rgba32f(
        helmet_output,
        helmet_width,
        helmet_height,
        min_max=0.2,
        min_mean=0.02,
        min_lit_pixels=helmet_width * helmet_height // 8,
        min_center_mean=0.03,
    )

    print(helmet_stdout.strip())
    print(
        f"offline_rt_cli_smoke helmet_raytrace_direct output={helmet_output} "
        f"max={helmet_stats['max_rgb']:.4f} "
        f"mean={helmet_stats['mean_rgb']:.4f} "
        f"lit_pixels={helmet_stats['lit_pixels']} "
        f"center_mean={helmet_stats['center_mean']:.4f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
