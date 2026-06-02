#!/usr/bin/env python3
"""Run a realtime render output profile through a local lxe_editor process."""

from __future__ import annotations

import argparse
import http.client
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time


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


def require_output_files(root: Path, structured: str) -> None:
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
            f"project init empty {quote_command_token(args.project_name)}",
            args.timeout_sec,
        )
        time.sleep(0.5)
        editor_command(
            args.api_host,
            api_port,
            token,
            "scene import "
            f"{quote_command_token(str(scene_path))} {quote_command_token(args.scene_id)}",
            args.timeout_sec,
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
            raise RuntimeError("realtime-render run did not return structured output")
        require_output_files(root, structured)
        print(structured)
        return 0
    finally:
        stop_editor(process, args.api_host, api_port, token, timeout_sec=5.0)


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except Exception as error:
        print(f"lxe_realtime_render error: {error}", file=sys.stderr)
        raise SystemExit(1)
