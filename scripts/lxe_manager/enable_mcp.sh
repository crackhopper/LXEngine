#!/usr/bin/env bash
#
# Point Codex at lxe_manager MCP: updates .codex/config.toml and exports
# LXE_MANAGER_MCP_BEARER_TOKEN for the current shell (source this script).
#
#   source scripts/lxe_manager/enable_mcp.sh --local --token '<token>'
#   source scripts/lxe_manager/enable_mcp.sh --endpoint 'http://host:3880/mcp' --token '<token>'
#   source scripts/lxe_manager/enable_mcp.sh --token '<token>'   # keep existing url in config

_lxe_manager_enable_mcp_main() {
  local script_dir repo_root config_path writer
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  repo_root="$(cd "${script_dir}/../.." && pwd)"
  config_path="${LXE_EDITOR_CODEX_CONFIG_PATH:-${repo_root}/.codex/config.toml}"
  writer="${script_dir}/enable_mcp_write_config.py"

  local usage
  usage() {
    echo "usage: source scripts/lxe_manager/enable_mcp.sh [--local | --endpoint <url>] --token <token>" >&2
    echo "  --local       use http://127.0.0.1:3880/mcp" >&2
    echo "  --endpoint    use the given manager MCP URL" >&2
    echo "  (neither)     keep the existing url in [mcp_servers.lxe_manager]" >&2
    echo "  --token       bearer token (required); exported as LXE_MANAGER_MCP_BEARER_TOKEN" >&2
  }

  local mode="inherit"
  local endpoint=""
  local token=""
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --local)
        if [ "${mode}" != "inherit" ]; then
          echo "enable_mcp: use only one of --local or --endpoint" >&2
          return 1
        fi
        mode="local"
        shift
        ;;
      --endpoint)
        if [ "${mode}" != "inherit" ]; then
          echo "enable_mcp: use only one of --local or --endpoint" >&2
          return 1
        fi
        mode="endpoint"
        if [ "$#" -lt 2 ]; then
          echo "enable_mcp: --endpoint requires a URL" >&2
          return 1
        fi
        endpoint="$2"
        shift 2
        ;;
      --token=*)
        token="${1#*=}"
        shift
        ;;
      --token)
        if [ "$#" -lt 2 ]; then
          echo "enable_mcp: --token requires a value" >&2
          return 1
        fi
        token="$2"
        shift 2
        ;;
      -h | --help)
        usage
        return 0
        ;;
      *)
        echo "enable_mcp: unknown argument: $1" >&2
        usage
        return 1
        ;;
    esac
  done

  if [ -z "${token}" ]; then
    echo "enable_mcp: --token is required" >&2
    usage
    return 1
  fi

  local manager_url=""
  if [ "${mode}" = "local" ]; then
    manager_url="http://127.0.0.1:3880/mcp"
  elif [ "${mode}" = "endpoint" ]; then
    manager_url="${endpoint}"
  fi

  if ! (
    set -euo pipefail
    if [ "${mode}" = "inherit" ]; then
      python3 "${writer}" --config "${config_path}" --keep-url
    else
      python3 "${writer}" --config "${config_path}" --set-url "${manager_url}"
    fi
  ); then
    return 1
  fi

  if [ "${mode}" = "local" ]; then
    echo "lxe_manager MCP target: local ${manager_url}"
  elif [ "${mode}" = "endpoint" ]; then
    echo "lxe_manager MCP target: remote ${manager_url}"
  else
    echo "lxe_manager MCP target: keep existing url in ${config_path}"
  fi

  LXE_MANAGER_MCP_BEARER_TOKEN="${token}"
  export LXE_MANAGER_MCP_BEARER_TOKEN
  return 0
}

_lxe_manager_enable_mcp_main "$@"
_lxe_manager_status=$?
unset -f _lxe_manager_enable_mcp_main
return "${_lxe_manager_status}" 2>/dev/null || exit "${_lxe_manager_status}"
