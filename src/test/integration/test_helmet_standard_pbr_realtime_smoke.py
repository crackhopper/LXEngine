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

from PIL import Image


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
        input_count = self._required_int_stat(input_stats, "compilerInputCount")
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
        self.assertGreaterEqual(
            self._count_green_glow_pixels(
                self.source_dir / payload["cpuSrgbPngPath"]
            ),
            128,
        )

    def test_debug_color_transfer_export_bundle_has_ramp_proof(self) -> None:
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
            "--debug-color-transfer",
            "--require-debug-color-transfer",
            "--project-name",
            "codex_test_debug_color_transfer",
        ]
        if self.editor:
            command.extend(["--editor", self.editor])

        result = subprocess.run(
            command,
            cwd=self.source_dir,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=120,
            check=False,
        )
        if result.returncode != 0:
            self.fail(
                "debug color-transfer smoke failed\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )

        payload = json.loads(result.stdout.strip())
        debug_payload = payload["debugColorTransfer"]
        self.assertTrue(debug_payload["manifestPath"])
        self.assertGreaterEqual(len(debug_payload["targets"]), 6)
        probe_labels = {
            probe["label"]
            for probe in debug_payload["manifest"].get("probes", [])
        }
        self.assertIn("gray18", probe_labels)
        self.assertIn("half", probe_labels)

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

    def test_require_debug_color_transfer_flag_requires_debug_export(self) -> None:
        module = self._load_realtime_render_module()

        with self.assertRaisesRegex(
            RuntimeError,
            "--require-debug-color-transfer requires --debug-color-transfer",
        ):
            module.main(
                [
                    "--scene",
                    str(self.source_dir / "does-not-matter.scene.yaml"),
                    "--require-debug-color-transfer",
                ]
            )

    def test_require_debug_color_transfer_bundle_rejects_missing_probes(self) -> None:
        module = self._load_realtime_render_module()
        self.assertTrue(hasattr(module, "require_debug_color_transfer_bundle"))
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            payload = self._write_debug_color_transfer_bundle(root, probes=[])

            with self.assertRaisesRegex(RuntimeError, "no probes"):
                module.require_debug_color_transfer_bundle(root, json.dumps(payload))

    def test_require_debug_color_transfer_bundle_rejects_probe_mismatch(self) -> None:
        module = self._load_realtime_render_module()
        self.assertTrue(hasattr(module, "require_debug_color_transfer_bundle"))
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            payload = self._write_debug_color_transfer_bundle(
                root,
                probes=[
                    {
                        "target": "debug.ramp.srgb",
                        "label": "gray18",
                        "expected": 118,
                        "red": 118,
                        "green": 80,
                        "blue": 118,
                    }
                ],
            )

            with self.assertRaisesRegex(RuntimeError, "probe mismatch"):
                module.require_debug_color_transfer_bundle(root, json.dumps(payload))

    def test_require_debug_color_transfer_bundle_accepts_ramp_probes(self) -> None:
        module = self._load_realtime_render_module()
        self.assertTrue(hasattr(module, "require_debug_color_transfer_bundle"))
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            probes = [
                {
                    "target": "debug.ramp.srgb",
                    "label": "gray18",
                    "expected": 118,
                    "red": 118,
                    "green": 117,
                    "blue": 119,
                },
                {
                    "target": "debug.ramp.unorm_manual_srgb",
                    "label": "half",
                    "expected": 188,
                    "red": 188,
                    "green": 189,
                    "blue": 187,
                },
            ]
            payload = self._write_debug_color_transfer_bundle(root, probes=probes)

            result = module.require_debug_color_transfer_bundle(
                root, json.dumps(payload)
            )

            self.assertEqual(result["manifest"], {"probes": probes})

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
    def _count_green_glow_pixels(path: Path) -> int:
        image = Image.open(path).convert("RGB")
        return sum(
            1
            for red, green, blue in image.getdata()
            if green > 80 and green > red * 1.25 and green > blue * 1.05
        )

    @staticmethod
    def _full_render_input_stats(
        omit_key: str = "", **overrides: object
    ) -> dict[str, object]:
        stats: dict[str, object] = {
            "compilerInputCount": 2,
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

    @staticmethod
    def _write_debug_color_transfer_bundle(
        root: Path, probes: list[dict[str, object]]
    ) -> dict[str, object]:
        output_dir = root / "debug"
        output_dir.mkdir()
        for name in [
            "hdr_color.exr",
            "hdr_color_preview.png",
            "tone_mapped_linear.exr",
            "tone_mapped_linear_preview.png",
            "srgb_attachment.png",
            "unorm_manual_srgb.png",
            "ramp_srgb_attachment.png",
            "ramp_unorm_manual_srgb.png",
        ]:
            (output_dir / name).write_bytes(b"debug")
        manifest_path = output_dir / "manifest.json"
        manifest_path.write_text(json.dumps({"probes": probes}), encoding="utf-8")
        return {
            "manifestPath": str(manifest_path.relative_to(root)),
            "targets": [
                "hdr.color",
                "debug.ldr.linear",
                "debug.final.srgb",
                "debug.final.unorm_manual_srgb",
                "debug.ramp.srgb",
                "debug.ramp.unorm_manual_srgb",
            ],
        }


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1])
