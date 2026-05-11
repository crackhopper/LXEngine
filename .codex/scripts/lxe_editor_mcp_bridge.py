#!/usr/bin/env python3
"""Bridge Codex stdio MCP traffic to a local or remote lxe_editor MCP socket."""

from __future__ import annotations

import json
import os
import pathlib
import socket
import sys
import threading
from dataclasses import dataclass
from typing import Any


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
LOCAL_HOSTS = {"127.0.0.1", "localhost", "::1", "0.0.0.0"}


class RuntimeStateError(RuntimeError):
    pass


@dataclass(frozen=True)
class BridgeTarget:
    host: str
    port: int
    token: str | None = None


def _parse_scalar(raw_value: str) -> Any:
    value = raw_value.strip()
    if not value:
        return ""
    if value[0] == value[-1] and value[0] in {"'", '"'}:
        return value[1:-1]
    lowered = value.lower()
    if lowered == "true":
        return True
    if lowered == "false":
        return False
    if lowered in {"null", "~"}:
        return None
    try:
        return int(value)
    except ValueError:
        return value


def _load_simple_yaml(path: pathlib.Path) -> dict[str, Any]:
    root: dict[str, Any] = {}
    stack: list[tuple[int, dict[str, Any]]] = [(-1, root)]

    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.split("#", 1)[0].rstrip()
        if not line.strip():
            continue

        indent = len(line) - len(line.lstrip(" "))
        stripped = line.strip()
        if ":" not in stripped:
            raise RuntimeStateError(
                f"Unsupported runtime_state.yaml syntax on line {line_number}: {raw_line!r}"
            )

        key, value = stripped.split(":", 1)
        key = key.strip()
        if not key:
            raise RuntimeStateError(
                f"Missing key in runtime_state.yaml on line {line_number}: {raw_line!r}"
            )

        while len(stack) > 1 and indent <= stack[-1][0]:
            stack.pop()

        current = stack[-1][1]
        if value.strip():
            current[key] = _parse_scalar(value)
            continue

        child: dict[str, Any] = {}
        current[key] = child
        stack.append((indent, child))

    return root


def _runtime_state_candidates() -> list[pathlib.Path]:
    candidates: list[pathlib.Path] = []
    for env_name in ("LXE_EDITOR_RUNTIME_ROOT", "LX_RUNTIME_ROOT"):
        raw_root = os.environ.get(env_name)
        if not raw_root:
            continue
        root = pathlib.Path(raw_root)
        candidates.append(root / "data" / "lxe_editor" / "runtime_state.yaml")
    candidates.append(REPO_ROOT / "data" / "lxe_editor" / "runtime_state.yaml")
    return candidates


def _read_runtime_state_target() -> BridgeTarget:
    runtime_state_path = None
    for candidate in _runtime_state_candidates():
        if candidate.exists():
            runtime_state_path = candidate
            break

    if runtime_state_path is None:
        searched = ", ".join(str(path) for path in _runtime_state_candidates())
        raise RuntimeStateError(
            f"Missing runtime state. Start lxe_editor first. Searched: {searched}"
        )

    data = _load_simple_yaml(runtime_state_path)
    host = data.get("mcpHost")
    port = data.get("mcpPort")
    token = None

    if not isinstance(host, str) or not host:
        raise RuntimeStateError(
            f"runtime_state.yaml does not provide mcpHost: {runtime_state_path}"
        )
    if host not in LOCAL_HOSTS:
        raise RuntimeStateError(
            f"Refusing non-local MCP host {host!r} from {runtime_state_path}; "
            "use LXE_EDITOR_REMOTE_MCP_* env vars for remote targets"
        )
    if not isinstance(port, int) or port <= 0 or port > 65535:
        raise RuntimeStateError(
            f"runtime_state.yaml does not provide a valid mcpPort: {runtime_state_path}"
        )
    if host == "0.0.0.0":
        host = "127.0.0.1"

    token_file = data.get("tokenFile")
    if isinstance(token_file, str) and token_file:
        path = pathlib.Path(token_file)
        if path.is_file():
            raw_token = path.read_text(encoding="utf-8").strip()
            token = raw_token or None

    return BridgeTarget(host=host, port=port, token=token)


