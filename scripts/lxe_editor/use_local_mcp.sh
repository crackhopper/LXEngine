#!/usr/bin/env bash

_lxe_editor_compat_script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "${LXE_MANAGER_URL:-}" ]; then
  # shellcheck source=../lxe_manager/enable_mcp.sh
  source "${_lxe_editor_compat_script_dir}/../lxe_manager/enable_mcp.sh" --endpoint "${LXE_MANAGER_URL}" --token "${LXE_MANAGER_MCP_BEARER_TOKEN:?set LXE_MANAGER_MCP_BEARER_TOKEN or use scripts/lxe_manager/enable_mcp.sh --token ...}"
else
  # shellcheck source=../lxe_manager/enable_mcp.sh
  source "${_lxe_editor_compat_script_dir}/../lxe_manager/enable_mcp.sh" --local --token "${LXE_MANAGER_MCP_BEARER_TOKEN:?set LXE_MANAGER_MCP_BEARER_TOKEN or use scripts/lxe_manager/enable_mcp.sh --token ...}"
fi
_lxe_editor_compat_status=$?
unset _lxe_editor_compat_script_dir
return "${_lxe_editor_compat_status}" 2>/dev/null || exit "${_lxe_editor_compat_status}"
