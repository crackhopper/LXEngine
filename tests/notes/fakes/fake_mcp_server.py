#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Any


def write_message(message: dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(message, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def read_message() -> dict[str, Any] | None:
    line = sys.stdin.readline()
    if line == "":
        return None
    line = line.strip()
    if not line:
        return {}
    if not line.lower().startswith("content-length:"):
        return json.loads(line)

    length = parse_content_length(line)
    while True:
        header = sys.stdin.readline()
        if header in {"", "\n", "\r\n"}:
            break
        if header.lower().startswith("content-length:"):
            length = parse_content_length(header.strip())
    if length <= 0:
        return {}
    return json.loads(sys.stdin.read(length))


def parse_content_length(header: str) -> int:
    _, _, value = header.partition(":")
    return int(value.strip())


def response(request: dict[str, Any], result: Any) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request.get("id"), "result": result}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["ok", "no-tool", "early-exit"], default=None)
    args = parser.parse_args()
    mode = args.mode or os.environ.get("FAKE_MCP_MODE", "ok")
    if mode == "early-exit":
        sys.stderr.write("fake MCP early exit\n")
        sys.stderr.flush()
        return 2

    while True:
        request = read_message()
        if request is None:
            break
        if not request:
            continue
        method = request.get("method")
        if "id" not in request:
            continue
        if method == "initialize":
            write_message(response(request, {"protocolVersion": "2024-11-05", "capabilities": {}}))
        elif method == "tools/list":
            tools = [{"name": "not.codex"}] if mode == "no-tool" else [{"name": "codex.prompt"}]
            write_message(response(request, {"tools": tools}))
        elif method == "tools/call":
            arguments = request.get("params", {}).get("arguments", {})
            text = "fake mcp response"
            if "Current user question:" in str(arguments.get("prompt", "")):
                text += " with prompt"
            write_message(response(request, {"content": [{"type": "text", "text": text}]}))
        else:
            write_message(
                {
                    "jsonrpc": "2.0",
                    "id": request.get("id"),
                    "error": {"code": -32601, "message": f"unknown method: {method}"},
                }
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
