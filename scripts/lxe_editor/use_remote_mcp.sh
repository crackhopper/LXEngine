#!/usr/bin/env bash

if [ "$#" -ne 2 ]; then
  echo "usage: source scripts/lxe_editor/use_remote_mcp.sh <mcp-url> <token>" >&2
  return 1 2>/dev/null || exit 1
fi

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
config_path="${LXE_EDITOR_CODEX_CONFIG_PATH:-${repo_root}/.codex/config.toml}"
mcp_url="$1"
token="$2"

if [ -z "${mcp_url}" ] || [ -z "${token}" ]; then
  echo "lxe_editor MCP target: both mcp-url and token are required" >&2
  return 1 2>/dev/null || exit 1
fi

export LXE_EDITOR_MCP_BEARER_TOKEN="${token}"
mkdir -p "$(dirname "${config_path}")"
_lxe_editor_write_codex_config "${config_path}" "${mcp_url}"
echo "lxe_editor MCP target: remote ${mcp_url}"
