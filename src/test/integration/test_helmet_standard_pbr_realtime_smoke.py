#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class HelmetStandardPbrRealtimeSmokeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        parser = argparse.ArgumentParser()
        parser.add_argument("--source-dir", required=True)
        parser.add_argument("--editor", default="")
        args, _ = parser.parse_known_args()
        cls.source_dir = Path(args.source_dir)
        cls.editor = args.editor

    def test_converted_helmet_renders_nonblack_through_source_variant_path(self) -> None:
        scene = (
            self.source_dir
            / "assets"
            / "scenes"
            / "generated"
            / "helmet_standard_pbr.scene.yaml"
        )
        command = [
            sys.executable,
            str(
                self.source_dir
                / "src"
                / "tools"
                / "lxe_realtime_render"
                / "lxe_realtime_render.py"
            ),
            "--scene",
            str(scene),
            "--profile",
            "preview",
            "--xvfb",
            "--require-nonblack",
            "--require-pipeline-metadata",
            "--project-name",
            "codex_test_helmet_standard_pbr",
        ]
        if self.editor:
            command.extend(["--editor", self.editor])

        result = subprocess.run(
            command,
            cwd=self.source_dir,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=90,
            check=False,
        )
        if result.returncode != 0:
            self.fail(
                "helmet standard-pbr realtime smoke failed\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )

        payload = json.loads(result.stdout.strip())
        stats = payload.get("imageStats", {})
        self.assertGreaterEqual(int(stats.get("litPixelCount", 0)), 64)
        self.assertGreaterEqual(float(stats.get("averageLuminance", 0.0)), 0.001)
        input_stats = payload["renderInputStats"]
        input_count = self._required_int_stat(input_stats, "inputCount")
        accepted_input_count = self._required_int_stat(
            input_stats, "acceptedInputCount"
        )
        rejected_input_count = self._required_int_stat(
            input_stats, "rejectedInputCount"
        )
        submitted_draw_count = self._required_int_stat(
            input_stats, "submittedDrawCount"
        )
        submitted_dispatch_count = self._required_int_stat(
            input_stats, "submittedDispatchCount"
        )
        fallback_count = self._required_int_stat(input_stats, "fallbackObservedCount")
        desc_pipeline_lookup_count = self._required_int_stat(
            input_stats, "descPipelineLookupCount"
        )
        desc_bound_input_count = self._required_int_stat(
            input_stats, "descBoundInputCount"
        )
        desc_executed_input_count = self._required_int_stat(
            input_stats, "descExecutedInputCount"
        )
        self.assertGreater(input_count, 0)
        self.assertGreater(accepted_input_count, 0)
        self.assertEqual(input_count, accepted_input_count + rejected_input_count)
        self.assertLess(rejected_input_count, input_count)
        self.assertGreater(submitted_draw_count, 0)
        self.assertEqual(submitted_dispatch_count, 0)
        self.assertEqual(fallback_count, 0)
        self.assertEqual(desc_pipeline_lookup_count, accepted_input_count)
        self.assertEqual(desc_bound_input_count, accepted_input_count)
        self.assertEqual(desc_executed_input_count, accepted_input_count)
        self.assertTrue(payload.get("cpuSrgbPngPath"))
        self.assertTrue(payload.get("metadataPath"))

    def test_require_output_files_rejects_missing_render_input_stats(self) -> None:
        module = self._load_realtime_render_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            payload = self._write_minimal_render_result(
                root, metadata={"width": 4, "height": 4}
            )

            with self.assertRaisesRegex(RuntimeError, "renderInputStats"):
                module.require_output_files(
                    root,
                    json.dumps(payload),
                    False,
                    0,
                    0.0,
                    False,
                )

    def test_require_output_files_accepts_native_integer_render_input_stats(
        self,
    ) -> None:
        module = self._load_realtime_render_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            payload = self._write_minimal_render_result(
                root,
                metadata={
                    "width": 4,
                    "height": 4,
                    "renderInputStats": self._full_render_input_stats(),
                },
            )

            result = module.require_output_files(
                root,
                json.dumps(payload),
                False,
                0,
                0.0,
                False,
            )

            self.assertEqual(
                result["renderInputStats"],
                self._full_render_input_stats(),
            )
            for value in result["renderInputStats"].values():
                self.assertIsInstance(value, int)
                self.assertNotIsInstance(value, bool)

    def test_require_output_files_rejects_malformed_render_input_stats(self) -> None:
        module = self._load_realtime_render_module()
        cases: list[tuple[str, object]] = [
            ("non-dict stats", []),
            (
                "missing key",
                self._full_render_input_stats("descExecutedInputCount"),
            ),
            (
                "missing submission proof",
                self._full_render_input_stats("descPipelineLookupCount"),
            ),
            (
                "missing compiler coverage",
                self._full_render_input_stats("acceptedInputCount"),
            ),
            (
                "string value",
                self._full_render_input_stats(descBoundInputCount="1"),
            ),
            (
                "float value",
                self._full_render_input_stats(descExecutedInputCount=1.9),
            ),
            (
                "bool value",
                self._full_render_input_stats(descExecutedInputCount=True),
            ),
        ]

        for label, stats in cases:
            with self.subTest(label=label):
                with tempfile.TemporaryDirectory() as temp_dir:
                    root = Path(temp_dir)
                    payload = self._write_minimal_render_result(
                        root,
                        metadata={
                            "width": 4,
                            "height": 4,
                            "renderInputStats": stats,
                        },
                    )

                    with self.assertRaisesRegex(RuntimeError, "renderInputStats"):
                        module.require_output_files(
                            root,
                            json.dumps(payload),
                            False,
                            0,
                            0.0,
                            False,
                        )

    @classmethod
    def _load_realtime_render_module(cls):
        module_path = (
            cls.source_dir
            / "src"
            / "tools"
            / "lxe_realtime_render"
            / "lxe_realtime_render.py"
        )
        spec = importlib.util.spec_from_file_location(
            "lxe_realtime_render_test_subject", module_path
        )
        if spec is None or spec.loader is None:
            raise RuntimeError(f"failed to load realtime render module: {module_path}")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def _required_int_stat(self, stats: dict[str, object], key: str) -> int:
        value = stats[key]
        self.assertIsInstance(value, int)
        self.assertNotIsInstance(value, bool)
        return value

    @staticmethod
    def _full_render_input_stats(
        omit_key: str = "", **overrides: object
    ) -> dict[str, object]:
        stats: dict[str, object] = {
            "inputCount": 2,
            "acceptedInputCount": 2,
            "rejectedInputCount": 0,
            "submittedDrawCount": 2,
            "submittedDispatchCount": 0,
            "fallbackObservedCount": 0,
            "descPipelineLookupCount": 2,
            "descBoundInputCount": 2,
            "descExecutedInputCount": 2,
        }
        stats.update(overrides)
        if omit_key:
            del stats[omit_key]
        return stats

    @staticmethod
    def _write_minimal_render_result(
        root: Path, metadata: dict[str, object]
    ) -> dict[str, object]:
        output_dir = root / "out"
        output_dir.mkdir()
        linear_exr = output_dir / "render.exr"
        cpu_png = output_dir / "render.png"
        metadata_path = output_dir / "render.json"
        linear_exr.write_bytes(b"exr")
        cpu_png.write_bytes(b"png")
        metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
        return {
            "linearExrPath": str(linear_exr.relative_to(root)),
            "cpuSrgbPngPath": str(cpu_png.relative_to(root)),
            "metadataPath": str(metadata_path.relative_to(root)),
            "width": 4,
            "height": 4,
        }


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1])
