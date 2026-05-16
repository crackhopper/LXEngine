#!/usr/bin/env python3
"""Rewrite Codex MCP config for lxe_manager (shared by enable_mcp.sh / .ps1)."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
import tomllib


def _strip_legacy_editor(text: str) -> str:
    old_editor = re.compile(r"(?ms)^\[mcp_servers\.lxe_editor\]\r?\n.*?(?=^\[|\Z)")
    return old_editor.sub("", text)


def _read_manager_url(text: str) -> str:
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as exc:
        raise SystemExit(f"enable_mcp: invalid TOML in config: {exc}") from exc
    mcp = data.get("mcp_servers")
    if not isinstance(mcp, dict):
        raise SystemExit(
            "enable_mcp: no [mcp_servers.lxe_manager] in config; use --local or --endpoint"
        )
    block = mcp.get("lxe_manager")
    if not isinstance(block, dict):
        raise SystemExit(
            "enable_mcp: no [mcp_servers.lxe_manager] in config; use --local or --endpoint"
        )
    url = block.get("url")
    if not isinstance(url, str) or not url:
        raise SystemExit("enable_mcp: [mcp_servers.lxe_manager] has no url= line")
    return url


def _write_block(text: str, manager_url: str) -> str:
    text = _strip_legacy_editor(text)
    block = (
        "[mcp_servers.lxe_manager]\n"
        f"url = {json.dumps(manager_url)}\n"
        'bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"\n'
    )
    manager_pattern = re.compile(r"(?ms)^\[mcp_servers\.lxe_manager\]\r?\n.*?(?=^\[|\Z)")
    if manager_pattern.search(text):
        text = manager_pattern.sub(lambda _: block, text)
    else:
        text = text.rstrip()
        text = f"{text}\n\n{block}" if text else block
    return text if text.endswith("\n") else text + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, type=pathlib.Path)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--set-url", metavar="URL", help="Write this manager MCP URL")
    group.add_argument(
        "--keep-url",
        action="store_true",
        help="Keep existing url under [mcp_servers.lxe_manager]",
    )
    args = parser.parse_args()
    config_path: pathlib.Path = args.config
    config_path.parent.mkdir(parents=True, exist_ok=True)
    text = config_path.read_text(encoding="utf-8") if config_path.is_file() else ""

    if args.set_url is not None:
        manager_url = args.set_url
    else:
        manager_url = _read_manager_url(text)

    out = _write_block(text, manager_url)
    config_path.write_text(out, encoding="utf-8")


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:
        sys.exit(0)
