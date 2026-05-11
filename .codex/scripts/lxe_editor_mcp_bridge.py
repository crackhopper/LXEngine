#!/usr/bin/env python3
"""Bridge Codex stdio MCP traffic to the future lxe_editor localhost socket.

This stays repo-local so Codex can register `lxe_editor` now, while the source
tree work that writes `data/lxe_editor/runtime_state.yaml` lands separately.
"""

from __future__ import annotations

import os
import pathlib
import socket
import sys
import threading
from typing import Any


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
LOCAL_HOSTS = {"127.0.0.1", "localhost", "::1"}


class RuntimeStateError(RuntimeError):
    pass


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

    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
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


def _read_runtime_state() -> tuple[str, int]:
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

    host = None
    port = None

    mcp_section = data.get("mcp")
    if isinstance(mcp_section, dict):
        host = mcp_section.get("host")
        port = mcp_section.get("port")

    if host is None:
        host = data.get("mcpHost")
    if port is None:
        port = data.get("mcpPort")

    if not isinstance(host, str) or not host:
        raise RuntimeStateError(
            f"runtime_state.yaml does not provide mcp.host or mcpHost: {runtime_state_path}"
        )
    if host not in LOCAL_HOSTS:
        raise RuntimeStateError(
            f"Refusing non-local MCP host {host!r} from {runtime_state_path}"
        )
    if not isinstance(port, int) or port <= 0 or port > 65535:
        raise RuntimeStateError(
            f"runtime_state.yaml does not provide a valid mcp.port or mcpPort: {runtime_state_path}"
        )

    return host, port


def _pump_stdin_to_socket(sock: socket.socket) -> None:
    try:
        while True:
            chunk = os.read(sys.stdin.fileno(), 65536)
            if not chunk:
                try:
                    sock.shutdown(socket.SHUT_WR)
                except OSError:
                    pass
                return
            sock.sendall(chunk)
    except OSError:
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
        host, port = _read_runtime_state()
        connection = socket.create_connection((host, port), timeout=5.0)
    except (OSError, RuntimeStateError) as exc:
        print(f"lxe_editor MCP bridge error: {exc}", file=sys.stderr)
        return 1

    connection.settimeout(None)
    stdin_thread = threading.Thread(
        target=_pump_stdin_to_socket,
        args=(connection,),
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
