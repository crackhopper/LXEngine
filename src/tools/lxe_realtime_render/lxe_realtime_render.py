#!/usr/bin/env python3
"""Run a realtime render output profile through a local lxe_editor process."""

from __future__ import annotations

import argparse
import struct
import http.client
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import time
import zlib


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def quote_command_token(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def request_json(
    host: str,
    port: int,
    token: str,
    method: str,
    path: str,
    body: dict[str, object] | None = None,
    timeout: float = 5.0,
) -> dict[str, object]:
    payload = b""
    headers = {"Authorization": f"Bearer {token}"}
    if body is not None:
        payload = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
        headers["Content-Length"] = str(len(payload))

    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        conn.request(method, path, body=payload, headers=headers)
        response = conn.getresponse()
        data = response.read().decode("utf-8")
    finally:
        conn.close()

    if response.status < 200 or response.status >= 300:
        raise RuntimeError(f"{method} {path} returned {response.status}: {data}")
    return json.loads(data)


def wait_for_health(
    process: subprocess.Popen[object], host: str, port: int, timeout_sec: float
) -> None:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                f"lxe_editor exited before API became healthy: code {process.returncode}"
            )
        try:
            conn = http.client.HTTPConnection(host, port, timeout=1.0)
            conn.request("GET", "/health")
            response = conn.getresponse()
            response.read()
            conn.close()
            if response.status == 200:
                return
        except OSError:
            pass
        time.sleep(0.1)
    raise RuntimeError(f"lxe_editor API did not become healthy on {host}:{port}")


def port_accepts_connections(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.2):
            return True
    except OSError:
        return False


def find_free_port(host: str) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind((host, 0))
        return int(sock.getsockname()[1])


def wait_for_token(token_path: Path, timeout_sec: float) -> str:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        if token_path.exists():
            token = token_path.read_text(encoding="utf-8").strip()
            if token:
                return token
        time.sleep(0.1)
    raise RuntimeError(f"lxe_editor token file was not written: {token_path}")


def editor_command(
    host: str, port: int, token: str, line: str, timeout: float
) -> dict[str, object]:
    response = request_json(
        host, port, token, "POST", "/api/command", {"line": line}, timeout
    )
    if not response.get("ok", False):
        message = response.get("message") or response.get("error") or response
        raise RuntimeError(f"editor command failed: {line}: {message}")
    return response


def wait_until_profile_visible(
    host: str, port: int, token: str, profile: str, timeout_sec: float
) -> None:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        try:
            response = editor_command(
                host, port, token, "realtime-render ls", timeout=5.0
            )
            structured = response.get("structuredJson", "")
            if isinstance(structured, str):
                payload = json.loads(structured)
                profiles = payload.get("profiles", [])
                if any(item.get("name") == profile for item in profiles):
                    return
            if isinstance(structured, str) and f'"name":"{profile}"' in structured:
                return
        except Exception:
            pass
        time.sleep(0.2)
    raise RuntimeError(f"output profile did not become visible: {profile}")


def wait_until_scene_loaded(
    host: str, port: int, token: str, timeout_sec: float
) -> None:
    deadline = time.monotonic() + timeout_sec
    last_status = ""
    while time.monotonic() < deadline:
        try:
            response = editor_command(
                host, port, token, "scene status", timeout=5.0
            )
            structured = response.get("structuredJson", "")
            if isinstance(structured, str):
                last_status = structured
                payload = json.loads(structured)
                if (
                    payload.get("activeSceneLoaded") is True
                    and payload.get("sceneOpenPending") is False
                ):
                    return
        except Exception as exc:
            last_status = str(exc)
        time.sleep(0.2)
    raise RuntimeError(f"scene did not finish loading: {last_status}")


def terminate_process(process: subprocess.Popen[object], timeout_sec: float) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=timeout_sec)
        return
    except subprocess.TimeoutExpired:
        pass
    os.killpg(process.pid, signal.SIGKILL)
    process.wait(timeout=timeout_sec)


def stop_editor(
    process: subprocess.Popen[object],
    host: str,
    port: int,
    token: str | None,
    timeout_sec: float,
) -> None:
    if token and process.poll() is None:
        try:
            editor_command(host, port, token, "quit", timeout=2.0)
            process.wait(timeout=timeout_sec)
            return
        except Exception:
            pass
    terminate_process(process, timeout_sec=timeout_sec)


