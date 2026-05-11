#!/usr/bin/env bash

_lxe_editor_repo_root() {
  local script_dir
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  cd "${script_dir}/../.." && pwd
}

_lxe_editor_write_codex_config() {
  local config_path="$1"
  local mcp_url="$2"
  cat > "${config_path}" <<EOF
model = "gpt-5.4"
model_reasoning_effort = "medium"
approval_policy = "never"
sandbox_mode = "danger-full-access"
trust_level = "trusted"

[mcp_servers.lxe_editor]
url = "${mcp_url}"
bearer_token_env_var = "LXE_EDITOR_MCP_BEARER_TOKEN"
EOF
}

repo_root="$(_lxe_editor_repo_root)"
runtime_root="${LXE_EDITOR_RUNTIME_ROOT:-${LX_RUNTIME_ROOT:-${repo_root}}}"
config_path="${LXE_EDITOR_CODEX_CONFIG_PATH:-${repo_root}/.codex/config.toml}"
runtime_state_path="${runtime_root}/data/lxe_editor/runtime_state.yaml"

if [ ! -f "${runtime_state_path}" ]; then
  echo "lxe_editor MCP target: missing runtime state at ${runtime_state_path}" >&2
  return 1 2>/dev/null || exit 1
fi

mapfile -t local_state < <(
  python3 - "${runtime_state_path}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
values = {}
for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = raw_line.strip()
    if not line or line.startswith("#") or ":" not in line:
        continue
    key, value = line.split(":", 1)
    values[key.strip()] = value.strip().strip("'\"")

url = values.get("mcpUrl", "").strip()
token_file = values.get("tokenFile", "").strip()
print(url)
print(token_file)
PY
)

mcp_url="${local_state[0]}"
token_file="${local_state[1]}"

if [ -z "${mcp_url}" ]; then
  echo "lxe_editor MCP target: runtime state does not provide mcpUrl" >&2
  return 1 2>/dev/null || exit 1
fi
if [ -z "${token_file}" ] || [ ! -f "${token_file}" ]; then
  echo "lxe_editor MCP target: token file is missing (${token_file})" >&2
  return 1 2>/dev/null || exit 1
fi

export LXE_EDITOR_MCP_BEARER_TOKEN
LXE_EDITOR_MCP_BEARER_TOKEN="$(tr -d '\r\n' < "${token_file}")"
if [ -z "${LXE_EDITOR_MCP_BEARER_TOKEN}" ]; then
  echo "lxe_editor MCP target: token file is empty (${token_file})" >&2
  return 1 2>/dev/null || exit 1
fi

mkdir -p "$(dirname "${config_path}")"
_lxe_editor_write_codex_config "${config_path}" "${mcp_url}"
echo "lxe_editor MCP target: local ${mcp_url}"
