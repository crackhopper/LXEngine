#!/usr/bin/env python3
"""Convert a single glTF metallic-roughness material to LXEngine standard-pbr.

The tool is intentionally narrow for REQ-073-c: it emits a Material v2
standard-pbr contract file plus a scene that binds the original glTF mesh to
that explicit material. Unsupported glTF shapes fail instead of falling back to
legacy `source: gltf` or `assets/materials/pbr.material`.
"""

from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path
from typing import Any


STANDARD_PBR_SOURCE = (
    "assets://shaders/glsl/common/materials/standard_pbr.contract.glsl"
)


def path_posix(path: Path) -> str:
    return path.as_posix()


def require_repo_relative(path: Path, repo_root: Path, label: str) -> str:
    resolved_path = path.resolve()
    resolved_root = repo_root.resolve()
    try:
        return path_posix(resolved_path.relative_to(resolved_root))
    except ValueError as error:
        raise ValueError(
            f"{label} must be inside repo root: path={resolved_path} "
            f"repoRoot={resolved_root}"
        ) from error


def relative_uri(owner_file: Path, target: Path) -> str:
    return os.path.relpath(target.resolve(), owner_file.parent.resolve()).replace(
        os.sep, "/"
    )


def format_float(value: float) -> str:
    text = f"{float(value):.9g}"
    if "e" not in text and "." not in text:
        text += ".0"
    return text


def format_vec(values: list[float]) -> str:
    return "[" + ", ".join(format_float(value) for value in values) + "]"


def snake_case_stem(stem: str) -> str:
    separated = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", stem)
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "_", separated).strip("_.-")
    return cleaned.lower() or "material"


def require_list(doc: dict[str, Any], key: str) -> list[Any]:
    value = doc.get(key)
    if not isinstance(value, list):
        raise ValueError(f"glTF field '{key}' must be a list")
    return value


def require_index(items: list[Any], index: Any, label: str) -> Any:
    if not isinstance(index, int):
        raise ValueError(f"{label} index must be an integer")
    if index < 0 or index >= len(items):
        raise ValueError(f"{label} index out of range: {index}")
    return items[index]


def read_gltf_json(gltf_path: Path) -> dict[str, Any]:
    if not gltf_path.exists():
        raise ValueError(f"glTF file not found: {gltf_path}")
    if gltf_path.suffix.lower() != ".gltf":
        raise ValueError("REQ-073-c converter accepts JSON .gltf files only")
    with gltf_path.open("r", encoding="utf-8") as stream:
        doc = json.load(stream)
    if not isinstance(doc, dict):
        raise ValueError("glTF root must be a JSON object")
    return doc


def texture_image_uri(doc: dict[str, Any], texture_info: Any, label: str) -> str:
    if texture_info is None:
        return ""
    if not isinstance(texture_info, dict):
        raise ValueError(f"{label} texture info must be an object")
    textures = require_list(doc, "textures")
    images = require_list(doc, "images")
    texture = require_index(textures, texture_info.get("index"), label)
    if not isinstance(texture, dict):
        raise ValueError(f"{label} texture entry must be an object")
    image = require_index(images, texture.get("source"), label)
    if not isinstance(image, dict):
        raise ValueError(f"{label} image entry must be an object")
    uri = image.get("uri")
    if not isinstance(uri, str) or not uri:
        raise ValueError(
            f"{label} must reference image.uri; bufferView images are unsupported"
        )
    return uri


def numeric_list(value: Any, count: int, default: list[float], label: str) -> list[float]:
    if value is None:
        return list(default)
    if not isinstance(value, list) or len(value) != count:
        raise ValueError(f"{label} must be an array of {count} numbers")
    result: list[float] = []
    for item in value:
        if not isinstance(item, int | float):
            raise ValueError(f"{label} must contain only numbers")
        result.append(float(item))
    return result


