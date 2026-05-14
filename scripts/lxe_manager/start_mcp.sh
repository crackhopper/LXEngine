#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
manager_dir="${repo_root}/tools/lxe_manager"
log_file="${LXE_MANAGER_MCP_LOG_FILE:-${repo_root}/data/lxe_manager/mcp.log}"

if [ ! -f "${manager_dir}/package.json" ]; then
  echo "lxe_manager MCP start failed: package.json not found under ${manager_dir}" >&2
  exit 1
fi

mkdir -p "$(dirname "${log_file}")"

log_line() {
  local message="$1"
  local timestamp
  timestamp="$(date -Is)"
  printf '[%s] %s\n' "${timestamp}" "${message}" | tee -a "${log_file}" >&2
}

cd "${manager_dir}"
restart_code=75
child_pid=""

terminate_child() {
  local signal="$1"
  local exit_code="$2"
  if [ -n "${child_pid}" ] && kill -0 "${child_pid}" 2>/dev/null; then
    log_line "received ${signal}; forwarding to lxe_manager pid=${child_pid}"
    kill -s "${signal}" "${child_pid}" 2>/dev/null || true
    wait "${child_pid}" 2>/dev/null || true
  fi
  log_line "wrapper exiting with code ${exit_code}"
  exit "${exit_code}"
}

trap 'terminate_child INT 130' INT
trap 'terminate_child TERM 143' TERM

while true; do
  log_line "starting lxe_manager: node --import tsx ./src/index.ts $*"
  node --import tsx ./src/index.ts "$@" \
    > >(tee -a "${log_file}") \
    2> >(tee -a "${log_file}" >&2) &
  child_pid=$!
  log_line "lxe_manager child pid=${child_pid}"
  set +e
  wait "${child_pid}"
  exit_code=$?
  set -e
  log_line "lxe_manager child exited code=${exit_code}"
  child_pid=""
  if [ "${exit_code}" -ne "${restart_code}" ]; then
    log_line "exit code ${exit_code} is not restart code ${restart_code}; wrapper exiting"
    exit "${exit_code}"
  fi
  log_line "restart code ${restart_code} received; restarting lxe_manager"
done
