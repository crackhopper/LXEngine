#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
manager_url="${LXE_MANAGER_URL:-http://127.0.0.1:3880/mcp}"
config_path="${LXE_EDITOR_CODEX_CONFIG_PATH:-${repo_root}/.codex/config.toml}"

mkdir -p "$(dirname "${config_path}")"
MANAGER_URL="${manager_url}" CONFIG_PATH="${config_path}" python3 - <<'PY'
import json
import os
import pathlib
import re

config_path = pathlib.Path(os.environ["CONFIG_PATH"])
manager_url = os.environ["MANAGER_URL"]
text = config_path.read_text(encoding="utf-8") if config_path.is_file() else ""
block = (
    "[mcp_servers.lxe_manager]\n"
    f"url = {json.dumps(manager_url)}\n"
    'bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"\n'
)
pattern = re.compile(r"(?ms)^\[mcp_servers\.lxe_manager\]\r?\n.*?(?=^\[|\Z)")
if pattern.search(text):
    text = pattern.sub(block, text)
else:
    text = text.rstrip()
    text = f"{text}\n\n{block}" if text else block
config_path.write_text(text if text.endswith("\n") else text + "\n", encoding="utf-8")
PY

echo "lxe_manager MCP target: local ${manager_url}"