def numeric_value(value: Any, default: float, label: str) -> float:
    if value is None:
        return default
    if not isinstance(value, int | float):
        raise ValueError(f"{label} must be a number")
    return float(value)


def alpha_mode(value: Any) -> str:
    if value is None:
        return "OPAQUE"
    if value not in {"OPAQUE", "MASK", "BLEND"}:
        raise ValueError(f"unsupported glTF alphaMode: {value}")
    return str(value)


def material_doc_from_gltf(doc: dict[str, Any]) -> dict[str, Any]:
    materials = require_list(doc, "materials")
    if len(materials) != 1:
        raise ValueError(
            "REQ-073-c glTF material converter requires exactly one material"
        )
    material = materials[0]
    if not isinstance(material, dict):
        raise ValueError("glTF material entry must be an object")

    pbr = material.get("pbrMetallicRoughness", {})
    if pbr is None:
        pbr = {}
    if not isinstance(pbr, dict):
        raise ValueError("pbrMetallicRoughness must be an object")

    base_color_factor = numeric_list(
        pbr.get("baseColorFactor"), 4, [1.0, 1.0, 1.0, 1.0], "baseColorFactor"
    )
    emissive_factor = numeric_list(
        material.get("emissiveFactor"), 3, [0.0, 0.0, 0.0], "emissiveFactor"
    )
    mode = alpha_mode(material.get("alphaMode"))

    return {
        "baseColor": base_color_factor[:3],
        "metallic": numeric_value(pbr.get("metallicFactor"), 1.0, "metallicFactor"),
        "roughness": numeric_value(
            pbr.get("roughnessFactor"), 1.0, "roughnessFactor"
        ),
        "emissive": emissive_factor,
        "alphaMode": mode,
        "alphaCutoff": numeric_value(
            material.get("alphaCutoff"), 0.5, "alphaCutoff"
        ),
        "textures": {
            "baseColorTexture": texture_image_uri(
                doc, pbr.get("baseColorTexture"), "baseColorTexture"
            ),
            "metallicRoughnessTexture": texture_image_uri(
                doc,
                pbr.get("metallicRoughnessTexture"),
                "metallicRoughnessTexture",
            ),
            "normalTexture": texture_image_uri(
                doc, material.get("normalTexture"), "normalTexture"
            ),
            "occlusionTexture": texture_image_uri(
                doc, material.get("occlusionTexture"), "occlusionTexture"
            ),
            "emissiveTexture": texture_image_uri(
                doc, material.get("emissiveTexture"), "emissiveTexture"
            ),
        },
    }


