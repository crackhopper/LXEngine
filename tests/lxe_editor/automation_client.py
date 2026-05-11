from __future__ import annotations

import contextlib
import json
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any


def _repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def _parse_simple_yaml_map(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        key, value = line.split(":", 1)
        values[key.strip()] = value.strip().strip("'\"")
    return values


def _normalize_loopback_host(host: str) -> str:
    if host == "0.0.0.0":
        return "127.0.0.1"
    return host


def _pick_free_port() -> int:
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def _runtime_state_candidates(runtime_root: pathlib.Path) -> list[pathlib.Path]:
    return [
        runtime_root / "data" / "lxe_editor" / "runtime_state.yaml",
    ]


def _token_candidates(runtime_root: pathlib.Path) -> list[pathlib.Path]:
    return [
        runtime_root / "data" / "lxe_editor" / "automation_token.txt",
    ]


def _resolve_executable(repo_root: pathlib.Path) -> pathlib.Path:
    override = os.environ.get("LXE_EDITOR_TEST_EXECUTABLE")
    candidates = []
    if override:
        candidates.append(pathlib.Path(override))
    candidates.extend(
        [
            repo_root / "build" / "src" / "demos" / "lxe_editor" / "lxe_editor",
        ]
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "unable to locate editor executable; set LXE_EDITOR_TEST_EXECUTABLE"
    )


def _prepare_runtime_root(repo_root: pathlib.Path) -> pathlib.Path:
    override = os.environ.get("LXE_EDITOR_TEST_RUNTIME_ROOT")
    if override:
        runtime_root = pathlib.Path(override)
        runtime_root.mkdir(parents=True, exist_ok=True)
        return runtime_root

    temp_root = pathlib.Path(
        tempfile.mkdtemp(prefix="lxengine-lxe-editor-runtime-")
    ).resolve()
    assets_src = repo_root / "assets"
    assets_dst = temp_root / "assets"
    try:
        assets_dst.symlink_to(assets_src, target_is_directory=True)
    except OSError:
        shutil.copytree(assets_src, assets_dst, dirs_exist_ok=True)
    (temp_root / "data" / "lxe_editor").mkdir(parents=True, exist_ok=True)
    (temp_root / "data" / "scenes").mkdir(parents=True, exist_ok=True)
    return temp_root


@dataclass(frozen=True)
class RuntimeEndpoint:
    host: str
    port: int


class LxeEditorClient:
    def __init__(
        self,
        runtime_root: pathlib.Path,
        endpoint: RuntimeEndpoint | None = None,
        token: str | None = None,
        timeout_s: float = 15.0,
    ) -> None:
        self.runtime_root = runtime_root.resolve()
        self.endpoint = endpoint
        self._token = token
        self.timeout_s = timeout_s

    def read_runtime_state(self) -> dict[str, str]:
        for candidate in _runtime_state_candidates(self.runtime_root):
            if candidate.is_file():
                return _parse_simple_yaml_map(candidate.read_text(encoding="utf-8"))
        return {}

    def read_token(self) -> str:
        if self._token:
            return self._token
        for candidate in _token_candidates(self.runtime_root):
            if candidate.is_file():
                token = candidate.read_text(encoding="utf-8").strip()
                if token:
                    self._token = token
                    return token
        raise FileNotFoundError("automation token file not found")

    def read_mcp_url(self) -> str:
        state = self.read_runtime_state()
        direct_url = state.get("mcpUrl", "").strip()
        if direct_url:
            return direct_url
        raise FileNotFoundError("mcp URL not found in runtime_state.yaml")

    def wait_until_ready(self, timeout_s: float | None = None) -> None:
        deadline = time.monotonic() + (timeout_s or self.timeout_s)
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                self._refresh_endpoint_from_runtime_state()
                self.health()
                self.read_token()
                return
            except Exception as exc:  # pragma: no cover - polling path
                last_error = exc
                time.sleep(0.1)
        raise TimeoutError("editor API server did not become ready") from last_error

    def health(self) -> dict[str, Any]:
        return self._request_json("GET", "/health", auth=False)

    def command(self, line: str) -> dict[str, Any]:
        return self._request_json("POST", "/api/command", {"line": line})

    def get_state(self) -> dict[str, Any]:
        return self._request_json("GET", "/api/state")

    def get_summary(self) -> dict[str, Any]:
        return self._request_json("GET", "/api/state/summary")

    def get_selection(self) -> dict[str, Any]:
        return self._request_json("GET", "/api/state/selection")

    def get_cameras(self) -> dict[str, Any]:
        return self._request_json("GET", "/api/state/cameras")

    def get_scene(self) -> dict[str, Any]:
        return self._request_json("GET", "/api/state/scene")

    def get_toolbar(self) -> dict[str, Any]:
        return self._request_json("GET", "/api/state/toolbar")

    def set_mode(self, mode: str) -> dict[str, Any]:
        return self._request_json("POST", "/api/mode", {"mode": mode})

    def set_preview(self, enabled: bool) -> dict[str, Any]:
        return self._request_json("POST", "/api/preview", {"enabled": enabled})

    def reset_editor_camera_to_game(self) -> dict[str, Any]:
        return self._request_json("POST", "/api/camera/reset-editor-to-game", {})

    def pick(self, x: float, y: float) -> dict[str, Any]:
        return self._request_json("POST", "/api/pick", {"x": x, "y": y})

    def quit(self) -> dict[str, Any]:
        return self.command("quit")

    def wait_for(
        self,
        predicate,
        timeout_s: float = 10.0,
        poll_interval_s: float = 0.1,
    ) -> Any:
        deadline = time.monotonic() + timeout_s
        last_value = None
        while time.monotonic() < deadline:
            last_value = predicate()
            if last_value:
                return last_value
            time.sleep(poll_interval_s)
        raise TimeoutError("timed out waiting for automation condition")

    def decode_structured_json(self, response: dict[str, Any]) -> dict[str, Any]:
        structured = response.get("structuredJson", "")
        if not structured:
            return {}
        return json.loads(structured)

    def mcp_request(
        self,
        method: str,
        params: dict[str, Any] | None = None,
        *,
        request_id: int = 1,
    ) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
        }
        if params is not None:
            payload["params"] = params

        url = self.read_mcp_url()
        headers = {
            "Accept": "application/json",
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.read_token()}",
        }
        request = urllib.request.Request(
            url,
            data=json.dumps(payload).encode("utf-8"),
            method="POST",
            headers=headers,
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout_s) as response:
                text = response.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            text = exc.read().decode("utf-8")
            raise RuntimeError(f"POST {url} failed: {exc.code} {text}") from exc
        return json.loads(text)

    def _refresh_endpoint_from_runtime_state(self) -> None:
        if self.endpoint is not None:
            return
        state = self.read_runtime_state()
        host = state.get("apiHost") or state.get("httpHost") or state.get("host")
        port_text = state.get("apiPort") or state.get("httpPort") or state.get("port")
        if host and port_text:
            self.endpoint = RuntimeEndpoint(
                host=_normalize_loopback_host(host),
                port=int(port_text),
            )

    def _request_json(
        self,
        method: str,
        path: str,
        payload: dict[str, Any] | None = None,
        *,
        auth: bool = True,
    ) -> dict[str, Any]:
        self._refresh_endpoint_from_runtime_state()
        if self.endpoint is None:
            raise RuntimeError("automation endpoint is not configured")

        body = None
        headers = {"Accept": "application/json"}
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        if auth:
            headers["Authorization"] = f"Bearer {self.read_token()}"

        url = f"http://{self.endpoint.host}:{self.endpoint.port}{path}"
        request = urllib.request.Request(url, data=body, method=method, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=self.timeout_s) as response:
                text = response.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            text = exc.read().decode("utf-8")
            raise RuntimeError(f"{method} {path} failed: {exc.code} {text}") from exc
        return json.loads(text)


