#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
manager_url="${LXE_MANAGER_URL:-http://127.0.0.1:3880/mcp}"
config_path="${LXE_EDITOR_CODEX_CONFIG_PATH:-${repo_root}/.codex/config.toml}"

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

echo "lxe_manager MCP target: local ${manager_url}"