def write_standard_pbr_material(
    path: Path, material_doc: dict[str, Any], gltf_dir: Path
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    textures: dict[str, str] = material_doc["textures"]

    lines = [
        "schema: lxe.material.v2",
        "bsdf:",
        "  type: standard-pbr",
        f"  source: {STANDARD_PBR_SOURCE}",
        "  parameters:",
        "    baseColor: { kind: rgb, value: "
        f"{format_vec(material_doc['baseColor'])} }}",
        "    metallic: { kind: float, value: "
        f"{format_float(material_doc['metallic'])} }}",
        "    roughness: { kind: float, value: "
        f"{format_float(material_doc['roughness'])} }}",
        "    emissive: { kind: rgb, value: "
        f"{format_vec(material_doc['emissive'])} }}",
        f"    alphaMode: {{ kind: string, value: {material_doc['alphaMode']} }}",
        "    alphaCutoff: { kind: float, value: "
        f"{format_float(material_doc['alphaCutoff'])} }}",
    ]
    for parameter_name in [
        "baseColorTexture",
        "metallicRoughnessTexture",
        "normalTexture",
        "occlusionTexture",
        "emissiveTexture",
    ]:
        uri = textures.get(parameter_name, "")
        if not uri:
            continue
        texture_path = gltf_dir / uri
        lines.append(
            f"    {parameter_name}: {{ kind: texture, valueType: rgb, uri: "
            f"{relative_uri(path, texture_path)} }}"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_scene(path: Path, gltf_uri: str, material_uri: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"""scene:
  name: Helmet Standard PBR
  gameplayCameraPath: /game_cam
  environment:
    enabled: false
    intensity: 0.0
    skyboxEnabled: false
  rendering:
    shadows: false
  defaultOutputProfile: preview
  outputProfiles:
    preview:
      camera: /game_cam
      width: 192
      height: 192
      outputFormat: exr-png
      outDir: artifacts/reference/damaged_helmet_direct
      backgroundColor: [0.0, 0.0, 0.0]
root:
  nodeName: scene_root
  name: ''
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  visibilityMask: 4294967295
  children:
    - nodeName: game_camera
      name: game_cam
      transform:
        translation: [3.19699, -0.14231, 2.17758]
        rotation: [0.891314, 0.00777838, 0.453303, -0.00395592]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      camera:
        type: perspective
        fovY: 35.0
        aspect: 1.0
        nearPlane: 0.1
        farPlane: 20.0
        cullingMask: 4294967295
    - nodeName: damaged_helmet
      name: damaged_helmet
      transform:
        translation: [0.0, 0.0, 0.0]
        rotation: [0.751125, 0.66016, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      mesh:
        uri: {gltf_uri}
      material:
        uri: {material_uri}
    - nodeName: compare_key_light
      name: compare_key_light
      transform:
        translation: [0.0, 4.0, 4.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      light:
        kind: Directional
        direction: [-0.101885, -0.74465, -0.659633]
        color: [1.0, 1.0, 1.0]
        intensity: 4.0
        shadowStrength: 0.0
        shadowDistance: 80.0
        shadowCascadeCount: 1
""",
        encoding="utf-8",
    )


def convert_gltf_material_scene(
    gltf_path: Path, out_root: Path, repo_root: Path, scene_path: Path | None = None
) -> dict[str, Any]:
    gltf_path = gltf_path.resolve()
    out_root = out_root.resolve()
    repo_root = repo_root.resolve()
    doc = read_gltf_json(gltf_path)
    material_doc = material_doc_from_gltf(doc)

    material_name = f"{snake_case_stem(gltf_path.stem)}_standard_pbr.material"
    material_path = out_root / "materials" / material_name
    if scene_path is None:
        scene_path = out_root / "helmet_standard_pbr.scene.yaml"
    else:
        scene_path = scene_path.resolve()

    write_standard_pbr_material(material_path, material_doc, gltf_path.parent)
    gltf_uri = require_repo_relative(gltf_path, repo_root, "glTF input")
    material_uri = require_repo_relative(material_path, repo_root, "material output")
    write_scene(scene_path, gltf_uri, material_uri)

    return {
        "schema": "lxe.gltfMaterialConversion.v1",
        "inputGltf": gltf_uri,
        "materialCount": 1,
        "outputMaterial": material_uri,
        "outputScene": require_repo_relative(scene_path, repo_root, "scene output"),
        "bsdfType": "standard-pbr",
        "bsdfSource": STANDARD_PBR_SOURCE,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gltf", required=True, type=Path, help="Input .gltf file")
    parser.add_argument(
        "--out", required=True, type=Path, help="Generated asset output directory"
    )
    parser.add_argument(
        "--scene",
        type=Path,
        default=None,
        help="Generated LXEngine scene path; defaults under --out",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path.cwd(),
        help="Repository root used for emitted asset URIs",
    )
    args = parser.parse_args(argv)
    manifest = convert_gltf_material_scene(
        gltf_path=args.gltf,
        out_root=args.out,
        repo_root=args.repo_root,
        scene_path=args.scene,
    )
    print(
        "Converted glTF material: "
        f"{manifest['bsdfType']} -> {manifest['outputMaterial']} "
        f"scene={manifest['outputScene']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
