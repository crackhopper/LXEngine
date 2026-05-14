#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
manager_dir="${repo_root}/tools/lxe_manager"

if [ ! -f "${manager_dir}/package.json" ]; then
  echo "lxe_manager MCP start failed: package.json not found under ${manager_dir}" >&2
  exit 1
fi

cd "${manager_dir}"
restart_code=75
child_pid=""

terminate_child() {
  local signal="$1"
  local exit_code="$2"
  if [ -n "${child_pid}" ] && kill -0 "${child_pid}" 2>/dev/null; then
    kill -s "${signal}" "${child_pid}" 2>/dev/null || true
    wait "${child_pid}" 2>/dev/null || true
  fi
  exit "${exit_code}"
}

trap 'terminate_child INT 130' INT
trap 'terminate_child TERM 143' TERM

while true; do
  node --import tsx ./src/index.ts "$@" &
  child_pid=$!
  set +e
  wait "${child_pid}"
  exit_code=$?
  set -e
  child_pid=""
  if [ "${exit_code}" -ne "${restart_code}" ]; then
    exit "${exit_code}"
  fi
done
