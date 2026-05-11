#!/usr/bin/env bash
if [ "$#" -ne 3 ]; then
  echo "usage: source scripts/lxe_editor/use_remote_mcp.sh <host> <port> <token>"
  return 1 2>/dev/null || exit 1
fi

export LXE_EDITOR_REMOTE_MCP_HOST="$1"
export LXE_EDITOR_REMOTE_MCP_PORT="$2"
export LXE_EDITOR_REMOTE_MCP_TOKEN="$3"
echo "lxe_editor MCP target: remote ${LXE_EDITOR_REMOTE_MCP_HOST}:${LXE_EDITOR_REMOTE_MCP_PORT}"