def resolved_result_path(root: Path, value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else root / path


def paeth_predictor(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def png_luminance_stats(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise RuntimeError(f"not a PNG file: {path}")

    pos = 8
    width = 0
    height = 0
    bit_depth = 0
    color_type = 0
    idat = bytearray()
    while pos + 8 <= len(data):
        length = int.from_bytes(data[pos : pos + 4], "big")
        chunk_type = data[pos + 4 : pos + 8]
        chunk_data = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, _ = struct.unpack(
                ">IIBBBBB", chunk_data
            )
        elif chunk_type == b"IDAT":
            idat.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    if width <= 0 or height <= 0:
        raise RuntimeError(f"PNG missing valid IHDR: {path}")
    if bit_depth != 8 or color_type not in (2, 6):
        raise RuntimeError(
            f"PNG stats only support 8-bit RGB/RGBA, got bitDepth={bit_depth} "
            f"colorType={color_type}: {path}"
        )

    bytes_per_pixel = 4 if color_type == 6 else 3
    row_size = width * bytes_per_pixel
    raw = zlib.decompress(bytes(idat))
    expected = (row_size + 1) * height
    if len(raw) != expected:
        raise RuntimeError(
            f"PNG decompressed size mismatch: expected={expected} got={len(raw)}"
        )

    previous = bytearray(row_size)
    lit_pixels = 0
    luminance_sum = 0.0
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        scanline = bytearray(raw[offset + 1 : offset + 1 + row_size])
        offset += row_size + 1
        for i in range(row_size):
            left = scanline[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
            up = previous[i]
            up_left = previous[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
            if filter_type == 0:
                recon = scanline[i]
            elif filter_type == 1:
                recon = (scanline[i] + left) & 0xFF
            elif filter_type == 2:
                recon = (scanline[i] + up) & 0xFF
            elif filter_type == 3:
                recon = (scanline[i] + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                recon = (scanline[i] + paeth_predictor(left, up, up_left)) & 0xFF
            else:
                raise RuntimeError(f"unsupported PNG filter {filter_type}: {path}")
            scanline[i] = recon

        for x in range(0, row_size, bytes_per_pixel):
            r = scanline[x]
            g = scanline[x + 1]
            b = scanline[x + 2]
            luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b
            luminance_sum += luminance / 255.0
            if luminance > 2.0:
                lit_pixels += 1
        previous = scanline

    pixel_count = width * height
    return {
        "width": width,
        "height": height,
        "pixelCount": pixel_count,
        "litPixelCount": lit_pixels,
        "averageLuminance": luminance_sum / max(pixel_count, 1),
    }


def require_pipeline_metadata(metadata_path: Path) -> None:
    metadata_text = metadata_path.read_text(encoding="utf-8")
    for token in [
        "standard-pbr",
        "standard_pbr.contract.glsl",
        "RenderPathNodeSignature",
        "PipelineKey",
        "final shader reflection",
    ]:
        if token not in metadata_text:
            raise RuntimeError(f"realtime metadata missing required token: {token}")
    for token in [
        "assets/materials/pbr.material",
        "source: gltf",
        "legacy material fallback",
        "debug material fallback",
        "empty source",
        "MaterialUBO as positive path",
    ]:
        if token in metadata_text:
            raise RuntimeError(f"realtime metadata contains forbidden token: {token}")


REQUIRED_RENDER_BATCH_STAT_KEYS = (
    "compilerBatchCountConsumed",
    "submittedIndirectBatchCount",
    "submittedIndirectDrawCount",
    "fallbackObservedCount",
)


def normalize_render_batch_stats(metadata: dict[str, object]) -> dict[str, int]:
    batch_stats = metadata.get("renderBatchStats")
    if not isinstance(batch_stats, dict):
        raise RuntimeError("realtime metadata omitted renderBatchStats")

    normalized: dict[str, int] = {}
    for key, value in batch_stats.items():
        if isinstance(value, bool):
            raise RuntimeError(
                f"realtime metadata renderBatchStats {key} is not an int"
            )
        try:
            normalized[key] = int(value)
        except (TypeError, ValueError) as error:
            raise RuntimeError(
                f"realtime metadata renderBatchStats {key} is not an int"
            ) from error

    for key in REQUIRED_RENDER_BATCH_STAT_KEYS:
        if key not in normalized:
            raise RuntimeError(f"realtime metadata renderBatchStats omitted {key}")

    return normalized


def require_output_files(
    root: Path,
    structured: str,
    require_nonblack: bool,
    min_lit_pixels: int,
    min_average_luminance: float,
    require_pipeline_metadata_check: bool,
) -> dict[str, object]:
    payload = json.loads(structured)
    required_paths = [
        ("linearExrPath", "linear EXR"),
        ("cpuSrgbPngPath", "CPU sRGB PNG"),
        ("metadataPath", "metadata JSON"),
    ]
    for field, label in required_paths:
        value = payload.get(field, "")
        if not isinstance(value, str) or not value:
            raise RuntimeError(f"realtime render result omitted {label}: {field}")
        path = resolved_result_path(root, value)
        if not path.is_file():
            raise RuntimeError(f"realtime render did not write {label}: {path}")
        if path.stat().st_size <= 0:
            raise RuntimeError(f"realtime render wrote empty {label}: {path}")

    metadata_value = payload.get("metadataPath", "")
    if not isinstance(metadata_value, str):
        raise RuntimeError("realtime render result metadataPath is not a string")
    metadata_path = resolved_result_path(root, metadata_value)
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    for dimension in ("width", "height"):
        if metadata.get(dimension) != payload.get(dimension):
            raise RuntimeError(
                f"metadata {dimension} does not match render result: {metadata_path}"
            )
    payload["renderBatchStats"] = normalize_render_batch_stats(metadata)
    if require_pipeline_metadata_check:
        require_pipeline_metadata(metadata_path)

    png_value = payload.get("cpuSrgbPngPath", "")
    if require_nonblack:
        png_path = resolved_result_path(root, png_value)
        stats = png_luminance_stats(png_path)
        if int(stats["litPixelCount"]) < min_lit_pixels:
            raise RuntimeError(
                "realtime render output is too dark: "
                f"litPixelCount={stats['litPixelCount']} "
                f"minLitPixels={min_lit_pixels} png={png_path}"
            )
        if float(stats["averageLuminance"]) < min_average_luminance:
            raise RuntimeError(
                "realtime render output average luminance is too low: "
                f"averageLuminance={stats['averageLuminance']} "
                f"minAverageLuminance={min_average_luminance} png={png_path}"
            )
        payload["imageStats"] = stats
    return payload


def prepare_smoke_project(
    root: Path, project_name: str, scene_path: Path, scene_id: str
) -> Path:
    project_root = root / "data" / "projects" / project_name
    if project_root.exists():
        shutil.rmtree(project_root)
    scenes_dir = project_root / "scenes"
    scenes_dir.mkdir(parents=True, exist_ok=True)
    scene_rel = Path("scenes") / f"{scene_id}.scene.yaml"
    shutil.copy2(scene_path, project_root / scene_rel)

    project_document = {
        "schema": "lxe.project.v1",
        "id": project_name,
        "displayName": project_name,
        "activeScene": scene_rel.as_posix(),
        "scenes": [{"id": scene_id, "path": scene_rel.as_posix()}],
        "assetRoots": ["."],
    }
    (project_root / "project.yaml").write_text(
        json.dumps(project_document, indent=2), encoding="utf-8"
    )
    return project_root


def prepare_bootstrap_project(root: Path, project_name: str) -> Path:
    project_root = root / "data" / "projects" / f"{project_name}_bootstrap"
    if project_root.exists():
        shutil.rmtree(project_root)
    scenes_dir = project_root / "scenes"
    scenes_dir.mkdir(parents=True, exist_ok=True)
    scene_rel = Path("scenes") / "bootstrap.scene.yaml"
    (project_root / scene_rel).write_text(
        """scene:
  name: Realtime Render Bootstrap
  gameplayCameraPath: /bootstrap_cam
  environment:
    enabled: false
    intensity: 0.0
    skyboxEnabled: false
  rendering:
    shadows: false
root:
  nodeName: scene_root
  name: ''
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  visibilityMask: 4294967295
  children:
    - nodeName: bootstrap_camera
      name: bootstrap_cam
      transform:
        translation: [0.0, 0.0, 3.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      camera:
        type: perspective
        fovY: 45.0
        aspect: 1.0
        nearPlane: 0.1
        farPlane: 20.0
        cullingMask: 4294967295
""",
        encoding="utf-8",
    )
    project_document = {
        "schema": "lxe.project.v1",
        "id": f"{project_name}_bootstrap",
        "displayName": f"{project_name}_bootstrap",
        "activeScene": scene_rel.as_posix(),
        "scenes": [{"id": "bootstrap", "path": scene_rel.as_posix()}],
        "assetRoots": ["."],
    }
    (project_root / "project.yaml").write_text(
        json.dumps(project_document, indent=2), encoding="utf-8"
    )
    return project_root


def snapshot_file(path: Path) -> bytes | None:
    return path.read_bytes() if path.exists() else None


def restore_file(path: Path, data: bytes | None) -> None:
    if data is None:
        path.unlink(missing_ok=True)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


class PreservedEditorRuntimeState:
    def __init__(self, root: Path, bootstrap_project: Path):
        self.root = root
        self.bootstrap_project = bootstrap_project
        self.state_paths = [
            root / "data" / "lxe_editor" / "editor_data.yaml",
            root / "data" / "lxe_editor" / "runtime_state.yaml",
            root / "data" / "lxe_editor" / "api_token.txt",
        ]
        self.snapshots: dict[Path, bytes | None] = {}

    def __enter__(self) -> "PreservedEditorRuntimeState":
        self.snapshots = {path: snapshot_file(path) for path in self.state_paths}
        editor_data_path = self.state_paths[0]
        editor_data_path.parent.mkdir(parents=True, exist_ok=True)
        editor_data_path.write_text(
            "version: 1\n"
            f"lastProject: {self.bootstrap_project}\n"
            "consoleHistory: []\n",
            encoding="utf-8",
        )
        self.state_paths[1].unlink(missing_ok=True)
        self.state_paths[2].unlink(missing_ok=True)
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        for path, data in self.snapshots.items():
            restore_file(path, data)


def parse_args(argv: list[str]) -> argparse.Namespace:
    root = repo_root_from_script()
    parser = argparse.ArgumentParser(
        description="Run realtime-render run <profile> through a local editor."
    )
    parser.add_argument("--scene", required=True, help="Scene YAML to import.")
    parser.add_argument("--profile", default="preview", help="Output profile name.")
    parser.add_argument(
        "--editor",
        default=str(root / "build/src/demos/lxe_editor/lxe_editor"),
        help="lxe_editor executable path.",
    )
    parser.add_argument("--api-host", default="127.0.0.1")
    parser.add_argument(
        "--api-port",
        type=int,
        default=0,
        help="Local API port. Defaults to 0, which selects a free port.",
    )
    parser.add_argument("--timeout-sec", type=float, default=60.0)
    parser.add_argument(
        "--project-name",
        default=f"codex_realtime_render_{os.getpid()}",
        help="Temporary editor project name.",
    )
    parser.add_argument("--scene-id", default="realtime_render_scene")
    parser.add_argument(
        "--xvfb",
        action="store_true",
        help="Launch editor through xvfb-run -a.",
    )
    parser.add_argument(
        "--require-nonblack",
        action="store_true",
        help="Fail if the CPU sRGB PNG has too few lit pixels.",
    )
    parser.add_argument("--min-lit-pixels", type=int, default=64)
    parser.add_argument("--min-average-luminance", type=float, default=0.001)
    parser.add_argument(
        "--require-pipeline-metadata",
        action="store_true",
        help="Fail unless realtime metadata proves the clean source path.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    root = repo_root_from_script()
    scene_path = Path(args.scene).resolve()
    editor_path = Path(args.editor).resolve()
    token_path = root / "data/lxe_editor/api_token.txt"

    if not scene_path.is_file():
        raise RuntimeError(f"scene file not found: {scene_path}")
    if not editor_path.is_file():
        raise RuntimeError(f"lxe_editor executable not found: {editor_path}")
    project_root = prepare_smoke_project(
        root, args.project_name, scene_path, args.scene_id
    )
    bootstrap_project = prepare_bootstrap_project(root, args.project_name)

    api_port = int(args.api_port)
    if api_port <= 0:
        api_port = find_free_port(args.api_host)
    elif port_accepts_connections(args.api_host, api_port):
        raise RuntimeError(
            f"{args.api_host}:{api_port} is already in use; choose another port"
        )

    editor_args = [
        str(editor_path),
        "--api-enable",
        "--api-host",
        args.api_host,
        "--api-port",
        str(api_port),
    ]
    launch_args = ["xvfb-run", "-a", *editor_args] if args.xvfb else editor_args

    with PreservedEditorRuntimeState(root, bootstrap_project):
        process = subprocess.Popen(
            launch_args,
            cwd=root,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        token: str | None = None
        try:
            wait_for_health(process, args.api_host, api_port, args.timeout_sec)
            token = wait_for_token(token_path, args.timeout_sec)

            editor_command(
                args.api_host,
                api_port,
                token,
                f"project open {quote_command_token(str(project_root))}",
                args.timeout_sec,
            )
            wait_until_scene_loaded(
                args.api_host, api_port, token, args.timeout_sec
            )
            wait_until_profile_visible(
                args.api_host, api_port, token, args.profile, args.timeout_sec
            )
            response = editor_command(
                args.api_host,
                api_port,
                token,
                f"realtime-render run {quote_command_token(args.profile)}",
                args.timeout_sec,
            )
            structured = response.get("structuredJson", "")
            if not isinstance(structured, str) or not structured:
                raise RuntimeError(
                    "realtime-render run did not return structured output"
                )
            payload = require_output_files(
                root,
                structured,
                args.require_nonblack,
                args.min_lit_pixels,
                args.min_average_luminance,
                args.require_pipeline_metadata,
            )
            print(json.dumps(payload, sort_keys=True))
            return 0
        finally:
            stop_editor(process, args.api_host, api_port, token, timeout_sec=5.0)


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except Exception as error:
        print(f"lxe_realtime_render error: {error}", file=sys.stderr)
        raise SystemExit(1)
