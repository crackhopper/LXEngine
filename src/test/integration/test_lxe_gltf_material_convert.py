#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


class GltfMaterialConvertTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        parser = argparse.ArgumentParser()
        parser.add_argument("--source-dir", required=True)
        args, _ = parser.parse_known_args()
        cls.source_dir = Path(args.source_dir)
        tool_dir = cls.source_dir / "src" / "tools" / "lxe_gltf_material_convert"
        sys.path.insert(0, str(tool_dir))
        import lxe_gltf_material_convert as converter

        cls.converter = converter

    def test_damaged_helmet_source_is_single_standard_pbr_material(self) -> None:
        gltf_path = (
            self.source_dir
            / "assets"
            / "models"
            / "damaged_helmet"
            / "DamagedHelmet.gltf"
        )
        doc = json.loads(gltf_path.read_text(encoding="utf-8"))

        self.assertEqual(len(doc["materials"]), 1)
        self.assertEqual(len(doc["meshes"]), 1)
        primitives = doc["meshes"][0]["primitives"]
        self.assertEqual(len(primitives), 1)
        self.assertEqual(primitives[0]["material"], 0)
        self.assertEqual(
            sorted(primitives[0]["attributes"].keys()),
            ["NORMAL", "POSITION", "TEXCOORD_0"],
        )

        material = doc["materials"][0]
        self.assertEqual(material["name"], "Material_MR")
        pbr = material["pbrMetallicRoughness"]
        self.assertIn("baseColorTexture", pbr)
        self.assertIn("metallicRoughnessTexture", pbr)
        self.assertIn("normalTexture", material)
        self.assertIn("occlusionTexture", material)
        self.assertIn("emissiveTexture", material)

        images = [image["uri"] for image in doc["images"]]
        self.assertEqual(
            images,
            [
                "Default_albedo.jpg",
                "Default_metalRoughness.jpg",
                "Default_emissive.jpg",
                "Default_AO.jpg",
                "Default_normal.jpg",
            ],
        )

    def test_damaged_helmet_conversion_writes_standard_pbr_scene(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo_root = Path(tmp) / "repo"
            helmet_src = self.source_dir / "assets" / "models" / "damaged_helmet"
            helmet_dst = repo_root / "assets" / "models" / "damaged_helmet"
            helmet_dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(helmet_src, helmet_dst)
            out_root = repo_root / "assets" / "scenes" / "generated"
            gltf_path = helmet_dst / "DamagedHelmet.gltf"

            manifest = self.converter.convert_gltf_material_scene(
                gltf_path=gltf_path,
                out_root=out_root,
                repo_root=repo_root,
            )

            material_path = out_root / "materials" / "damaged_helmet_standard_pbr.material"
            scene_path = out_root / "helmet_standard_pbr.scene.yaml"
            self.assertEqual(manifest["materialCount"], 1)
            self.assertEqual(manifest["outputMaterial"], "assets/scenes/generated/materials/damaged_helmet_standard_pbr.material")
            self.assertEqual(manifest["outputScene"], "assets/scenes/generated/helmet_standard_pbr.scene.yaml")
            self.assertTrue(material_path.exists())
            self.assertTrue(scene_path.exists())

            material_text = material_path.read_text(encoding="utf-8")
            self.assertIn("schema: lxe.material.v2", material_text)
            self.assertIn("type: standard-pbr", material_text)
            self.assertIn(
                "source: assets://shaders/glsl/common/materials/standard_pbr.contract.glsl",
                material_text,
            )
            self.assertIn("baseColor: { kind: rgb, value: [1.0, 1.0, 1.0] }", material_text)
            self.assertIn("metallic: { kind: float, value: 1.0 }", material_text)
            self.assertIn("roughness: { kind: float, value: 1.0 }", material_text)
            self.assertIn("emissive: { kind: rgb, value: [1.0, 1.0, 1.0] }", material_text)
            self.assertIn("alphaMode: { kind: string, value: OPAQUE }", material_text)
            self.assertIn("alphaCutoff: { kind: float, value: 0.5 }", material_text)
            for texture in [
                "Default_albedo.jpg",
                "Default_metalRoughness.jpg",
                "Default_normal.jpg",
                "Default_AO.jpg",
                "Default_emissive.jpg",
            ]:
                self.assertIn(texture, material_text)
            self.assertNotIn("assets/materials/pbr.material", material_text)
            self.assertNotIn("source: gltf", material_text)

            scene_text = scene_path.read_text(encoding="utf-8")
            self.assertIn("name: Helmet Standard PBR", scene_text)
            self.assertIn("enabled: false", scene_text)
            self.assertIn("skyboxEnabled: false", scene_text)
            self.assertIn("shadows: false", scene_text)
            self.assertIn("outDir: artifacts/reference/damaged_helmet_direct", scene_text)
            self.assertIn("translation: [-1.2, 0.0, 3.2]", scene_text)
            self.assertIn("rotation: [0.983954, 0.0, -0.178425, 0.0]", scene_text)
            self.assertIn("fovY: 35.0", scene_text)
            self.assertIn("kind: Directional", scene_text)
            self.assertIn("direction: [-0.45, -0.75, -0.48]", scene_text)
            self.assertIn("intensity: 4.0", scene_text)
            self.assertNotIn("nodeName: ground", scene_text)
            self.assertNotIn("name: ground", scene_text)
            self.assertNotIn("ground_mesh", scene_text)
            self.assertNotIn("builtin://lxe_editor/primitives/plane", scene_text)
            self.assertIn("uri: assets/models/damaged_helmet/DamagedHelmet.gltf", scene_text)
            self.assertIn(
                "uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material",
                scene_text,
            )
            self.assertNotIn("assets/materials/pbr.material", scene_text)
            self.assertNotIn("source: gltf", scene_text)


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1])