class LxeEditorHarness:
    def __init__(
        self,
        *,
        repo_root: pathlib.Path | None = None,
        executable: pathlib.Path | None = None,
        runtime_root: pathlib.Path | None = None,
        host: str = "127.0.0.1",
        port: int | None = None,
    ) -> None:
        self.repo_root = (repo_root or _repo_root()).resolve()
        self.executable = executable or _resolve_executable(self.repo_root)
        self.runtime_root = runtime_root or _prepare_runtime_root(self.repo_root)
        self.host = host
        self.port = port or _pick_free_port()
        self.process: subprocess.Popen[str] | None = None
        self.client = LxeEditorClient(
            self.runtime_root,
            endpoint=RuntimeEndpoint(self.host, self.port),
        )
        self._owns_runtime_root = runtime_root is None and os.environ.get(
            "LXE_EDITOR_TEST_RUNTIME_ROOT"
        ) is None

    def start(self) -> "LxeEditorHarness":
        if os.name != "nt" and not os.environ.get("DISPLAY"):
            raise RuntimeError("DISPLAY is not set; run the black-box suite under xvfb-run")

        env = os.environ.copy()
        env["LX_RUNTIME_ROOT"] = str(self.runtime_root)
        sdl_lib_dir = self.repo_root / "build" / "_deps" / "sdl3-build"
        if sdl_lib_dir.is_dir():
            existing_ld = env.get("LD_LIBRARY_PATH")
            env["LD_LIBRARY_PATH"] = (
                f"{sdl_lib_dir}:{existing_ld}" if existing_ld else str(sdl_lib_dir)
            )

        self.process = subprocess.Popen(
            [
                str(self.executable),
                "--automation-enable",
                "--automation-host",
                self.host,
                "--automation-port",
                str(self.port),
            ],
            cwd=self.repo_root,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        try:
            self.client.wait_until_ready()
        except Exception:
            self.close()
            raise
        return self

    def close(self, *, cleanup_runtime_root: bool = True) -> None:
        if self.process is not None:
            if self.process.poll() is None:
                with contextlib.suppress(Exception):
                    self.client.quit()
                try:
                    self.process.wait(timeout=5.0)
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=5.0)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                        self.process.wait(timeout=5.0)
                except Exception:
                    self.process.terminate()
                    self.process.kill()
                    self.process.wait(timeout=5.0)
            self.process = None
        if cleanup_runtime_root and self._owns_runtime_root:
            shutil.rmtree(self.runtime_root, ignore_errors=True)

    def restart(self) -> "LxeEditorHarness":
        self.close(cleanup_runtime_root=False)
        self.process = None
        self.client = LxeEditorClient(
            self.runtime_root,
            endpoint=RuntimeEndpoint(self.host, self.port),
        )
        return self.start()
