#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image


KTX2_IDENTIFIER = b"\xABKTX 20\xBB\r\n\x1A\n"
VK_FORMAT_R16G16B16A16_SFLOAT = 97
FACE_NAMES = ("px", "nx", "py", "ny", "pz", "nz")
ATLAS_CELLS = {
    "px": (2, 1),
    "nx": (0, 1),
    "py": (1, 0),
    "ny": (1, 2),
    "pz": (1, 1),
    "nz": (3, 1),
}


@dataclass(frozen=True)
class Ktx2Header:
    vk_format: int
    type_size: int
    pixel_width: int
    pixel_height: int
    pixel_depth: int
    layer_count: int
    face_count: int
    level_count: int
    supercompression_scheme: int


@dataclass(frozen=True)
class Ktx2Level:
    byte_offset: int
    byte_length: int
    uncompressed_byte_length: int


@dataclass(frozen=True)
class Bounds:
    xmin: float
    xmax: float
    ymin: float
    ymax: float
    zmin: float
    zmax: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert an HDR KTX2 cubemap into a finite unlit room asset."
    )
    parser.add_argument("--input", required=True, type=Path)
    size = parser.add_mutually_exclusive_group(required=True)
    size.add_argument(
        "--bounds",
        type=float,
        nargs=6,
        metavar=("XMIN", "XMAX", "YMIN", "YMAX", "ZMIN", "ZMAX"),
    )
    size.add_argument(
        "--scale",
        type=float,
        help="half extent for a centered finite skybox cube",
    )
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--name", required=True)
    parser.add_argument(
        "--node-name",
        help="scene node name written into the generated finite skybox snippet",
    )
    parser.add_argument("--tone-map", required=True, choices=("aces", "reinhard"))
    parser.add_argument("--exposure", required=True, type=float)
    return parser.parse_args()


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def validate_args(args: argparse.Namespace) -> Bounds:
    if not args.input.is_file():
        fail(f"input file does not exist: {args.input}")
    if not args.name or any(part in args.name for part in ("/", "\\")):
        fail("--name must be a non-empty file stem without path separators")
    if not math.isfinite(args.exposure) or args.exposure < 0.0:
        fail("--exposure must be a finite non-negative number")
    if args.node_name is not None and (
        not args.node_name or any(part in args.node_name for part in ("/", "\\"))
    ):
        fail("--node-name must be a non-empty scene node name without path separators")

    if args.scale is not None:
        if not math.isfinite(args.scale) or args.scale <= 0.0:
            fail("--scale must be a finite positive number")
        bounds = Bounds(
            -args.scale,
            args.scale,
            -args.scale,
            args.scale,
            -args.scale,
            args.scale,
        )
    else:
        bounds = Bounds(*args.bounds)
    if not bounds.xmin < bounds.xmax:
        fail("--bounds requires xmin < xmax")
    if not bounds.ymin < bounds.ymax:
        fail("--bounds requires ymin < ymax")
    if not bounds.zmin < bounds.zmax:
        fail("--bounds requires zmin < zmax")
    return bounds


def read_u32(data: bytes, offset: int) -> int:
    if offset + 4 > len(data):
        fail("truncated KTX2 header")
    return struct.unpack_from("<I", data, offset)[0]


def read_u64(data: bytes, offset: int) -> int:
    if offset + 8 > len(data):
        fail("truncated KTX2 level index")
    return struct.unpack_from("<Q", data, offset)[0]


def parse_ktx2_header(data: bytes) -> Ktx2Header:
    if len(data) < 80:
        fail("KTX2 file is too small")
    if data[:12] != KTX2_IDENTIFIER:
        fail("input is not a KTX2 file")
    return Ktx2Header(
        vk_format=read_u32(data, 12),
        type_size=read_u32(data, 16),
        pixel_width=read_u32(data, 20),
        pixel_height=read_u32(data, 24),
        pixel_depth=read_u32(data, 28),
        layer_count=read_u32(data, 32),
        face_count=read_u32(data, 36),
        level_count=read_u32(data, 40),
        supercompression_scheme=read_u32(data, 44),
    )


