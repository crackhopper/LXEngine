#!/usr/bin/env python3
"""Convert the PBRT v3 BMW M6 scene into LXEngine assets.

The converter emits both runtime-supported approximation assets and
source-preserving PBRT material assets. The latter intentionally records data
the current renderer cannot consume yet.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DIRECTIVES = {
    "Film",
    "LookAt",
    "Camera",
    "Sampler",
    "Integrator",
    "WorldBegin",
    "WorldEnd",
    "LightSource",
    "MakeNamedMaterial",
    "NamedMaterial",
    "Shape",
    "AttributeBegin",
    "AttributeEnd",
    "AreaLightSource",
    "Texture",
    "Transform",
    "Translate",
    "Scale",
    "Rotate",
    "ConcatTransform",
}


@dataclass
class Token:
    value: str
    line: int
    quoted: bool = False


@dataclass
class PbrtParameter:
    raw_key: str
    value: Any
    line: int

    @property
    def kind(self) -> str:
        parts = self.raw_key.split(maxsplit=1)
        return parts[0] if parts else "unknown"

    @property
    def name(self) -> str:
        parts = self.raw_key.split(maxsplit=1)
        return parts[1] if len(parts) == 2 else self.raw_key


@dataclass
class PbrtMaterial:
    name: str
    parameters: list[PbrtParameter] = field(default_factory=list)
    start_line: int = 0
    end_line: int = 0

    def parameter(self, name: str) -> PbrtParameter | None:
        for param in self.parameters:
            if param.name == name:
                return param
        return None

    def value(self, name: str, default: Any = None) -> Any:
        param = self.parameter(name)
        return default if param is None else param.value

    def pbrt_type(self) -> str:
        value = self.value("type", "matte")
        if isinstance(value, list) and value:
            return str(value[0])
        return str(value)


@dataclass
class PbrtShape:
    index: int
    shape_type: str
    filename: str
    material: str
    line: int
    source_name: str = ""


@dataclass
class PbrtScene:
    input_path: Path
    film_type: str = "image"
    film: dict[str, Any] = field(default_factory=dict)
    camera_type: str = "perspective"
    camera: dict[str, Any] = field(default_factory=dict)
    look_at: dict[str, list[float]] = field(default_factory=dict)
    sampler_type: str = ""
    sampler: dict[str, Any] = field(default_factory=dict)
    integrator_type: str = ""
    integrator: dict[str, Any] = field(default_factory=dict)
    light_type: str = ""
    light: dict[str, Any] = field(default_factory=dict)
    scene_bounds: list[list[float]] | None = None
    materials: list[PbrtMaterial] = field(default_factory=list)
    shapes: list[PbrtShape] = field(default_factory=list)
    ignored_commented_directives: list[dict[str, Any]] = field(default_factory=list)


@dataclass
class PlyMesh:
    positions: list[tuple[float, float, float]]
    normals: list[tuple[float, float, float]]
    texcoords: list[tuple[float, float]]
    faces: list[tuple[int, int, int]]
    bounds_min: tuple[float, float, float]
    bounds_max: tuple[float, float, float]


def tokenize_pbrt(text: str) -> list[Token]:
    tokens: list[Token] = []
    line = 1
    i = 0
    while i < len(text):
        ch = text[i]
        if ch in " \t\r":
            i += 1
            continue
        if ch == "\n":
            line += 1
            i += 1
            continue
        if ch == "#":
            while i < len(text) and text[i] != "\n":
                i += 1
            continue
        if ch == '"':
            start_line = line
            i += 1
            buf: list[str] = []
            while i < len(text):
                if text[i] == "\\" and i + 1 < len(text):
                    buf.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                if text[i] == "\n":
                    line += 1
                buf.append(text[i])
                i += 1
            tokens.append(Token("".join(buf), start_line, True))
            continue
        if ch in "[]":
            tokens.append(Token(ch, line, False))
            i += 1
            continue
        start = i
        start_line = line
        while i < len(text) and text[i] not in " \t\r\n[]#":
            i += 1
        tokens.append(Token(text[start:i], start_line, False))
    return tokens


def parse_scalar(token: Token) -> Any:
    if token.quoted:
        return token.value
    try:
        if re.match(r"^[+-]?\d+$", token.value):
            return int(token.value)
        return float(token.value)
    except ValueError:
        return token.value


class PbrtParser:
    def __init__(self, path: Path):
        self.path = path
        self.text = path.read_text(encoding="utf-8", errors="replace")
        self.tokens = tokenize_pbrt(self.text)
        self.pos = 0
        self.scene = PbrtScene(input_path=path)
        self.comment_names = self._scan_comment_names()
        self.current_material = ""

    def parse(self) -> PbrtScene:
        self.scene.scene_bounds = self._scan_scene_bounds()
        self.scene.ignored_commented_directives = self._scan_commented_directives()
        while not self._at_end():
            token = self._advance()
            if token.value == "Film":
                self.scene.film_type = str(self._expect_value("Film type"))
                self.scene.film = self._parse_params_until_directive()
            elif token.value == "LookAt":
                nums = [float(self._expect_value("LookAt")) for _ in range(9)]
                self.scene.look_at = {
                    "eye": nums[0:3],
                    "target": nums[3:6],
                    "up": nums[6:9],
                }
            elif token.value == "Camera":
                self.scene.camera_type = str(self._expect_value("Camera type"))
                self.scene.camera = self._parse_params_until_directive()
            elif token.value == "Sampler":
                self.scene.sampler_type = str(self._expect_value("Sampler type"))
                self.scene.sampler = self._parse_params_until_directive()
            elif token.value == "Integrator":
                self.scene.integrator_type = str(
                    self._expect_value("Integrator type")
                )
                self.scene.integrator = self._parse_params_until_directive()
            elif token.value == "LightSource":
                self.scene.light_type = str(self._expect_value("LightSource type"))
                self.scene.light = self._parse_params_until_directive()
            elif token.value == "MakeNamedMaterial":
                self.scene.materials.append(self._parse_material(token.line))
            elif token.value == "NamedMaterial":
                self.current_material = str(self._expect_value("NamedMaterial name"))
            elif token.value == "Shape":
                self.scene.shapes.append(self._parse_shape(token.line))
            elif token.value in {"WorldBegin", "WorldEnd", "AttributeBegin", "AttributeEnd"}:
                continue
            elif token.value in DIRECTIVES:
                raise ValueError(
                    f"Unsupported PBRT directive at line {token.line}: {token.value}"
                )
            else:
                raise ValueError(f"Unexpected token at line {token.line}: {token.value}")
        return self.scene

    def _at_end(self) -> bool:
        return self.pos >= len(self.tokens)

    def _peek(self) -> Token | None:
        if self._at_end():
            return None
        return self.tokens[self.pos]

    def _advance(self) -> Token:
        token = self.tokens[self.pos]
        self.pos += 1
        return token

    def _expect_value(self, context: str) -> Any:
        if self._at_end():
            raise ValueError(f"Unexpected EOF while reading {context}")
        token = self._advance()
        return parse_scalar(token)

    def _parse_params_until_directive(self) -> dict[str, Any]:
        params: dict[str, Any] = {}
        while not self._at_end():
            token = self._peek()
            if token and not token.quoted and token.value in DIRECTIVES:
                break
            key_token = self._advance()
            if not key_token.quoted:
                raise ValueError(
                    f"Expected PBRT parameter key at line {key_token.line}, "
                    f"got {key_token.value}"
                )
            params[key_token.value] = self._parse_value()
        return params

    def _parse_material_params_until_directive(self) -> list[PbrtParameter]:
        params: list[PbrtParameter] = []
        while not self._at_end():
            token = self._peek()
            if token and not token.quoted and token.value in DIRECTIVES:
                break
            key_token = self._advance()
            if not key_token.quoted:
                raise ValueError(
                    f"Expected material parameter key at line {key_token.line}, "
                    f"got {key_token.value}"
                )
            params.append(
                PbrtParameter(
                    raw_key=key_token.value,
                    value=self._parse_value(),
                    line=key_token.line,
                )
            )
        return params

    def _parse_value(self) -> Any:
        token = self._advance()
        if token.value != "[":
            return parse_scalar(token)
        values: list[Any] = []
        while not self._at_end():
            item = self._advance()
            if item.value == "]":
                break
            values.append(parse_scalar(item))
        return values

    def _parse_material(self, start_line: int) -> PbrtMaterial:
        name = str(self._expect_value("material name"))
        params = self._parse_material_params_until_directive()
        end_line = params[-1].line if params else start_line
        return PbrtMaterial(name=name, parameters=params, start_line=start_line, end_line=end_line)

    def _parse_shape(self, line: int) -> PbrtShape:
        shape_type = str(self._expect_value("shape type"))
        params = self._parse_params_until_directive()
        filename = params.get("string filename")
        if not isinstance(filename, str):
            raise ValueError(f"Shape at line {line} has no string filename")
        source_name = self._nearest_comment_name(line)
        return PbrtShape(
            index=len(self.scene.shapes) + 1,
            shape_type=shape_type,
            filename=filename,
            material=self.current_material,
            line=line,
            source_name=source_name,
        )

    def _scan_comment_names(self) -> list[tuple[int, str]]:
        names: list[tuple[int, str]] = []
        for line_no, line in enumerate(self.text.splitlines(), start=1):
            match = re.match(r"\s*#\s*Name\s+\"?(.+?)\"?\s*$", line)
            if match:
                names.append((line_no, match.group(1)))
        return names

    def _nearest_comment_name(self, line: int) -> str:
        candidates = [name for line_no, name in self.comment_names if line_no < line]
        return candidates[-1] if candidates else ""

    def _scan_scene_bounds(self) -> list[list[float]] | None:
        match = re.search(
            r"Scene bounds:\s*\(([^)]+)\)\s*-\s*\(([^)]+)\)",
            self.text,
        )
        if not match:
            return None
        return [
            [float(v.strip()) for v in match.group(1).split(",")],
            [float(v.strip()) for v in match.group(2).split(",")],
        ]

    def _scan_commented_directives(self) -> list[dict[str, Any]]:
        ignored: list[dict[str, Any]] = []
        pattern = re.compile(r"^\s*#\s*(AreaLightSource|Texture)\b(.*)$")
        for line_no, line in enumerate(self.text.splitlines(), start=1):
            match = pattern.match(line)
            if match:
                ignored.append(
                    {
                        "line": line_no,
                        "directive": match.group(1),
                        "text": match.group(0).strip(),
                    }
                )
        return ignored


def read_ply(path: Path) -> PlyMesh:
    with path.open("rb") as f:
        header_lines: list[str] = []
        while True:
            line = f.readline()
            if not line:
                raise ValueError(f"PLY missing end_header: {path}")
            decoded = line.decode("ascii", errors="strict").rstrip("\n")
            header_lines.append(decoded)
            if decoded == "end_header":
                break
        if header_lines[0] != "ply":
            raise ValueError(f"Not a PLY file: {path}")
        if "format binary_little_endian 1.0" not in header_lines:
            raise ValueError(f"Unsupported PLY format in {path}")
        vertex_count = 0
        face_count = 0
        vertex_properties: list[str] = []
        in_vertex = False
        for line in header_lines:
            parts = line.split()
            if parts[:2] == ["element", "vertex"]:
                vertex_count = int(parts[2])
                in_vertex = True
            elif parts[:2] == ["element", "face"]:
                face_count = int(parts[2])
                in_vertex = False
            elif in_vertex and parts[:2] == ["property", "float"]:
                vertex_properties.append(parts[2])
        if vertex_count <= 0:
            raise ValueError(f"PLY has no vertices: {path}")
        if not {"x", "y", "z", "nx", "ny", "nz"}.issubset(vertex_properties):
            raise ValueError(f"PLY missing required position/normal properties: {path}")
        vertex_stride = 4 * len(vertex_properties)
        positions: list[tuple[float, float, float]] = []
        normals: list[tuple[float, float, float]] = []
        texcoords: list[tuple[float, float]] = []
        for _ in range(vertex_count):
            data = f.read(vertex_stride)
            if len(data) != vertex_stride:
                raise ValueError(f"Unexpected EOF in PLY vertices: {path}")
            values = struct.unpack("<" + "f" * len(vertex_properties), data)
            fields = dict(zip(vertex_properties, values))
            positions.append((fields["x"], fields["y"], fields["z"]))
            normals.append((fields["nx"], fields["ny"], fields["nz"]))
            if "u" in fields and "v" in fields:
                texcoords.append((fields["u"], fields["v"]))
        faces: list[tuple[int, int, int]] = []
        for _ in range(face_count):
            count_data = f.read(1)
            if len(count_data) != 1:
                raise ValueError(f"Unexpected EOF in PLY faces: {path}")
            count = struct.unpack("<B", count_data)[0]
            raw = f.read(4 * count)
            if len(raw) != 4 * count:
                raise ValueError(f"Unexpected EOF in PLY face indices: {path}")
            indices = struct.unpack("<" + "i" * count, raw)
            if count != 3:
                raise ValueError(
                    f"Unsupported non-triangle face with {count} vertices in {path}"
                )
            faces.append((indices[0], indices[1], indices[2]))
    mins = tuple(min(p[i] for p in positions) for i in range(3))
    maxs = tuple(max(p[i] for p in positions) for i in range(3))
    return PlyMesh(
        positions=positions,
        normals=normals,
        texcoords=texcoords,
        faces=faces,
        bounds_min=mins,
        bounds_max=maxs,
    )


def write_obj(mesh: PlyMesh, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as out:
        out.write("# Generated by lxe_pbrt_scene_convert\n")
        for x, y, z in mesh.positions:
            out.write(f"v {x:.9g} {y:.9g} {z:.9g}\n")
        for u, v in mesh.texcoords:
            out.write(f"vt {u:.9g} {v:.9g}\n")
        for nx, ny, nz in mesh.normals:
            out.write(f"vn {nx:.9g} {ny:.9g} {nz:.9g}\n")
        for a, b, c in mesh.faces:
            if mesh.texcoords:
                out.write(
                    f"f {a + 1}/{a + 1}/{a + 1} "
                    f"{b + 1}/{b + 1}/{b + 1} "
                    f"{c + 1}/{c + 1}/{c + 1}\n"
                )
            else:
                out.write(
                    f"f {a + 1}//{a + 1} {b + 1}//{b + 1} {c + 1}//{c + 1}\n"
                )


def as_float_list(value: Any, fallback: list[float]) -> list[float]:
    if isinstance(value, list):
        return [float(v) for v in value]
    return fallback


def first_float(value: Any, fallback: float) -> float:
    if isinstance(value, list) and value:
        return float(value[0])
    if isinstance(value, (int, float)):
        return float(value)
    return fallback


def sanitize_filename(name: str) -> str:
    safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", name.strip())
    return safe or "unnamed"


def vec4_color(rgb: list[float]) -> list[float]:
    padded = list(rgb[:3])
    while len(padded) < 3:
        padded.append(1.0)
    return [round(v, 9) for v in padded] + [1.0]


def approximate_material(material: PbrtMaterial) -> tuple[dict[str, Any], list[str], str]:
    pbrt_type = material.pbrt_type()
    kd = as_float_list(material.value("Kd"), [0.8, 0.8, 0.8])
    ks = as_float_list(material.value("Ks"), [0.0, 0.0, 0.0])
    roughness = first_float(material.value("roughness"), 0.7)
    if pbrt_type == "substrate":
        roughness = (
            first_float(material.value("uroughness"), roughness)
            + first_float(material.value("vroughness"), roughness)
        ) * 0.5
    metallic = 0.0
    strategy = f"{pbrt_type}-to-pbr-approx"
    losses: list[str] = []
    if pbrt_type == "metal":
        metallic = 1.0
        kd = [0.8, 0.78, 0.72]
        roughness = 0.18
        losses.append("spectral eta/k approximated as RGB metallic material")
    elif pbrt_type == "glass":
        kd = [0.85, 0.95, 1.0]
        roughness = 0.02
        losses.append("PBRT glass transmission/refraction not represented by current PBR shader")
    elif pbrt_type == "fourier":
        kd = [0.75, 0.72, 0.68]
        roughness = 0.55
        losses.append("Fourier BSDF approximated as leather-colored dielectric")
    elif pbrt_type == "mix":
        amount = as_float_list(material.value("amount"), [0.5, 0.5, 0.5])
        kd = [max(0.0, min(1.0, v)) for v in amount[:3]]
        roughness = 0.65
        losses.append("PBRT mix material stores references in source YAML; runtime uses blended fallback")
    elif pbrt_type == "uber":
        specular = max(ks) if ks else 0.0
        roughness = max(roughness, 0.02)
        if material.value("Kt") not in (None, [0, 0, 0], [0.0, 0.0, 0.0]):
            losses.append("PBRT uber transmission Kt not represented by current PBR shader")
        if specular > 0.45:
            roughness = min(roughness, 0.35)
    elif pbrt_type == "substrate":
        losses.append("PBRT substrate layered diffuse/specular model approximated as PBR dielectric")
    roughness = max(0.0005, min(1.0, roughness))
    doc = {
        "shader": "pbr",
        "variants": {
            "HAS_METALLIC_ROUGHNESS": False,
            "HAS_NORMAL_MAP": False,
            "HAS_AO_MAP": False,
            "HAS_EMISSIVE_MAP": False,
            "HAS_IBL": True,
        },
        "passes": {
            "Forward": {
                "renderState": {
                    "cullMode": "Back",
                    "depthTest": True,
                    "depthWrite": True,
                    "blendEnable": pbrt_type == "glass",
                }
            },
            "OfflineRayTrace": {
                "shader": "offline_pbr_direct_ray",
                "stage": "compute",
            },
        },
        "parameters": {
            "MaterialUBO.baseColorFactor": vec4_color(kd),
            "MaterialUBO.metallicFactor": metallic,
            "MaterialUBO.roughnessFactor": roughness,
            "MaterialUBO.ao": 1.0,
        },
    }
    return doc, losses, strategy


def source_material_doc(
    material: PbrtMaterial,
    scene_dir: Path,
    out_root: Path,
    runtime_uri: str,
    losses: list[str],
    strategy: str,
) -> dict[str, Any]:
    resource_refs: list[dict[str, Any]] = []
    named_refs: list[str] = []
    params: dict[str, Any] = {}
    order: list[str] = []
    for param in material.parameters:
        params[param.name] = {
            "pbrtKey": param.raw_key,
            "type": param.kind,
            "value": param.value,
            "line": param.line,
        }
        order.append(param.name)
        if param.kind in {"spectrum", "string"} and isinstance(param.value, str):
            if param.name in {"eta", "k", "bsdffile", "filename"}:
                src = (scene_dir / param.value).resolve()
                dst = out_root / param.value
                resource_refs.append(
                    {
                        "parameter": param.name,
                        "input": str(src),
                        "output": path_posix(dst),
                    }
                )
        if param.name in {"namedmaterial1", "namedmaterial2"} and isinstance(param.value, str):
            named_refs.append(param.value)
    return {
        "schema": "lxe.pbrtMaterialSource.v1",
        "name": material.name,
        "pbrtType": material.pbrt_type(),
        "parameters": params,
        "parameterOrder": order,
        "resourceRefs": resource_refs,
        "namedMaterialRefs": named_refs,
        "sourceLocation": {
            "file": path_posix(scene_dir / "bmw-m6.pbrt"),
            "startLine": material.start_line,
            "endLine": material.end_line,
        },
        "runtimeApproximation": {
            "uri": runtime_uri,
            "strategy": strategy,
            "precisionLoss": losses,
        },
        "unsupportedByCurrentRenderer": losses,
    }


def path_posix(path: Path) -> str:
    return path.as_posix()


def rel_to_repo(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def yaml_scalar(value: Any) -> str:
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if math.isfinite(value):
            return repr(value)
        raise ValueError(f"non-finite float cannot be written to YAML: {value}")
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    raise TypeError(f"unsupported YAML scalar type: {type(value).__name__}")


def yaml_is_scalar(value: Any) -> bool:
    return value is None or isinstance(value, (bool, int, float, str))


def yaml_inline(value: Any) -> str:
    if yaml_is_scalar(value):
        return yaml_scalar(value)
    if isinstance(value, list) and all(yaml_is_scalar(item) for item in value):
        return "[" + ", ".join(yaml_scalar(item) for item in value) + "]"
    if isinstance(value, dict) and not value:
        return "{}"
    if isinstance(value, list) and not value:
        return "[]"
    raise TypeError("value cannot be emitted inline")


def yaml_emit(value: Any, indent: int = 0) -> list[str]:
    pad = " " * indent
    if yaml_is_scalar(value) or (isinstance(value, list) and not value) or (
        isinstance(value, dict) and not value
    ):
        return [pad + yaml_inline(value)]
    if isinstance(value, list):
        lines: list[str] = []
        for item in value:
            if yaml_is_scalar(item) or (
                isinstance(item, (list, dict)) and not item
            ) or (
                isinstance(item, list)
                and all(yaml_is_scalar(child) for child in item)
            ):
                lines.append(pad + "- " + yaml_inline(item))
            else:
                lines.append(pad + "-")
                lines.extend(yaml_emit(item, indent + 2))
        return lines
    if isinstance(value, dict):
        lines = []
        for key, item in value.items():
            key_text = yaml_scalar(str(key))
            if yaml_is_scalar(item) or (
                isinstance(item, (list, dict)) and not item
            ) or (
                isinstance(item, list)
                and all(yaml_is_scalar(child) for child in item)
            ):
                lines.append(pad + f"{key_text}: " + yaml_inline(item))
            else:
                lines.append(pad + f"{key_text}:")
                lines.extend(yaml_emit(item, indent + 2))
        return lines
    raise TypeError(f"unsupported YAML value type: {type(value).__name__}")


def yaml_write(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(yaml_emit(data)) + "\n", encoding="utf-8")


def compute_camera_transform(eye: list[float], target: list[float]) -> dict[str, Any]:
    # SceneDocument accepts eye/target/up under camera and derives the node
    # transform. Keep identity transform here so the source camera data remains
    # explicit and unambiguous in YAML.
    return {
        "translation": [round(v, 9) for v in eye],
        "rotation": [1.0, 0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
    }


def scene_yaml(
    scene: PbrtScene,
    repo_root: Path,
    out_root: Path,
    scene_path: Path,
    material_map: dict[str, dict[str, str]],
) -> dict[str, Any]:
    film_width = int(scene.film.get("integer xresolution", 1400))
    film_height = int(scene.film.get("integer yresolution", 1000))
    samples = int(scene.sampler.get("integer pixelsamples", 1))
    max_bounce = int(scene.integrator.get("integer maxdepth", 1))
    eye = scene.look_at.get("eye", [0.0, 0.0, 5.0])
    target = scene.look_at.get("target", [0.0, 0.0, 0.0])
    up = scene.look_at.get("up", [0.0, 1.0, 0.0])
    fov = float(scene.camera.get("float fov", 45.0))
    env_uri = rel_to_repo(out_root / "textures" / "sky.exr", repo_root)
    children: list[dict[str, Any]] = [
        {
            "nodeName": "pbrt_camera",
            "name": "pbrt_camera",
            "transform": compute_camera_transform(eye, target),
            "visibilityMask": 4294967295,
            "camera": {
                "eye": eye,
                "target": target,
                "up": up,
                "type": "perspective",
                "fovY": fov,
                "aspect": film_width / film_height,
                "nearPlane": 0.1,
                "farPlane": 1000.0,
                "cullingMask": 4294967295,
            },
        }
    ]
    children.append(
        {
            "nodeName": "pbrt_runtime_key_light",
            "name": "pbrt_runtime_key_light",
            "transform": {
                "translation": [0.0, 0.0, 0.0],
                "rotation": [1.0, 0.0, 0.0, 0.0],
                "scale": [1.0, 1.0, 1.0],
            },
            "visibilityMask": 4294967295,
            "light": {
                "kind": "Directional",
                "direction": [-0.35, -0.85, -0.4],
                "color": [1.0, 1.0, 1.0],
                "intensity": 3.0,
                "shadowStrength": 0.0,
                "shadowDistance": 80.0,
                "shadowCascadeCount": 1,
            },
            "offline": {
                "runtimeApproximationFor": "PBRT LightSource infinite textures/sky.exr",
                "note": "Current offline renderer needs explicit direct light until HDR environment lighting is implemented.",
            },
        }
    )
    for shape in scene.shapes:
        mesh_name = Path(shape.filename).name.replace(".ply", ".obj")
        mesh_uri = rel_to_repo(out_root / "meshes" / mesh_name, repo_root)
        mat = material_map[shape.material]
        node: dict[str, Any] = {
            "nodeName": f"pbrt_mesh_{shape.index:05d}",
            "name": shape.source_name or f"mesh_{shape.index:05d}",
            "transform": {
                "translation": [0.0, 0.0, 0.0],
                "rotation": [1.0, 0.0, 0.0, 0.0],
                "scale": [1.0, 1.0, 1.0],
            },
            "visibilityMask": 4294967295,
            "mesh": {"uri": mesh_uri},
            "materials": [
                {
                    "tag": "realtime-pbr",
                    "uri": mat["runtime"],
                    "offline": {
                        "pbrtSourceMaterialUri": mat["source"],
                        "pbrtMaterialName": shape.material,
                    },
                },
                {
                    "tag": "offline-pbrt-reference",
                    "uri": mat["runtime"],
                    "offline": {
                        "pbrtSourceMaterialUri": mat["source"],
                        "pbrtMaterialName": shape.material,
                        "runtimeApproximationUntilSupported": True,
                    },
                },
            ],
        }
        children.append(node)
    return {
        "scene": {
            "name": "PBRT BMW M6",
            "gameplayCameraPath": "/pbrt_camera",
            "environment": {
                "enabled": True,
                "hdrUri": env_uri,
                "intensity": 1.0,
                "skyboxEnabled": True,
            },
            "defaultOutputProfile": "pbrt-reference",
            "outputProfiles": {
                "pbrt-reference": {
                    "camera": "/pbrt_camera",
                    "width": film_width,
                    "height": film_height,
                    "materialTag": "realtime-pbr",
                    "outputFormat": "exr-png",
                    "outDir": "artifacts/pbrt/bmw-m6/realtime",
                    "backgroundColor": [0.0, 0.0, 0.0],
                },
                "offline-pbrt-reference": {
                    "camera": "/pbrt_camera",
                    "width": film_width,
                    "height": film_height,
                    "materialTag": "offline-pbrt-reference",
                    "outputFormat": "exr-png",
                    "outDir": "artifacts/pbrt/bmw-m6/offline",
                    "backgroundColor": [0.0, 0.0, 0.0],
                },
            },
            "offlineRender": {
                "integrator": "software-compute",
                "samples": samples,
                "maxBounce": max_bounce,
                "seed": 1,
                "profile": "offline-pbrt-reference",
                "materialTag": "offline-pbrt-reference",
                "compareMode": "shaded",
            },
        },
        "root": {
            "nodeName": "scene_root",
            "name": "",
            "transform": {
                "translation": [0.0, 0.0, 0.0],
                "rotation": [1.0, 0.0, 0.0, 0.0],
                "scale": [1.0, 1.0, 1.0],
            },
            "visibilityMask": 4294967295,
            "children": children,
        },
    }


def copy_if_exists(src: Path, dst: Path) -> dict[str, str] | None:
    if not src.exists():
        return None
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    return {"input": path_posix(src), "output": path_posix(dst)}


def write_conversion_doc(path: Path, manifest: dict[str, Any]) -> None:
    unsupported = manifest.get("unsupportedFeatures", [])
    lines = [
        "# PBRT BMW M6 Conversion Report",
        "",
        "## Outputs",
        "",
        f"- Scene: `{manifest['outputScene']}`",
        f"- Meshes: {manifest['meshCount']}",
        f"- Runtime materials: {manifest['runtimeMaterialCount']}",
        f"- PBRT source materials: {manifest['sourceMaterialCount']}",
        "",
        "## Current Renderer Input",
        "",
        "Use the generated scene file for current realtime/offline rendering. It references OBJ meshes, runtime PBR approximation materials, and the copied HDR environment.",
        "",
        "## Source-Preserving Input",
        "",
        "Each scene material binding contains `offline.pbrtSourceMaterialUri`. Those YAML files preserve PBRT material types, parameters, resource references, and source line ranges.",
        "",
        "## Precision Loss Today",
        "",
    ]
    if unsupported:
        for item in unsupported:
            lines.append(f"- `{item['material']}`: {item['reason']}")
    else:
        lines.append("- None reported.")
    lines.extend(
        [
            "",
            "## Future Full-Fidelity Rendering Route",
            "",
            "1. Add a renderer-side PBRT source material loader that consumes `lxe.pbrtMaterialSource.v1` YAML directly.",
            "2. Implement spectral metal support using preserved `eta`/`k` SPD resources.",
            "3. Implement PBRT glass/transmission and transparent shadow behavior for window and lamp meshes.",
            "4. Implement Fourier BSDF loading for `bsdfs/leather.bsdf` instead of using the PBR fallback.",
            "5. Implement substrate/clearcoat car paint, preserving separate diffuse/specular lobes and anisotropic roughness.",
            "6. Add environment importance sampling for `textures/sky.exr` so offline path tracing converges on the PBRT lighting setup.",
            "7. Switch the `offline-pbrt-reference` material tag from runtime approximation to source material consumption once those renderer modules exist.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def convert_scene(input_path: Path, out_root: Path, scene_path: Path, repo_root: Path) -> dict[str, Any]:
    parser = PbrtParser(input_path)
    scene = parser.parse()
    if scene.light_type != "infinite":
        raise ValueError("BMW M6 converter expects an infinite light source")
    out_root.mkdir(parents=True, exist_ok=True)
    scene_dir = input_path.parent
    material_map: dict[str, dict[str, str]] = {}
    unsupported: list[dict[str, Any]] = []
    for material in scene.materials:
        safe = sanitize_filename(material.name)
        runtime_path = out_root / "materials" / "runtime-pbr-approx" / f"{safe}.material"
        source_path = out_root / "materials" / "pbrt-source" / f"{safe}.pbrt-material.yaml"
        runtime_doc, losses, strategy = approximate_material(material)
        yaml_write(runtime_path, runtime_doc)
        runtime_uri = rel_to_repo(runtime_path, repo_root)
        source_uri = rel_to_repo(source_path, repo_root)
        source_doc = source_material_doc(
            material=material,
            scene_dir=scene_dir,
            out_root=out_root,
            runtime_uri=runtime_uri,
            losses=losses,
            strategy=strategy,
        )
        yaml_write(source_path, source_doc)
        material_map[material.name] = {"runtime": runtime_uri, "source": source_uri}
        for loss in losses:
            unsupported.append(
                {
                    "material": material.name,
                    "pbrtType": material.pbrt_type(),
                    "reason": loss,
                    "sourceMaterialUri": source_uri,
                }
            )
    mesh_records: list[dict[str, Any]] = []
    for shape in scene.shapes:
        src = scene_dir / shape.filename
        mesh = read_ply(src)
        out_name = Path(shape.filename).name.replace(".ply", ".obj")
        dst = out_root / "meshes" / out_name
        write_obj(mesh, dst)
        mesh_records.append(
            {
                "shapeIndex": shape.index,
                "sourceName": shape.source_name,
                "input": path_posix(src),
                "output": rel_to_repo(dst, repo_root),
                "vertexCount": len(mesh.positions),
                "texCoordCount": len(mesh.texcoords),
                "faceCount": len(mesh.faces),
                "bounds": {
                    "min": list(mesh.bounds_min),
                    "max": list(mesh.bounds_max),
                },
                "pbrtMaterial": shape.material,
            }
        )
    resources: list[dict[str, str]] = []
    for src_rel, dst_rel in [
        ("textures/sky.exr", "textures/sky.exr"),
        ("spds/Al.eta.spd", "spds/Al.eta.spd"),
        ("spds/Al.k.spd", "spds/Al.k.spd"),
        ("bsdfs/leather.bsdf", "bsdfs/leather.bsdf"),
        ("BLENDSWAP_LICENSE.txt", "licenses/BLENDSWAP_LICENSE.txt"),
    ]:
        copied = copy_if_exists(scene_dir / src_rel, out_root / dst_rel)
        if copied:
            copied["output"] = rel_to_repo(Path(copied["output"]), repo_root)
            resources.append(copied)
    scene_doc = scene_yaml(scene, repo_root, out_root, scene_path, material_map)
    yaml_write(scene_path, scene_doc)
    shape_materials = []
    for shape in scene.shapes:
        mapped = material_map[shape.material]
        shape_materials.append(
            {
                "shapeIndex": shape.index,
                "mesh": rel_to_repo(out_root / "meshes" / Path(shape.filename).name.replace(".ply", ".obj"), repo_root),
                "pbrtMaterial": shape.material,
                "runtimeMaterialUri": mapped["runtime"],
                "sourceMaterialUri": mapped["source"],
            }
        )
    manifest = {
        "schema": "lxe.pbrtSceneConversion.v1",
        "inputScene": path_posix(input_path),
        "sourceSceneBounds": scene.scene_bounds,
        "outputScene": rel_to_repo(scene_path, repo_root),
        "meshCount": len(scene.shapes),
        "materialCount": len(scene.materials),
        "runtimeMaterialCount": len(scene.materials),
        "sourceMaterialCount": len(scene.materials),
        "shapeMaterials": shape_materials,
        "meshes": mesh_records,
        "camera": {
            "eye": scene.look_at.get("eye"),
            "target": scene.look_at.get("target"),
            "up": scene.look_at.get("up"),
            "fov": scene.camera.get("float fov"),
        },
        "environment": {
            "input": path_posix(scene_dir / str(scene.light.get("string mapname", "textures/sky.exr"))),
            "output": rel_to_repo(out_root / "textures" / "sky.exr", repo_root),
        },
        "sourceResources": resources,
        "ignoredCommentedDirectives": scene.ignored_commented_directives,
        "unsupportedFeatures": unsupported,
        "runtimeApproximations": [
            {
                "feature": "PBRT infinite environment direct illumination",
                "approximation": "Generated pbrt_runtime_key_light directional light for current renderer visibility",
            }
        ],
    }
    manifest_path = out_root / "pbrt_bmw_m6.converted.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=False), encoding="utf-8")
    write_conversion_doc(out_root / "pbrt_bmw_m6.conversion.md", manifest)
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="PBRT scene file")
    parser.add_argument("--out", required=True, type=Path, help="Converted asset output directory")
    parser.add_argument("--scene", required=True, type=Path, help="Generated LXEngine scene YAML path")
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path.cwd(),
        help="Repository root used for relative asset URIs",
    )
    args = parser.parse_args(argv)
    manifest = convert_scene(
        input_path=args.input,
        out_root=args.out,
        scene_path=args.scene,
        repo_root=args.repo_root,
    )
    print(
        "Converted PBRT scene: "
        f"{manifest['meshCount']} meshes, "
        f"{manifest['materialCount']} materials -> {manifest['outputScene']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
