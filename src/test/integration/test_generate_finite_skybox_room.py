#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import unittest
from pathlib import Path

from PIL import Image


def parse_test_args() -> tuple[Path, Path]:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--binary-dir", required=True)
    args, remaining = parser.parse_known_args()
    sys.argv = [sys.argv[0], *remaining]
    return Path(args.source_dir), Path(args.binary_dir)


SOURCE_DIR, BINARY_DIR = parse_test_args()


class GenerateFiniteSkyboxRoomTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source_dir = SOURCE_DIR
        cls.output_dir = BINARY_DIR / "test_generated" / "finite_room"

    def setUp(self) -> None:
        if self.output_dir.exists():
            for path in sorted(self.output_dir.rglob("*"), reverse=True):
                if path.is_file():
                    path.unlink()
                elif path.is_dir():
                    path.rmdir()

    def test_generates_finite_room_assets_from_neutral_ktx2(self) -> None:
        result = self._run_generator(
            "--input",
            "assets/env/khronos/neutral/ggx/specular.ktx2",
            "--bounds",
            "-12",
            "12",
            "-8",
            "10",
            "-12",
            "12",
            "--output-dir",
            str(self.output_dir),
            "--name",
            "test_neutral_room",
            "--tone-map",
            "aces",
            "--exposure",
            "1.0",
        )

        self.assertEqual(
            result.returncode,
            0,
            f"generator failed\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )

        obj = self.output_dir / "test_neutral_room.obj"
        material = self.output_dir / "test_neutral_room_unlit.material"
        snippet = self.output_dir / "test_neutral_room.scene-snippet.yaml"
        png = self.output_dir / "textures" / "test_neutral_room_srgb.png"

        for path in [obj, material, snippet, png]:
            self.assertTrue(path.is_file(), f"missing generated file: {path}")
            self.assertGreater(path.stat().st_size, 0, f"empty generated file: {path}")

        obj_text = obj.read_text(encoding="utf-8")
        self.assertIn("\nvt ", obj_text)
        self.assertNotIn("mtllib", obj_text)

        material_text = material.read_text(encoding="utf-8")
        self.assertIn("schema: lxe.material.v2", material_text)
        self.assertIn("renderClass: surface.opaque", material_text)
        self.assertIn("type: unlit-texture", material_text)
        self.assertIn(
            "assets://shaders/glsl/common/materials/unlit_texture.contract.glsl",
            material_text,
        )
        self.assertIn("textures/test_neutral_room_srgb.png", material_text)

        snippet_text = snippet.read_text(encoding="utf-8")
        self.assertIn("test_neutral_room.obj", snippet_text)
        self.assertIn("test_neutral_room_unlit.material", snippet_text)

        with Image.open(png) as image:
            self.assertEqual(image.mode, "RGB")
            self.assertGreater(image.width, image.height)

    def test_helmet_scene_finite_room_references_resolve(self) -> None:
        scene = (
            self.source_dir
            / "assets"
            / "scenes"
            / "generated"
            / "helmet_standard_pbr.scene.yaml"
        )
        scene_text = scene.read_text(encoding="utf-8")
        self.assertIn("nodeName: finite_neutral_room", scene_text)

        mesh_match = re.search(
            r"mesh:\s*\n\s*uri:\s*(assets/scenes/generated/finite_room/test_neutral_room\.obj)",
            scene_text,
        )
        material_match = re.search(
            r"material:\s*\n\s*uri:\s*(assets/scenes/generated/finite_room/test_neutral_room_unlit\.material)",
            scene_text,
        )
        self.assertIsNotNone(mesh_match, "finite room mesh URI is missing")
        self.assertIsNotNone(material_match, "finite room material URI is missing")

        mesh_path = self.source_dir / mesh_match.group(1)
        material_path = self.source_dir / material_match.group(1)
        self.assertTrue(mesh_path.is_file(), f"missing finite room mesh: {mesh_path}")
        self.assertTrue(
            material_path.is_file(), f"missing finite room material: {material_path}"
        )

        material_text = material_path.read_text(encoding="utf-8")
        texture_match = re.search(
            r"uri:\s*(textures/test_neutral_room_srgb\.png)", material_text
        )
        self.assertIsNotNone(texture_match, "finite room texture URI is missing")
        texture_path = material_path.parent / texture_match.group(1)
        self.assertTrue(
            texture_path.is_file(), f"missing finite room texture: {texture_path}"
        )

    def _run_generator(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(
                    self.source_dir
                    / "scripts"
                    / "assets"
                    / "generate_finite_skybox_room.py"
                ),
                *args,
            ],
            cwd=self.source_dir,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )


if __name__ == "__main__":
    unittest.main()
