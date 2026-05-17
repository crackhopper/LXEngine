#!/usr/bin/env bash
#
# Point Codex at lxe_manager MCP: updates .codex/config.toml and exports
# LXE_MANAGER_MCP_BEARER_TOKEN for the current shell (source this script).
#
#   source scripts/lxe_manager/enable_mcp.sh --local
#   source scripts/lxe_manager/enable_mcp.sh --local --no-start-manager
#   ./scripts/lxe_manager/enable_mcp.sh --stop-manager   # ok without source
#   source scripts/lxe_manager/enable_mcp.sh --endpoint 'http://host:3880/mcp' --token '<token>'
#   source scripts/lxe_manager/enable_mcp.sh --token '<token>'   # keep existing url in config
#
# --local: optional --token; frees LXE_MANAGER_PORT if busy, then starts start_mcp.sh
# in the background with the same LXE_MANAGER_MCP_BEARER_TOKEN (unless --no-start-manager).

_lxe_manager_tcp_port_busy() {
  local port="$1"
  (exec 3<>"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1
}

_lxe_manager_wait_port_free() {
  local port="$1"
  local max_tries="${2:-50}"
  local i
  for ((i = 0; i < max_tries; i++)); do
    if ! _lxe_manager_tcp_port_busy "${port}"; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

# Stop processes listening on 127.0.0.1:port (lxe_manager MCP).
_lxe_manager_stop_local_mcp() {
  local port="$1"
  local pids

  if ! _lxe_manager_tcp_port_busy "${port}"; then
    echo "enable_mcp: port ${port} is free." >&2
    return 0
  fi

  if command -v fuser >/dev/null 2>&1; then
    echo "enable_mcp: stopping processes holding TCP port ${port} (SIGTERM)." >&2
    fuser -k -SIGTERM "${port}/tcp" >/dev/null 2>&1 || true
  elif command -v lsof >/dev/null 2>&1; then
    pids="$(lsof -nP -iTCP:"${port}" -sTCP:LISTEN -t 2>/dev/null | tr '\n' ' ')"
    if [ -n "${pids}" ]; then
      echo "enable_mcp: stopping listener PIDs on port ${port}: ${pids}" >&2
      kill -TERM ${pids} 2>/dev/null || true
    fi
  else
    echo "enable_mcp: install fuser or lsof to stop the manager on port ${port}" >&2
    return 1
  fi

  if _lxe_manager_wait_port_free "${port}" 40; then
    echo "enable_mcp: port ${port} is free." >&2
    return 0
  fi

  echo "enable_mcp: port ${port} still busy; SIGKILL." >&2
  if command -v fuser >/dev/null 2>&1; then
    fuser -k -SIGKILL "${port}/tcp" >/dev/null 2>&1 || true
  elif command -v lsof >/dev/null 2>&1; then
    pids="$(lsof -nP -iTCP:"${port}" -sTCP:LISTEN -t 2>/dev/null | tr '\n' ' ')"
    [ -n "${pids}" ] && kill -KILL ${pids} 2>/dev/null || true
  fi
  sleep 0.2

  if _lxe_manager_tcp_port_busy "${port}"; then
    echo "enable_mcp: could not free port ${port}." >&2
    return 1
  fi
  echo "enable_mcp: port ${port} is free." >&2
  return 0
}

_lxe_manager_enable_mcp_main() {
  local script_dir repo_root config_path writer
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  repo_root="$(cd "${script_dir}/../.." && pwd)"
  config_path="${LXE_EDITOR_CODEX_CONFIG_PATH:-${repo_root}/.codex/config.toml}"
  writer="${script_dir}/enable_mcp_write_config.py"

  local usage
  usage() {
    echo "usage: source scripts/lxe_manager/enable_mcp.sh [--local | --endpoint <url> | --stop-manager] ..." >&2
    echo "  --local             http://127.0.0.1:<port>/mcp; stop listener on port if needed, then start start_mcp.sh (unless --no-start-manager)" >&2
    echo "  --stop-manager      stop listeners on LXE_MANAGER_PORT (default 3880); no Codex config change; may run as ./enable_mcp.sh without source" >&2
    echo "  --endpoint <url>    set MCP URL (requires --token)" >&2
    echo "  (neither)           keep url in [mcp_servers.lxe_manager] (requires --token)" >&2
    echo "  --token <value>     optional with --local; required otherwise" >&2
    echo "  --no-start-manager  with --local: only write config + export token" >&2
  }

  local mode="inherit"
  local endpoint=""
  local token=""
  local no_start_manager=0
  local stop_manager_only=0
  local mcp_port="${LXE_MANAGER_PORT:-3880}"
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --no-start-manager)
        no_start_manager=1
        shift
        ;;
      --stop-manager)
        stop_manager_only=1
        shift
        ;;
      --local)
        if [ "${mode}" != "inherit" ]; then
          echo "enable_mcp: use only one of --local, --endpoint, or --stop-manager" >&2
          return 1
        fi
        mode="local"
        shift
        ;;
      --endpoint)
        if [ "${mode}" != "inherit" ]; then
          echo "enable_mcp: use only one of --local, --endpoint, or --stop-manager" >&2
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

  if [ "${stop_manager_only}" -eq 1 ]; then
    if [ "${mode}" != "inherit" ]; then
      echo "enable_mcp: --stop-manager cannot be combined with --local or --endpoint" >&2
      return 1
    fi
    if [ -n "${token}" ]; then
      echo "enable_mcp: --stop-manager ignores --token" >&2
    fi
    _lxe_manager_stop_local_mcp "${mcp_port}" || return 1
    return 0
  fi

  local token_auto_generated=0
  if [ -z "${token}" ]; then
    if [ "${mode}" = "local" ]; then
      token="$(python3 -c 'import secrets; print(secrets.token_urlsafe(32))')"
      token_auto_generated=1
    else
      echo "enable_mcp: --token is required (except with --local)" >&2
      usage
      return 1
    fi
  fi

  local manager_url=""
  if [ "${mode}" = "local" ]; then
    manager_url="http://127.0.0.1:${mcp_port}/mcp"
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

  if [ "${token_auto_generated}" -eq 1 ]; then
    echo "enable_mcp: generated local bearer token." >&2
    printf 'export LXE_MANAGER_MCP_BEARER_TOKEN=%q\n' "${token}" >&2
  fi

  if [ "${mode}" = "local" ] && [ "${no_start_manager}" -eq 0 ]; then
    if _lxe_manager_tcp_port_busy "${mcp_port}"; then
      echo "enable_mcp: 127.0.0.1:${mcp_port} busy; stopping existing listener(s) before start_mcp.sh." >&2
      _lxe_manager_stop_local_mcp "${mcp_port}" || return 1
    fi
    mkdir -p "${repo_root}/data/lxe_manager" 2>/dev/null || true
    (
      cd "${repo_root}" || exit 1
      nohup env \
        LXE_MANAGER_MCP_BEARER_TOKEN="${token}" \
        LXE_MANAGER_PORT="${mcp_port}" \
        bash "${script_dir}/start_mcp.sh" </dev/null >/dev/null 2>&1 &
    )
    echo "enable_mcp: started start_mcp.sh in the background with this LXE_MANAGER_MCP_BEARER_TOKEN (log: ${repo_root}/data/lxe_manager/mcp.log)." >&2
  fi
  return 0
}

# Exporting token requires source. --stop-manager does not.
_enable_mcp_allow_direct_exec=0
for _enable_mcp_arg in "$@"; do
  if [[ "${_enable_mcp_arg}" == "--stop-manager" ]]; then
    _enable_mcp_allow_direct_exec=1
    break
  fi
done

if [[ "${BASH_SOURCE[0]}" == "$0" ]] && [[ ${_enable_mcp_allow_direct_exec} -eq 0 ]]; then
  _enable_mcp_path="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
  echo "enable_mcp: must be sourced so LXE_MANAGER_MCP_BEARER_TOKEN exists in this shell for Codex." >&2
  echo "  source ${_enable_mcp_path} $*" >&2
  echo "  (exception: ${_enable_mcp_path} --stop-manager may be run as ./...)" >&2
  exit 1
fi

_lxe_manager_enable_mcp_main "$@"
_lxe_manager_enable_mcp_status=$?
unset -f _lxe_manager_enable_mcp_main
unset -f _lxe_manager_tcp_port_busy
unset -f _lxe_manager_wait_port_free
unset -f _lxe_manager_stop_local_mcp
return "${_lxe_manager_enable_mcp_status}" 2>/dev/null || exit "${_lxe_manager_enable_mcp_status}"