def validate_supported_ktx2_cubemap(header: Ktx2Header) -> None:
    if header.vk_format != VK_FORMAT_R16G16B16A16_SFLOAT:
        fail("unsupported KTX2 format: expected VK_FORMAT_R16G16B16A16_SFLOAT")
    if header.type_size != 2:
        fail("unsupported KTX2 typeSize: expected 2")
    if header.pixel_width <= 0 or header.pixel_height <= 0:
        fail("KTX2 cubemap dimensions must be non-zero")
    if header.pixel_width != header.pixel_height:
        fail("KTX2 cubemap faces must be square")
    if header.pixel_depth != 0:
        fail("3D KTX2 textures are not supported")
    if header.layer_count != 0:
        fail("KTX2 texture arrays are not supported")
    if header.face_count != 6:
        fail("KTX2 cubemap must have exactly six faces")
    if header.level_count <= 0:
        fail("KTX2 cubemap must contain at least one mip level")
    if header.supercompression_scheme != 0:
        fail("supercompressed KTX2 cubemaps are not supported")


def parse_ktx2_levels(data: bytes, level_count: int) -> list[Ktx2Level]:
    levels: list[Ktx2Level] = []
    index_offset = 80
    index_size = 24
    if index_offset + level_count * index_size > len(data):
        fail("KTX2 level index is truncated")
    for level in range(level_count):
        offset = index_offset + level * index_size
        levels.append(
            Ktx2Level(
                byte_offset=read_u64(data, offset),
                byte_length=read_u64(data, offset + 8),
                uncompressed_byte_length=read_u64(data, offset + 16),
            )
        )
    return levels


def tone_map_channel(value: float, mode: str, exposure: float) -> int:
    x = max(value * exposure, 0.0)
    if mode == "aces":
        x = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)
    else:
        x = x / (1.0 + x)
    x = min(max(x, 0.0), 1.0)
    if x <= 0.0031308:
        encoded = 12.92 * x
    else:
        encoded = 1.055 * (x ** (1.0 / 2.4)) - 0.055
    return int(min(max(round(encoded * 255.0), 0), 255))


def decode_ktx2_first_mip_to_atlas(
    path: Path, tone_map: str, exposure: float
) -> Image.Image:
    data = path.read_bytes()
    header = parse_ktx2_header(data)
    validate_supported_ktx2_cubemap(header)
    levels = parse_ktx2_levels(data, header.level_count)
    level = levels[0]

    width = header.pixel_width
    height = header.pixel_height
    bytes_per_pixel = 8
    face_bytes = width * height * bytes_per_pixel
    expected_level_bytes = face_bytes * 6
    if level.byte_length != expected_level_bytes:
        fail("unsupported KTX2 first mip layout or compression")
    if level.uncompressed_byte_length != expected_level_bytes:
        fail("unsupported KTX2 first mip uncompressed length")
    if level.byte_offset > len(data) or expected_level_bytes > len(data) - level.byte_offset:
        fail("KTX2 first mip data is out of bounds")

    atlas = Image.new("RGB", (width * 4, height * 3), (0, 0, 0))
    row_half_count = width * 4
    row_format = f"<{row_half_count}e"
    row_byte_count = row_half_count * 2

    for face_index, face_name in enumerate(FACE_NAMES):
        cell_x, cell_y = ATLAS_CELLS[face_name]
        face_offset = level.byte_offset + face_index * face_bytes
        rows = []
        for y in range(height):
            row_offset = face_offset + y * row_byte_count
            rgba = struct.unpack_from(row_format, data, row_offset)
            row = bytearray(width * 3)
            for x in range(width):
                src = x * 4
                dst = x * 3
                row[dst] = tone_map_channel(rgba[src], tone_map, exposure)
                row[dst + 1] = tone_map_channel(rgba[src + 1], tone_map, exposure)
                row[dst + 2] = tone_map_channel(rgba[src + 2], tone_map, exposure)
            rows.append(bytes(row))
        face_image = Image.frombytes("RGB", (width, height), b"".join(rows))
        atlas.paste(face_image, (cell_x * width, cell_y * height))
    return atlas


def cell_uv(face_name: str, corner: str) -> tuple[float, float]:
    cell_x, cell_y = ATLAS_CELLS[face_name]
    u0 = cell_x / 4.0
    u1 = (cell_x + 1) / 4.0
    v_top = cell_y / 3.0
    v_bottom = (cell_y + 1) / 3.0
    corners = {
        "tl": (u0, v_top),
        "tr": (u1, v_top),
        "br": (u1, v_bottom),
        "bl": (u0, v_bottom),
    }
    return corners[corner]


