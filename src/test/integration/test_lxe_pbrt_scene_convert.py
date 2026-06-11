#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path, PurePosixPath


def write_binary_ply(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    header = (
        "ply\n"
        "format binary_little_endian 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property float nx\n"
        "property float ny\n"
        "property float nz\n"
        "element face 1\n"
        "property list uint8 int vertex_indices\n"
        "end_header\n"
    ).encode("ascii")
    vertices = b"".join(
        struct.pack("<ffffff", *values)
        for values in [
            (0.0, 0.0, 0.0, 0.0, 0.0, 1.0),
            (1.0, 0.0, 0.0, 0.0, 0.0, 1.0),
            (0.0, 1.0, 0.0, 0.0, 0.0, 1.0),
        ]
    )
    face = struct.pack("<Biii", 3, 0, 1, 2)
    path.write_bytes(header + vertices + face)


def write_binary_ply_with_uv(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    header = (
        "ply\n"
        "format binary_little_endian 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property float nx\n"
        "property float ny\n"
        "property float nz\n"
        "property float u\n"
        "property float v\n"
        "element face 1\n"
        "property list uint8 int vertex_indices\n"
        "end_header\n"
    ).encode("ascii")
    vertices = b"".join(
        struct.pack("<ffffffff", *values)
        for values in [
            (0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0),
            (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0),
            (0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0),
        ]
    )
    face = struct.pack("<Biii", 3, 0, 1, 2)
    path.write_bytes(header + vertices + face)


def write_fixture(root: Path) -> Path:
    (root / "geometry").mkdir(parents=True)
    (root / "textures").mkdir()
    (root / "spds").mkdir()
    (root / "bsdfs").mkdir()
    write_binary_ply(root / "geometry" / "mesh_00001.ply")
    write_binary_ply_with_uv(root / "geometry" / "mesh_00002.ply")
    write_binary_ply(root / "geometry" / "mesh_00003.ply")
    write_binary_ply(root / "geometry" / "mesh_00004.ply")
    (root / "textures" / "sky.exr").write_bytes(b"fake-exr")
    (root / "spds" / "Al.eta.spd").write_text("400 1.0\n", encoding="utf-8")
    (root / "spds" / "Al.k.spd").write_text("400 2.0\n", encoding="utf-8")
    (root / "bsdfs" / "leather.bsdf").write_bytes(b"fake-bsdf")
    (root / "BLENDSWAP_LICENSE.txt").write_text("license\n", encoding="utf-8")
    pbrt = root / "bmw-m6.pbrt"
    pbrt.write_text(
        """
Film "image" "integer xresolution" 1400 "integer yresolution" 1000
    "string filename" "bmw-m6.exr"
LookAt -11 .8 5   -2 -.5 0   0 1 0
Camera "perspective" "float fov" 30
Sampler "sobol" "integer pixelsamples" 4096
Integrator "path" "integer maxdepth" 10
WorldBegin
LightSource "infinite" "string mapname" "textures/sky.exr"
# Scene bounds: (-1, -2, -3) - (4, 5, 6)
MakeNamedMaterial "LogoSilver"
        "string type" [ "metal" ]
        "spectrum k" "spds/Al.k.spd"
        "spectrum eta" "spds/Al.eta.spd"
MakeNamedMaterial "LEATHER-white"
        "string type" [ "fourier" ]
        "string bsdffile" "bsdfs/leather.bsdf"
MakeNamedMaterial "LEATHER"
        "string type" "mix"
        "string namedmaterial1" "LEATHER-white"
        "string namedmaterial2" "LogoSilver"
        "rgb amount" [.2 .2 .2]
MakeNamedMaterial "floor"
        "string type" [ "matte" ]
        "rgb Kd" [.5 .5 .5]
MakeNamedMaterial "CarPaint"
        "float uroughness" [0.0005]
        "float vroughness" [0.00051]
        "string type" [ "substrate" ]
        "rgb Kd" [.4 .03 .03]
        "rgb Ks" [.3 .3 .3]
MakeNamedMaterial "WindscreenGlass"
        "string type" [ "glass" ]
# Name "wheel"
AttributeBegin
    NamedMaterial "LogoSilver"
    Shape "plymesh" "string filename" "geometry/mesh_00001.ply"
AttributeEnd
# Name "seat"
AttributeBegin
    NamedMaterial "LEATHER"
    Shape "plymesh" "string filename" "geometry/mesh_00002.ply"
AttributeEnd
# Name "Plane_Plane.001"
AttributeBegin
    NamedMaterial "floor"
    Shape "plymesh" "string filename" "geometry/mesh_00003.ply"
AttributeEnd
# Name "window"
AttributeBegin
    NamedMaterial "WindscreenGlass"
    Shape "plymesh" "string filename" "geometry/mesh_00004.ply"
AttributeEnd
""".strip()
        + "\n",
        encoding="utf-8",
    )
    return pbrt


class PbrtSceneConvertTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        parser = argparse.ArgumentParser()
        parser.add_argument("--source-dir", required=True)
        args, _ = parser.parse_known_args()
        cls.source_dir = Path(args.source_dir)
        tool_dir = cls.source_dir / "src" / "tools" / "lxe_pbrt_scene_convert"
        sys.path.insert(0, str(tool_dir))
        import lxe_pbrt_scene_convert as converter

        cls.converter = converter

    def test_conversion_preserves_source_material_and_writes_runtime_assets(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            fixture = tmp_path / "fixture" / "bmw-m6"
            input_scene = write_fixture(fixture)
            out_root = tmp_path / "repo" / "data" / "scenes" / "bmw-m6"
            scene_path = out_root / "pbrt_bmw_m6.scene.yaml"
            manifest = self.converter.convert_scene(
                input_path=input_scene,
                out_root=out_root,
                scene_path=scene_path,
                repo_root=tmp_path / "repo",
            )

            self.assertEqual(manifest["meshCount"], 4)
            self.assertEqual(manifest["materialCount"], 6)
            self.assertEqual(manifest["runtimeMaterialCount"], 6)
            self.assertEqual(manifest["sourceMaterialCount"], 6)

            scene_text = scene_path.read_text(encoding="utf-8")
            self.assertIn('"enabled": true', scene_text)
            self.assertIn('"skyboxEnabled": false', scene_text)
            self.assertIn('"realtimeRender"', scene_text)
            self.assertIn('"ibl": true', scene_text)
            self.assertIn('"alphaTransparency": true', scene_text)

            obj_text = (out_root / "meshes" / "mesh_00001.obj").read_text(
                encoding="utf-8"
            )
            self.assertIn("v 1 0 0", obj_text)
            self.assertIn("vn 0 0 1", obj_text)
            self.assertIn("f 1//1 2//2 3//3", obj_text)
            uv_obj_text = (out_root / "meshes" / "mesh_00002.obj").read_text(
                encoding="utf-8"
            )
            self.assertIn("vt 1 0", uv_obj_text)
            self.assertIn("f 1/1/1 2/2/2 3/3/3", uv_obj_text)

            runtime_material_text = (
                out_root
                / "materials"
                / "runtime-pbr-approx"
                / "LogoSilver.material"
            ).read_text(encoding="utf-8")
            self.assertIn('"schema": "lxe.material.v2"', runtime_material_text)
            self.assertIn('"bsdf":', runtime_material_text)
            self.assertIn('"type": "metal"', runtime_material_text)
            self.assertIn('"eta":', runtime_material_text)
            self.assertIn('"kind": "spectrum"', runtime_material_text)
            self.assertIn('"uri": "spds/Al.eta.spd"', runtime_material_text)
            self.assertNotIn(
                '"pbrtMaterialParameterSources":', runtime_material_text
            )
            self.assertNotIn('"shader":', runtime_material_text)
            self.assertNotIn('"variants":', runtime_material_text)
            self.assertNotIn('"removedDefaultFlow":', runtime_material_text)
            self.assertNotIn('"techniques":', runtime_material_text)
            self.assertNotIn('"resources":', runtime_material_text)
            self.assertNotIn("MaterialUBO.", runtime_material_text)

            car_paint_text = (
                out_root
                / "materials"
                / "runtime-pbr-approx"
                / "CarPaint.material"
            ).read_text(encoding="utf-8")
            self.assertIn('"type": "substrate"', car_paint_text)
            self.assertIn('"Kd":', car_paint_text)
            self.assertIn('"Ks":', car_paint_text)
            self.assertIn('"uroughness":', car_paint_text)
            self.assertIn('"vroughness":', car_paint_text)
            self.assertNotIn("MaterialUBO.", car_paint_text)
            self.assertNotIn('"shader":', car_paint_text)

            glass_material_text = (
                out_root
                / "materials"
                / "runtime-pbr-approx"
                / "WindscreenGlass.material"
            ).read_text(encoding="utf-8")
            self.assertIn('"Kr":', glass_material_text)
            self.assertIn('"kind": "rgb"', glass_material_text)
            self.assertIn('"eta":', glass_material_text)
            self.assertNotIn('"Kr": "pbrt-default"', glass_material_text)
            self.assertNotIn('"eta": "pbrt-default"', glass_material_text)
            self.assertNotIn('"renderState":', glass_material_text)
            self.assertNotIn("MaterialUBO.", glass_material_text)

            runtime_mix_material_text = (
                out_root
                / "materials"
                / "runtime-pbr-approx"
                / "LEATHER.material"
            ).read_text(encoding="utf-8")
            self.assertIn('"type": "mix"', runtime_mix_material_text)
            self.assertIn('"namedmaterial1":', runtime_mix_material_text)
            self.assertIn('"kind": "materialRef"', runtime_mix_material_text)
            self.assertIn('"uri": "LEATHER-white.material"', runtime_mix_material_text)
            self.assertIn('"uri": "LogoSilver.material"', runtime_mix_material_text)
            owner_material_uri = PurePosixPath(
                "materials/runtime-pbr-approx/LEATHER.material"
            )
            logo_ref_uri = PurePosixPath("LogoSilver.material")
            self.assertEqual(
                (owner_material_uri.parent / logo_ref_uri).as_posix(),
                "materials/runtime-pbr-approx/LogoSilver.material",
            )
            self.assertNotIn('"uri": "named:', runtime_mix_material_text)

            source_material_text = (
                out_root
                / "materials"
                / "pbrt-source"
                / "LogoSilver.pbrt-material.yaml"
            ).read_text(encoding="utf-8")
            self.assertIn(
                '"schema": "lxe.pbrtMaterialSource.v1"', source_material_text
            )
            self.assertIn('"pbrtType": "metal"', source_material_text)
            self.assertIn('"eta":', source_material_text)
            self.assertIn('"value": "spds/Al.eta.spd"', source_material_text)
            self.assertIn('"k":', source_material_text)
            self.assertIn('"value": "spds/Al.k.spd"', source_material_text)

            mix_material_text = (
                out_root
                / "materials"
                / "pbrt-source"
                / "LEATHER.pbrt-material.yaml"
            ).read_text(encoding="utf-8")
            self.assertIn(
                '"namedMaterialRefs": ["LEATHER-white", "LogoSilver"]',
                mix_material_text,
            )
            self.assertIn('"amount":', mix_material_text)
            self.assertIn('"value": [0.2, 0.2, 0.2]', mix_material_text)

            self.assertIn('"nodeName": "pbrt_runtime_key_light"', scene_text)
            self.assertIn('"nodeName": "bmw_m6_car"', scene_text)
            self.assertIn('"name": "BMW_M6_Car"', scene_text)
            self.assertIn('"name": "Plane_Plane.001"', scene_text)
            self.assertIn('"tag": "realtime-pbr"', scene_text)
            self.assertIn('"pbrtSourceMaterialUri":', scene_text)

            manifest_path = out_root / "pbrt_bmw_m6.converted.json"
            manifest_doc = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest_doc["sourceSceneBounds"], [[-1.0, -2.0, -3.0], [4.0, 5.0, 6.0]])
            self.assertEqual(
                manifest_doc["materialParameterSources"]["WindscreenGlass"]["eta"],
                "pbrt-default",
            )
            self.assertEqual(
                manifest_doc["materialParameterSources"]["LogoSilver"]["eta"],
                "explicit",
            )
            self.assertTrue((out_root / "pbrt_bmw_m6.conversion.md").exists())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