def _read_remote_target() -> BridgeTarget | None:
    host = os.environ.get("LXE_EDITOR_REMOTE_MCP_HOST", "").strip()
    if not host:
        return None
    port_text = os.environ.get("LXE_EDITOR_REMOTE_MCP_PORT", "").strip()
    token = os.environ.get("LXE_EDITOR_REMOTE_MCP_TOKEN", "").strip()
    if not port_text:
        raise RuntimeStateError("LXE_EDITOR_REMOTE_MCP_PORT is required when using remote MCP")
    if not token:
        raise RuntimeStateError("LXE_EDITOR_REMOTE_MCP_TOKEN is required when using remote MCP")
    try:
        port = int(port_text)
    except ValueError as exc:
        raise RuntimeStateError("LXE_EDITOR_REMOTE_MCP_PORT must be an integer") from exc
    if port <= 0 or port > 65535:
        raise RuntimeStateError("LXE_EDITOR_REMOTE_MCP_PORT is out of range")
    return BridgeTarget(host=host, port=port, token=token)


def _resolve_target() -> BridgeTarget:
    remote = _read_remote_target()
    if remote is not None:
        return remote
    return _read_runtime_state_target()


def _frame_payload(payload: bytes) -> bytes:
    return f"Content-Length: {len(payload)}\r\n\r\n".encode("utf-8") + payload


def _inject_initialize_token(payload: bytes, token: str | None) -> bytes:
    if not token:
        return payload
    try:
        message = json.loads(payload.decode("utf-8"))
    except Exception:
        return payload
    if message.get("method") != "initialize":
        return payload
    params = message.get("params")
    if not isinstance(params, dict):
        params = {}
        message["params"] = params
    params["token"] = token
    return json.dumps(message, separators=(",", ":")).encode("utf-8")


def _pump_stdin_to_socket(sock: socket.socket, token: str | None) -> None:
    buffer = bytearray()
    initialize_rewritten = False
    try:
        while True:
            chunk = os.read(sys.stdin.fileno(), 65536)
            if not chunk:
                break
            buffer.extend(chunk)
            while True:
                header_end = buffer.find(b"\r\n\r\n")
                if header_end < 0:
                    break
                header = bytes(buffer[:header_end]).decode("utf-8", errors="replace")
                content_length = None
                for line in header.split("\r\n"):
                    if line.lower().startswith("content-length:"):
                        content_length = int(line.split(":", 1)[1].strip())
                        break
                if content_length is None:
                    raise RuntimeStateError("missing Content-Length in MCP stdin stream")
                payload_offset = header_end + 4
                payload_end = payload_offset + content_length
                if len(buffer) < payload_end:
                    break
                payload = bytes(buffer[payload_offset:payload_end])
                del buffer[:payload_end]
                if not initialize_rewritten:
                    payload = _inject_initialize_token(payload, token)
                    initialize_rewritten = True
                sock.sendall(_frame_payload(payload))
    except (OSError, RuntimeStateError):
        pass
    finally:
        try:
            sock.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def _pump_socket_to_stdout(sock: socket.socket) -> int:
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                return 0
            os.write(sys.stdout.fileno(), chunk)
    except OSError as exc:
        print(f"lxe_editor MCP bridge I/O error: {exc}", file=sys.stderr)
        return 1


def main() -> int:
    try:
        target = _resolve_target()
        connection = socket.create_connection((target.host, target.port), timeout=5.0)
    except (OSError, RuntimeStateError) as exc:
        print(f"lxe_editor MCP bridge error: {exc}", file=sys.stderr)
        return 1

    connection.settimeout(None)
    stdin_thread = threading.Thread(
        target=_pump_stdin_to_socket,
        args=(connection, target.token),
        daemon=True,
    )
    stdin_thread.start()

    try:
        return _pump_socket_to_stdout(connection)
    finally:
        try:
            connection.close()
        except OSError:
            pass
        stdin_thread.join(timeout=0.2)


if __name__ == "__main__":
    raise SystemExit(main())
