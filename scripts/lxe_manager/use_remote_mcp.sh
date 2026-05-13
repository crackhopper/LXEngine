#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: source scripts/lxe_manager/use_remote_mcp.sh <manager-mcp-url> <token>" >&2
  return 1 2>/dev/null || exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
config_path="${LXE_EDITOR_CODEX_CONFIG_PATH:-${repo_root}/.codex/config.toml}"
manager_url="$1"
token="$2"

if [ -z "${manager_url}" ] || [ -z "${token}" ]; then
  echo "lxe_manager MCP target: both manager-mcp-url and token are required" >&2
  return 1 2>/dev/null || exit 1
fi

export LXE_MANAGER_MCP_BEARER_TOKEN="${token}"
mkdir -p "$(dirname "${config_path}")"
cat > "${config_path}" <<EOF
model = "gpt-5.4"
model_reasoning_effort = "medium"
approval_policy = "never"
sandbox_mode = "danger-full-access"
trust_level = "trusted"

[mcp_servers.lxe_manager]
url = "${manager_url}"
bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"
EOF

echo "lxe_manager MCP target: remote ${manager_url}"
