#!/usr/bin/env bash

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

_lxe_editor_default_executable() {
  local repo_root="$1"
  local candidate
  for candidate in \
    "${repo_root}/build/src/demos/lxe_editor/lxe_editor" \
    "${repo_root}/build-release/src/demos/lxe_editor/lxe_editor"
  do
    if [ -x "${candidate}" ]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done
  return 1
}

_lxe_editor_runtime_pid_alive() {
  local runtime_state_path="$1"
  local pid
  pid="$(python3 - "${runtime_state_path}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
values = {}
for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = raw_line.strip()
    if not line or line.startswith("#") or ":" not in line:
        continue
    key, value = line.split(":", 1)
    values[key.strip()] = value.strip().strip("'\"")
print(values.get("pid", "").strip())
PY
)"
  if [ -z "${pid}" ]; then
    return 2
  fi
  if kill -0 "${pid}" 2>/dev/null; then
    return 0
  fi
  return 1
}

_lxe_editor_start_local() {
  local repo_root="$1"
  local runtime_root="$2"
  local executable="${LXE_EDITOR_EXECUTABLE:-}"
  if [ -z "${executable}" ]; then
    executable="$(_lxe_editor_default_executable "${repo_root}")" || {
      echo "lxe_editor MCP target: runtime state is missing and no local lxe_editor executable was found. Set LXE_EDITOR_EXECUTABLE or build build/src/demos/lxe_editor/lxe_editor first." >&2
      return 1
    }
  fi
  if [ ! -x "${executable}" ]; then
    echo "lxe_editor MCP target: configured lxe_editor executable is not runnable (${executable})" >&2
    return 1
  fi

  local -a args=()
  if [ -n "${LXE_EDITOR_AUTOSTART_ARGS:-}" ]; then
    # shellcheck disable=SC2206
    args=(${LXE_EDITOR_AUTOSTART_ARGS})
  fi

  local log_path="${LXE_EDITOR_AUTOSTART_LOG:-${runtime_root}/data/lxe_editor/autostart.log}"
  mkdir -p "$(dirname "${log_path}")"
  local -a launch=("${executable}")
  if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
    launch=("xvfb-run" "-a" "${executable}")
  fi
  (
    export LX_RUNTIME_ROOT="${runtime_root}"
    nohup "${launch[@]}" "${args[@]}" >>"${log_path}" 2>&1 &
  )
  return 0
}

_lxe_editor_wait_for_runtime_state() {
  local runtime_state_path="$1"
  local timeout_s="${LXE_EDITOR_AUTOSTART_TIMEOUT_S:-15}"
  local deadline
  deadline="$(python3 - "${timeout_s}" <<'PY'
import sys
import time
print(time.time() + float(sys.argv[1]))
PY
)"

  while true; do
    if [ -f "${runtime_state_path}" ]; then
      return 0
    fi
    if ! python3 - "${deadline}" <<'PY'
import sys
import time
sys.exit(0 if time.time() < float(sys.argv[1]) else 1)
PY
    then
      break
    fi
    sleep 0.1
  done
  return 1
}

repo_root="$(_lxe_editor_repo_root)"
runtime_root="${LXE_EDITOR_RUNTIME_ROOT:-${LX_RUNTIME_ROOT:-${repo_root}}}"
config_path="${LXE_EDITOR_CODEX_CONFIG_PATH:-${repo_root}/.codex/config.toml}"
runtime_state_path="${runtime_root}/data/lxe_editor/runtime_state.yaml"

if [ ! -f "${runtime_state_path}" ]; then
  _lxe_editor_start_local "${repo_root}" "${runtime_root}" || {
    return 1 2>/dev/null || exit 1
  }
  _lxe_editor_wait_for_runtime_state "${runtime_state_path}" || {
    if [ -f "${runtime_root}/data/lxe_editor/autostart.log" ]; then
      echo "lxe_editor MCP target: timed out waiting for runtime state at ${runtime_state_path} after auto-start; recent autostart.log:" >&2
      tail -n 20 "${runtime_root}/data/lxe_editor/autostart.log" >&2
    else
      echo "lxe_editor MCP target: timed out waiting for runtime state at ${runtime_state_path} after auto-start" >&2
    fi
    return 1 2>/dev/null || exit 1
  }
fi

mapfile -t local_state < <(
  python3 - "${runtime_state_path}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
values = {}
for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = raw_line.strip()
    if not line or line.startswith("#") or ":" not in line:
        continue
    key, value = line.split(":", 1)
    values[key.strip()] = value.strip().strip("'\"")

url = values.get("mcpUrl", "").strip()
token_file = values.get("tokenFile", "").strip()
print(url)
print(token_file)
PY
)

mcp_url="${local_state[0]}"
token_file="${local_state[1]}"

_lxe_editor_runtime_pid_alive "${runtime_state_path}"
runtime_pid_status=$?
if [ -f "${runtime_state_path}" ] && [ "${runtime_pid_status}" -eq 1 ]; then
  rm -f "${runtime_state_path}"
  _lxe_editor_start_local "${repo_root}" "${runtime_root}" || {
    return 1 2>/dev/null || exit 1
  }
  _lxe_editor_wait_for_runtime_state "${runtime_state_path}" || {
    if [ -f "${runtime_root}/data/lxe_editor/autostart.log" ]; then
      echo "lxe_editor MCP target: timed out waiting for fresh runtime state at ${runtime_state_path} after stale state cleanup; recent autostart.log:" >&2
      tail -n 20 "${runtime_root}/data/lxe_editor/autostart.log" >&2
    else
      echo "lxe_editor MCP target: timed out waiting for fresh runtime state at ${runtime_state_path} after stale state cleanup" >&2
    fi
    return 1 2>/dev/null || exit 1
  }
  mapfile -t local_state < <(
    python3 - "${runtime_state_path}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
values = {}
for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = raw_line.strip()
    if not line or line.startswith("#") or ":" not in line:
        continue
    key, value = line.split(":", 1)
    values[key.strip()] = value.strip().strip("'\"")

url = values.get("mcpUrl", "").strip()
token_file = values.get("tokenFile", "").strip()
print(url)
print(token_file)
PY
  )
  mcp_url="${local_state[0]}"
  token_file="${local_state[1]}"
fi

if [ -z "${mcp_url}" ]; then
  echo "lxe_editor MCP target: runtime state does not provide mcpUrl" >&2
  return 1 2>/dev/null || exit 1
fi
if [ -z "${token_file}" ] || [ ! -f "${token_file}" ]; then
  if [ "${token_file##*/}" = "automation_token.txt" ]; then
    echo "lxe_editor MCP target: runtime state still points at legacy automation token (${token_file}); restart with the current lxe_editor build so it writes api_token.txt" >&2
  else
    echo "lxe_editor MCP target: token file is missing (${token_file})" >&2
  fi
  return 1 2>/dev/null || exit 1
fi

export LXE_EDITOR_MCP_BEARER_TOKEN
LXE_EDITOR_MCP_BEARER_TOKEN="$(tr -d '\r\n' < "${token_file}")"
if [ -z "${LXE_EDITOR_MCP_BEARER_TOKEN}" ]; then
  echo "lxe_editor MCP target: token file is empty (${token_file})" >&2
  return 1 2>/dev/null || exit 1
fi

mkdir -p "$(dirname "${config_path}")"
_lxe_editor_write_codex_config "${config_path}" "${mcp_url}"
echo "lxe_editor MCP target: local ${mcp_url}"
