#!/usr/bin/env python3
"""Run the controlled realtime/offline EXR comparison scene."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys


def run_command(args: list[str], cwd: Path) -> str:
    result = subprocess.run(
        args,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "command failed with code "
            f"{result.returncode}: {' '.join(args)}\n{result.stdout}"
        )
    return result.stdout


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--binary-dir", required=True)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    source_dir = Path(args.source_dir).resolve()
    binary_dir = Path(args.binary_dir).resolve()

    scene = source_dir / "assets/scenes/realtime_offline_compare_flat.scene.yaml"
    realtime_wrapper = source_dir / "src/tools/lxe_realtime_render/lxe_realtime_render.py"
    offline_tool = binary_dir / "src/tools/lxe_offline_render/lxe_offline_render"
    compare_tool = binary_dir / "src/tools/lxe_compare_exr/lxe_compare_exr"
    editor = binary_dir / "src/demos/lxe_editor/lxe_editor"

    run_command(
        [
            sys.executable,
            str(realtime_wrapper),
            "--scene",
            str(scene),
            "--profile",
            "preview",
            "--editor",
            str(editor),
            "--xvfb",
            "--timeout-sec",
            "120",
        ],
        source_dir,
    )
    run_command(
        [
            str(offline_tool),
            "--scene",
            str(scene),
            "--profile",
            "preview",
            "--out",
            "artifacts/compare/flat/offline/render",
        ],
        source_dir,
    )
    compare_output = run_command(
        [
            str(compare_tool),
            "--reference",
            "artifacts/compare/flat/offline/render.exr",
            "--candidate",
            "artifacts/compare/flat/realtime/Realtime%20Offline%20Compare%20Flat/preview/render-linear.exr",
            "--mean-threshold",
            "0",
            "--max-threshold",
            "0",
            "--rmse-threshold",
            "0",
        ],
        source_dir,
    )
    metrics = json.loads(compare_output)
    if not metrics.get("passed", False):
        raise RuntimeError(f"controlled EXR comparison failed: {compare_output}")
    print(compare_output.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