def write_obj(path: Path, name: str, bounds: Bounds) -> None:
    x0, x1 = bounds.xmin, bounds.xmax
    y0, y1 = bounds.ymin, bounds.ymax
    z0, z1 = bounds.zmin, bounds.zmax
    faces = [
        ("px", [(x1, y1, z1), (x1, y1, z0), (x1, y0, z0), (x1, y0, z1)]),
        ("nx", [(x0, y1, z0), (x0, y1, z1), (x0, y0, z1), (x0, y0, z0)]),
        ("py", [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)]),
        ("ny", [(x0, y0, z1), (x1, y0, z1), (x1, y0, z0), (x0, y0, z0)]),
        ("pz", [(x0, y1, z1), (x1, y1, z1), (x1, y0, z1), (x0, y0, z1)]),
        ("nz", [(x1, y1, z0), (x0, y1, z0), (x0, y0, z0), (x1, y0, z0)]),
    ]

    lines = [f"o {name}"]
    vertices: list[tuple[float, float, float]] = []
    uvs: list[tuple[float, float]] = []
    triangles: list[tuple[tuple[int, int], tuple[int, int], tuple[int, int]]] = []
    for face_name, corners in faces:
        first_vertex = len(vertices) + 1
        first_uv = len(uvs) + 1
        vertices.extend(corners)
        uvs.extend(
            [
                cell_uv(face_name, "tl"),
                cell_uv(face_name, "tr"),
                cell_uv(face_name, "br"),
                cell_uv(face_name, "bl"),
            ]
        )
        triangles.append(
            (
                (first_vertex, first_uv),
                (first_vertex + 1, first_uv + 1),
                (first_vertex + 2, first_uv + 2),
            )
        )
        triangles.append(
            (
                (first_vertex, first_uv),
                (first_vertex + 2, first_uv + 2),
                (first_vertex + 3, first_uv + 3),
            )
        )

    for vertex in vertices:
        lines.append("v {:.9g} {:.9g} {:.9g}".format(*vertex))
    for uv in uvs:
        lines.append("vt {:.9g} {:.9g}".format(*uv))
    for a, b, c in triangles:
        lines.append(
            "f {}/{} {}/{} {}/{}".format(a[0], a[1], b[0], b[1], c[0], c[1])
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_material(path: Path, name: str) -> None:
    path.write_text(
        "\n".join(
            [
                "schema: lxe.material.v2",
                "renderClass: surface.opaque",
                "bsdf:",
                "  type: unlit-texture",
                "  source: assets://shaders/glsl/common/materials/unlit_texture.contract.glsl",
                "  parameters:",
                f"    baseColorTexture: {{ kind: texture, valueType: rgb, uri: textures/{name}_srgb.png }}",
                "",
            ]
        ),
        encoding="utf-8",
    )


def write_scene_snippet(path: Path, name: str, node_name: str) -> None:
    path.write_text(
        "\n".join(
            [
                f"- nodeName: {node_name}",
                f"  name: {node_name}",
                "  transform:",
                "    translation: [0.0, 0.0, 0.0]",
                "    rotation: [1.0, 0.0, 0.0, 0.0]",
                "    scale: [1.0, 1.0, 1.0]",
                "  visibilityMask: 4294967295",
                "  skybox:",
                "    mode: finite",
                "    mesh:",
                f"      uri: {name}.obj",
                "    material:",
                f"      uri: {name}_unlit.material",
                "",
            ]
        ),
        encoding="utf-8",
    )


def main() -> int:
    args = parse_args()
    bounds = validate_args(args)
    output_dir = args.output_dir
    texture_dir = output_dir / "textures"
    texture_dir.mkdir(parents=True, exist_ok=True)

    try:
        atlas = decode_ktx2_first_mip_to_atlas(args.input, args.tone_map, args.exposure)
    except OSError as exc:
        fail(f"failed to read input: {exc}")
    except struct.error as exc:
        fail(f"failed to decode KTX2 cubemap: {exc}")

    write_obj(output_dir / f"{args.name}.obj", args.name, bounds)
    write_material(output_dir / f"{args.name}_unlit.material", args.name)
    write_scene_snippet(
        output_dir / f"{args.name}.scene-snippet.yaml",
        args.name,
        args.node_name or f"finite_{args.name}",
    )
    atlas.save(texture_dir / f"{args.name}_srgb.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
