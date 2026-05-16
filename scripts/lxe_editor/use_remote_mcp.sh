#!/usr/bin/env bash

_lxe_editor_compat_script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "$#" -ne 1 ]; then
  echo "usage: export LXE_MANAGER_MCP_BEARER_TOKEN=<token>; source scripts/lxe_editor/use_remote_mcp.sh <manager-mcp-url>" >&2
  return 1 2>/dev/null || exit 1
fi
# shellcheck source=../lxe_manager/enable_mcp.sh
source "${_lxe_editor_compat_script_dir}/../lxe_manager/enable_mcp.sh" --endpoint "$1" --token "${LXE_MANAGER_MCP_BEARER_TOKEN:?set LXE_MANAGER_MCP_BEARER_TOKEN or use scripts/lxe_manager/enable_mcp.sh --token ...}"
_lxe_editor_compat_status=$?
unset _lxe_editor_compat_script_dir
return "${_lxe_editor_compat_status}" 2>/dev/null || exit "${_lxe_editor_compat_status}"
