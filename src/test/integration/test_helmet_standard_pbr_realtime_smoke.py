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
        batch_stats = payload["renderBatchStats"]
        compiler_input_draw_count = self._required_int_stat(
            batch_stats, "compilerInputDrawCount"
        )
        compiler_prepared_candidate_count = self._required_int_stat(
            batch_stats, "compilerPreparedCandidateCount"
        )
        compiler_batch_count_consumed = self._required_int_stat(
            batch_stats, "compilerBatchCountConsumed"
        )
        compiler_batch_count = self._required_int_stat(
            batch_stats, "compilerBatchCount"
        )
        compiler_draw_count = self._required_int_stat(
            batch_stats, "compilerDrawCount"
        )
        submitted_batch_count = self._required_int_stat(
            batch_stats, "submittedIndirectBatchCount"
        )
        submitted_draw_count = self._required_int_stat(
            batch_stats, "submittedIndirectDrawCount"
        )
        fallback_count = self._required_int_stat(
            batch_stats, "fallbackObservedCount"
        )
        indirect_capable_draw_count = self._required_int_stat(
            batch_stats, "indirectCapableDrawCount"
        )
        indexed_indirect_command_count = self._required_int_stat(
            batch_stats, "submittedIndexedIndirectCommandCount"
        )
        direct_indexed_draw_count = self._required_int_stat(
            batch_stats, "submittedDirectIndexedDrawCount"
        )
        unsupported_draw_count = self._required_int_stat(
            batch_stats, "unsupportedDrawCount"
        )
        legacy_rejected_draw_count = self._required_int_stat(
            batch_stats, "legacyRejectedDrawCount"
        )
        first_command_offset = self._required_int_stat(
            batch_stats, "firstCommandOffset"
        )
        last_command_offset = self._required_int_stat(
            batch_stats, "lastCommandOffset"
        )
        self.assertGreater(compiler_batch_count, 0)
        self.assertGreater(compiler_input_draw_count, 0)
        self.assertEqual(
            compiler_input_draw_count,
            compiler_prepared_candidate_count,
        )
        self.assertEqual(
            compiler_prepared_candidate_count,
            compiler_draw_count,
        )
        self.assertEqual(compiler_draw_count, indirect_capable_draw_count)
        self.assertEqual(
            compiler_batch_count,
            compiler_batch_count_consumed,
        )
        self.assertEqual(
            compiler_batch_count_consumed,
            submitted_batch_count,
        )
        self.assertGreater(indirect_capable_draw_count, 0)
        self.assertEqual(indirect_capable_draw_count, submitted_draw_count)
        self.assertEqual(indexed_indirect_command_count, submitted_draw_count)
        self.assertGreater(submitted_draw_count, 0)
        self.assertEqual(direct_indexed_draw_count, 0)
        self.assertEqual(unsupported_draw_count, 0)
        self.assertEqual(legacy_rejected_draw_count, 0)
        self.assertEqual(fallback_count, 0)
        covered_command_count = last_command_offset + 1
        self.assertEqual(first_command_offset, 0)
        self.assertEqual(covered_command_count, submitted_draw_count)
        self.assertEqual(covered_command_count, compiler_draw_count)
        self.assertEqual(covered_command_count, indirect_capable_draw_count)
        self.assertTrue(payload.get("cpuSrgbPngPath"))
        self.assertTrue(payload.get("metadataPath"))

    def test_require_output_files_rejects_missing_render_batch_stats(self) -> None:
        module = self._load_realtime_render_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            payload = self._write_minimal_render_result(
                root, metadata={"width": 4, "height": 4}
            )

            with self.assertRaisesRegex(RuntimeError, "renderBatchStats"):
                module.require_output_files(
                    root,
                    json.dumps(payload),
                    False,
                    0,
                    0.0,
                    False,
                )

    def test_require_output_files_accepts_native_integer_render_batch_stats(
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
                    "renderBatchStats": self._full_render_batch_stats(),
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
                result["renderBatchStats"],
                self._full_render_batch_stats(),
            )
            for value in result["renderBatchStats"].values():
                self.assertIsInstance(value, int)
                self.assertNotIsInstance(value, bool)

    def test_require_output_files_rejects_malformed_render_batch_stats(self) -> None:
        module = self._load_realtime_render_module()
        cases: list[tuple[str, object]] = [
            ("non-dict stats", []),
            (
                "missing key",
                self._full_render_batch_stats("submittedIndirectDrawCount"),
            ),
            (
                "missing first offset",
                self._full_render_batch_stats("firstCommandOffset"),
            ),
            (
                "missing compiler coverage",
                self._full_render_batch_stats("compilerDrawCount"),
            ),
            (
                "string value",
                self._full_render_batch_stats(
                    compilerBatchCountConsumed="1"
                ),
            ),
            (
                "float value",
                self._full_render_batch_stats(submittedIndirectBatchCount=1.9),
            ),
            (
                "bool value",
                self._full_render_batch_stats(submittedIndirectBatchCount=True),
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
                            "renderBatchStats": stats,
                        },
                    )

                    with self.assertRaisesRegex(RuntimeError, "renderBatchStats"):
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
    def _full_render_batch_stats(
        omit_key: str = "", **overrides: object
    ) -> dict[str, object]:
        stats: dict[str, object] = {
            "compilerInputDrawCount": 2,
            "compilerPreparedCandidateCount": 2,
            "compilerBatchCount": 1,
            "compilerDrawCount": 2,
            "indirectCapableDrawCount": 2,
            "unsupportedDrawCount": 0,
            "legacyRejectedDrawCount": 0,
            "compilerBatchCountConsumed": 1,
            "boundBatchGeometryCount": 1,
            "submittedDirectIndexedDrawCount": 0,
            "submittedIndexedIndirectCommandCount": 2,
            "submittedIndirectBatchCount": 1,
            "submittedIndirectDrawCount": 2,
            "firstCommandOffset": 0,
            "lastCommandOffset": 1,
            "fallbackObservedCount": 0,
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
