#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-}"
if [[ $# -gt 0 ]]; then
  shift
fi

BUILD_DIR=""
SCENE_PATH=""
DURATION_SECONDS="600"
SAMPLE_INTERVAL_SECONDS="5"
OUTPUT_DIR=""
LSAN_SUPPRESSIONS_MODE="off"

usage() {
  cat >&2 <<'EOF'
usage: scripts/diagnostics/lxe_editor_leak_check.sh <sanitizer|soak|all> [options]
  --build-dir <dir>
  --scene <scene-path>
  --duration <seconds>
  --sample-interval <seconds>
  --output-dir <dir>
  --lsan-suppressions <on|off>
EOF
}

require_option_value() {
  local option_name="${1}"
  local option_value="${2:-}"
  if [[ -z "${option_value}" || "${option_value}" == -* ]]; then
    echo "missing value for ${option_name}" >&2
    usage
    exit 2
  fi
  printf '%s' "${option_value}"
}

require_toggle_value() {
  local option_name="${1}"
  local option_value="${2:-}"
  option_value="$(require_option_value "${option_name}" "${option_value}")"
  case "${option_value}" in
    on|off)
      printf '%s' "${option_value}"
      ;;
    *)
      echo "invalid value for ${option_name}: ${option_value} (expected on|off)" >&2
      usage
      exit 2
      ;;
  esac
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$(require_option_value "$1" "${2:-}")"
      shift 2
      ;;
    --scene)
      SCENE_PATH="$(require_option_value "$1" "${2:-}")"
      shift 2
      ;;
    --duration)
      DURATION_SECONDS="$(require_option_value "$1" "${2:-}")"
      shift 2
      ;;
    --sample-interval)
      SAMPLE_INTERVAL_SECONDS="$(require_option_value "$1" "${2:-}")"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="$(require_option_value "$1" "${2:-}")"
      shift 2
      ;;
    --lsan-suppressions)
      LSAN_SUPPRESSIONS_MODE="$(require_toggle_value "$1" "${2:-}")"
      shift 2
      ;;
    *)
      usage
      exit 2
      ;;
  esac
done

case "${MODE}" in
  sanitizer|soak|all)
    ;;
  *)
    usage
    exit 2
    ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"

if [[ -z "${OUTPUT_DIR}" ]]; then
  OUTPUT_DIR="${REPO_ROOT}/artifacts/diagnostics/lxe_editor/${TIMESTAMP}"
fi

if [[ -z "${BUILD_DIR}" ]]; then
  BUILD_DIR="${REPO_ROOT}/build-asan"
fi

CMAKE_BIN="${LX_LEAK_CHECK_CMAKE:-cmake}"
CTEST_BIN="${LX_LEAK_CHECK_CTEST:-ctest}"
NINJA_BIN="${LX_LEAK_CHECK_NINJA:-ninja}"
EDITOR_BIN="${LX_LEAK_CHECK_LXE_EDITOR_BIN:-${BUILD_DIR}/src/demos/lxe_editor/lxe_editor}"
EDITOR_SMOKE_TIMEOUT_SECONDS="${LX_LEAK_CHECK_EDITOR_SMOKE_TIMEOUT_SECONDS:-120}"
EDITOR_SMOKE_DWELL_SECONDS="${LX_LEAK_CHECK_EDITOR_SMOKE_DWELL_SECONDS:-3}"
SOAK_TERMINATION_GRACE_SECONDS="${LX_LEAK_CHECK_SOAK_TERMINATION_GRACE_SECONDS:-10}"
EDITOR_API_HOST="${LX_LEAK_CHECK_EDITOR_API_HOST:-127.0.0.1}"
EDITOR_API_PORT="${LX_LEAK_CHECK_EDITOR_API_PORT:-37681}"
EDITOR_API_TOKEN_FILE="${LX_LEAK_CHECK_EDITOR_API_TOKEN_FILE:-${REPO_ROOT}/data/lxe_editor/api_token.txt}"
LSAN_SUPPRESSIONS_FILE="${REPO_ROOT}/scripts/diagnostics/lsan/lxe_editor.supp"

mkdir -p "${OUTPUT_DIR}"
mkdir -p "${BUILD_DIR}"

SANITIZER_STATUS="not_run"
SOAK_STATUS="not_run"
SOAK_EDITOR_EXIT_STATUS="not_run"
SOAK_RSS_START_KB=""
SOAK_RSS_END_KB=""
SOAK_RSS_PEAK_KB=""
FAILURE_KIND="none"
FAILED_STEP=""
FAILURE_REASON=""
LAUNCHED_EDITOR_PID=""

merge_asan_options() {
  local merged_options=""
  local option
  local -a asan_parts=()

  if [[ -n "${ASAN_OPTIONS:-}" ]]; then
    IFS=':' read -r -a asan_parts <<< "${ASAN_OPTIONS}"
    for option in "${asan_parts[@]}"; do
      if [[ -z "${option}" || "${option}" == detect_leaks=* ]]; then
        continue
      fi
      if [[ -n "${merged_options}" ]]; then
        merged_options+=":"
      fi
      merged_options+="${option}"
    done
  fi

  if [[ -n "${merged_options}" ]]; then
    merged_options+=":"
  fi
  merged_options+="detect_leaks=1"
  export ASAN_OPTIONS="${merged_options}"
}

merge_lsan_options() {
  local merged_options=""
  local option
  local -a lsan_parts=()

  if [[ -n "${LSAN_OPTIONS:-}" ]]; then
    IFS=':' read -r -a lsan_parts <<< "${LSAN_OPTIONS}"
    for option in "${lsan_parts[@]}"; do
      if [[ -z "${option}" || "${option}" == suppressions=* ]]; then
        continue
      fi
      if [[ -n "${merged_options}" ]]; then
        merged_options+=":"
      fi
      merged_options+="${option}"
    done
  fi

  if [[ "${LSAN_SUPPRESSIONS_MODE}" == "on" ]]; then
    if [[ -n "${merged_options}" ]]; then
      merged_options+=":"
    fi
    merged_options+="suppressions=${LSAN_SUPPRESSIONS_FILE}"
  fi

  export LSAN_OPTIONS="${merged_options}"
}

merge_asan_options
merge_lsan_options

GIT_COMMIT="$(git -C "${REPO_ROOT}" rev-parse HEAD)"

sanitize_summary_value() {
  local value="${1:-}"
  value="${value//$'\n'/ }"
  value="${value//$'\r'/ }"
  printf '%s' "${value}"
}

record_failure() {
  local kind="${1}"
  local step="${2}"
  local reason="${3:-}"

  if [[ "${FAILURE_KIND}" != "none" ]]; then
    return 0
  fi

  FAILURE_KIND="${kind}"
  FAILED_STEP="${step}"
  FAILURE_REASON="$(sanitize_summary_value "${reason}")"
}

editor_api_token_available() {
  [[ -s "${EDITOR_API_TOKEN_FILE}" ]]
}

log_indicates_environment_failure() {
  local log_file="${1}"
  [[ -f "${log_file}" ]] || return 1
  grep -Eiq \
    'No available video device|video device|unable to open display|cannot open display|failed to create api token directory|failed to write api token file|missing API token|missing api token|token missing|graphics prerequisite|display server' \
    "${log_file}"
}

record_environment_or_control_failure() {
  local step="${1}"
  local control_reason="${2}"
  local environment_reason="${3}"
  local log_file="${4:-}"

  if ! editor_api_token_available; then
    record_failure "environment_failure" "${step}" \
      "editor API token missing before readiness"
    return 0
  fi

  if [[ -n "${log_file}" ]] && log_indicates_environment_failure "${log_file}"; then
    record_failure "environment_failure" "${step}" "${environment_reason}"
    return 0
  fi

  record_failure "smoke_control_failure" "${step}" "${control_reason}"
}

write_env() {
  {
    echo "mode=${MODE}"
    echo "repo_root=${REPO_ROOT}"
    echo "build_dir=${BUILD_DIR}"
    echo "scene=${SCENE_PATH}"
    echo "duration=${DURATION_SECONDS}"
    echo "sample_interval=${SAMPLE_INTERVAL_SECONDS}"
    echo "cmake_bin=${CMAKE_BIN}"
    echo "ctest_bin=${CTEST_BIN}"
    echo "ninja_bin=${NINJA_BIN}"
    echo "editor_bin=${EDITOR_BIN}"
    echo "editor_smoke_timeout_seconds=${EDITOR_SMOKE_TIMEOUT_SECONDS}"
    echo "editor_smoke_dwell_seconds=${EDITOR_SMOKE_DWELL_SECONDS}"
    echo "soak_termination_grace_seconds=${SOAK_TERMINATION_GRACE_SECONDS}"
    echo "editor_api_host=${EDITOR_API_HOST}"
    echo "editor_api_port=${EDITOR_API_PORT}"
    echo "editor_api_token_file=${EDITOR_API_TOKEN_FILE}"
    echo "lsan_suppressions=${LSAN_SUPPRESSIONS_MODE}"
    echo "lsan_suppressions_file=${LSAN_SUPPRESSIONS_FILE}"
    echo "lsan_options=${LSAN_OPTIONS}"
    echo "asan_options=${ASAN_OPTIONS}"
    echo "git_commit=${GIT_COMMIT}"
  } > "${OUTPUT_DIR}/env.txt"
}

write_summary() {
  {
    echo "mode=${MODE}"
    echo "git_commit=${GIT_COMMIT}"
    echo "sanitizer_status=${SANITIZER_STATUS}"
    echo "soak_status=${SOAK_STATUS}"
    echo "duration_seconds=${DURATION_SECONDS}"
    echo "soak_exit_status=${SOAK_EDITOR_EXIT_STATUS}"
    echo "rss_start_kb=${SOAK_RSS_START_KB}"
    echo "rss_end_kb=${SOAK_RSS_END_KB}"
    echo "rss_peak_kb=${SOAK_RSS_PEAK_KB}"
    echo "failure_kind=${FAILURE_KIND}"
    echo "failed_step=${FAILED_STEP}"
    echo "failure_reason=${FAILURE_REASON}"
  } > "${OUTPUT_DIR}/summary.txt"
}

run_sanitizer_mode() {
  SANITIZER_STATUS="running"

  {
    echo "[sanitizer] asan_options=${ASAN_OPTIONS}"
    echo "[sanitizer] configure"
  } >> "${OUTPUT_DIR}/sanitizer.log"
  if ! "${CMAKE_BIN}" -S "${REPO_ROOT}" -B "${BUILD_DIR}" -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DLX_ENABLE_SANITIZERS=ON \
      -DLX_BUILD_DEMOS=ON \
      >> "${OUTPUT_DIR}/sanitizer.log" 2>&1; then
    SANITIZER_STATUS="failed"
    record_failure "setup_failure" "configure" "cmake configure failed"
    return 1
  fi

  {
    echo "[sanitizer] build"
  } >> "${OUTPUT_DIR}/sanitizer.log"
  if ! "${NINJA_BIN}" -C "${BUILD_DIR}" \
      test_lxe_editor_session test_lxe_editor_interaction test_command_bus \
      lxe_editor >> "${OUTPUT_DIR}/sanitizer.log" 2>&1; then
    SANITIZER_STATUS="failed"
    record_failure "build_failure" "build" "ninja build failed"
    return 1
  fi

  {
    echo "[sanitizer] ctest"
  } >> "${OUTPUT_DIR}/sanitizer.log"
  if ! "${CTEST_BIN}" --test-dir "${BUILD_DIR}" --output-on-failure \
      -R '^(test_lxe_editor_session|test_lxe_editor_interaction|test_command_bus)$' \
      >> "${OUTPUT_DIR}/sanitizer.log" 2>&1; then
    SANITIZER_STATUS="failed"
    if grep -Eq 'AddressSanitizer|LeakSanitizer' "${OUTPUT_DIR}/sanitizer.log"; then
      record_failure "sanitizer_finding" "ctest" \
        "ctest failed with sanitizer signature in sanitizer.log"
    elif log_indicates_environment_failure "${OUTPUT_DIR}/sanitizer.log"; then
      record_failure "environment_failure" "ctest" \
        "environment prerequisite missing during ctest"
    else
      record_failure "test_failure" "ctest" \
        "ctest failed without sanitizer signature"
    fi
    return 1
  fi

  {
    echo "[sanitizer] editor smoke"
  } >> "${OUTPUT_DIR}/sanitizer.log"
  if ! run_bounded_editor_smoke >> "${OUTPUT_DIR}/sanitizer.log" 2>&1; then
    SANITIZER_STATUS="failed"
    return 1
  fi

  SANITIZER_STATUS="passed"
}

launch_editor_process() {
  local stdout_file="${1:-}"
  local stderr_file="${2:-}"

  pushd "${REPO_ROOT}" >/dev/null
  if [[ -n "${stdout_file}" || -n "${stderr_file}" ]]; then
    setsid "${EDITOR_BIN}" \
      --api-host "${EDITOR_API_HOST}" \
      --api-port "${EDITOR_API_PORT}" \
      > "${stdout_file}" 2> "${stderr_file}" &
  else
    setsid "${EDITOR_BIN}" \
      --api-host "${EDITOR_API_HOST}" \
      --api-port "${EDITOR_API_PORT}" &
  fi
  LAUNCHED_EDITOR_PID="$!"
  popd >/dev/null
}

wait_for_pid_exit() {
  local pid="${1}"
  local timeout_seconds="${2}"
  local deadline

  deadline="$(( $(date +%s) + timeout_seconds ))"
  while kill -0 "${pid}" 2>/dev/null; do
    if [[ "$(date +%s)" -ge "${deadline}" ]]; then
      return 1
    fi
    sleep 1
  done
  return 0
}

stop_process_group_with_grace() {
  local pid="${1}"
  local grace_seconds="${2}"

  if ! kill -0 "${pid}" 2>/dev/null; then
    return 0
  fi

  kill -TERM -- "-${pid}" || true
  if wait_for_pid_exit "${pid}" "${grace_seconds}"; then
    return 0
  fi

  kill -KILL -- "-${pid}" || true
  wait_for_pid_exit "${pid}" 5 || true
}

build_scene_load_command() {
  local escaped_path="${SCENE_PATH//\\/\\\\}"
  escaped_path="${escaped_path//\"/\\\"}"
  printf 'scene load "%s"' "${escaped_path}"
}

editor_api_ready_check() {
  python3 - "${EDITOR_API_HOST}" "${EDITOR_API_PORT}" "${EDITOR_API_TOKEN_FILE}" <<'PY'
import http.client
import pathlib
import sys

host, port_text, token_path_text = sys.argv[1:4]
port = int(port_text)
token_path = pathlib.Path(token_path_text)
token = token_path.read_text(encoding="utf-8").strip() if token_path.exists() else ""
if not token:
    raise SystemExit(1)

health = http.client.HTTPConnection(host, port, timeout=2)
try:
    health.request("GET", "/health")
    health_response = health.getresponse()
    health_response.read()
    if health_response.status != 200:
        raise SystemExit(1)
finally:
    health.close()

state = http.client.HTTPConnection(host, port, timeout=2)
try:
    state.request(
        "GET",
        "/api/state/summary",
        headers={"Authorization": f"Bearer {token}"},
    )
    state_response = state.getresponse()
    state_response.read()
    if state_response.status != 200:
        raise SystemExit(1)
finally:
    state.close()
PY
}

wait_for_editor_api_ready() {
  local pid="${1}"
  local timeout_seconds="${2}"
  local deadline

  deadline="$(( $(date +%s) + timeout_seconds ))"
  while true; do
    if editor_api_ready_check; then
      return 0
    fi
    if ! kill -0 "${pid}" 2>/dev/null; then
      return 2
    fi
    if [[ "$(date +%s)" -ge "${deadline}" ]]; then
      return 1
    fi
    sleep 1
  done
}

wait_for_pid_dwell() {
  local pid="${1}"
  local dwell_seconds="${2}"
  local deadline

  if [[ "${dwell_seconds}" -le 0 ]]; then
    return 0
  fi

  deadline="$(( $(date +%s) + dwell_seconds ))"
  while [[ "$(date +%s)" -lt "${deadline}" ]]; do
    if ! kill -0 "${pid}" 2>/dev/null; then
      return 1
    fi
    sleep 1
  done
  return 0
}

post_editor_command() {
  local line="${1}"

  python3 - "${EDITOR_API_HOST}" "${EDITOR_API_PORT}" "${EDITOR_API_TOKEN_FILE}" "${line}" <<'PY'
import http.client
import json
import pathlib
import sys

host, port_text, token_path_text, line = sys.argv[1:5]
port = int(port_text)
token_path = pathlib.Path(token_path_text)
token = token_path.read_text(encoding="utf-8").strip() if token_path.exists() else ""
if not token:
    sys.stderr.write("missing API token\n")
    raise SystemExit(1)

conn = http.client.HTTPConnection(host, port, timeout=3)
try:
    body = json.dumps({"line": line})
    conn.request(
        "POST",
        "/api/command",
        body=body,
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
    )
    response = conn.getresponse()
    payload = response.read().decode("utf-8", errors="replace")
    if response.status < 200 or response.status >= 300:
        sys.stderr.write(payload + "\n")
        raise SystemExit(1)
    parsed = json.loads(payload)
    if not parsed.get("ok", False):
        error = parsed.get("error") or {}
        message = error.get("message") or parsed.get("message") or "command failed"
        sys.stderr.write(message + "\n")
        raise SystemExit(1)
    sys.stdout.write(payload)
finally:
    conn.close()
PY
}

run_bounded_editor_smoke() {
  local editor_pid
  local ready_status
  local editor_exit_status=0

  launch_editor_process
  editor_pid="${LAUNCHED_EDITOR_PID}"

  wait_for_editor_api_ready "${editor_pid}" "${EDITOR_SMOKE_TIMEOUT_SECONDS}"
  ready_status=$?

  if [[ "${ready_status}" -eq 1 ]]; then
    echo "[sanitizer] editor smoke timeout after ${EDITOR_SMOKE_TIMEOUT_SECONDS}s"
    stop_process_group_with_grace "${editor_pid}" "${SOAK_TERMINATION_GRACE_SECONDS}"
    wait "${editor_pid}" || true
    record_environment_or_control_failure \
      "editor_smoke_timeout" \
      "editor smoke timeout after ${EDITOR_SMOKE_TIMEOUT_SECONDS}s" \
      "environment prerequisite missing before editor API became ready" \
      "${OUTPUT_DIR}/sanitizer.log"
    return 1
  fi

  if [[ "${ready_status}" -eq 2 ]]; then
    if wait "${editor_pid}"; then
      editor_exit_status=0
    else
      editor_exit_status=$?
    fi
    record_environment_or_control_failure \
      "editor_smoke_api_ready" \
      "editor exited before API became ready with status ${editor_exit_status}" \
      "environment prerequisite missing before editor API became ready" \
      "${OUTPUT_DIR}/sanitizer.log"
    return 1
  fi

  if [[ "${ready_status}" -eq 0 ]]; then
    if [[ -n "${SCENE_PATH}" ]]; then
      if ! post_editor_command "$(build_scene_load_command)"; then
        stop_process_group_with_grace "${editor_pid}" "${SOAK_TERMINATION_GRACE_SECONDS}"
        wait "${editor_pid}" || true
        record_failure "smoke_control_failure" "editor_smoke_scene_load" \
          "failed to load scene via editor API"
        return 1
      fi
    fi

    if ! wait_for_pid_dwell "${editor_pid}" "${EDITOR_SMOKE_DWELL_SECONDS}"; then
      stop_process_group_with_grace "${editor_pid}" "${SOAK_TERMINATION_GRACE_SECONDS}"
      wait "${editor_pid}" || true
      record_environment_or_control_failure \
        "editor_smoke_dwell" \
        "editor exited during smoke dwell before normal quit" \
        "environment prerequisite missing during smoke dwell" \
        "${OUTPUT_DIR}/sanitizer.log"
      return 1
    fi

    if ! post_editor_command "quit"; then
      stop_process_group_with_grace "${editor_pid}" "${SOAK_TERMINATION_GRACE_SECONDS}"
      wait "${editor_pid}" || true
      record_failure "smoke_control_failure" "editor_smoke_quit" \
        "failed to send quit through editor API"
      return 1
    fi

    if ! wait_for_pid_exit "${editor_pid}" "${EDITOR_SMOKE_TIMEOUT_SECONDS}"; then
      echo "[sanitizer] editor smoke timeout after ${EDITOR_SMOKE_TIMEOUT_SECONDS}s"
      stop_process_group_with_grace "${editor_pid}" "${SOAK_TERMINATION_GRACE_SECONDS}"
      wait "${editor_pid}" || true
      record_failure "smoke_control_failure" "editor_smoke_timeout" \
        "editor smoke timeout after ${EDITOR_SMOKE_TIMEOUT_SECONDS}s"
      return 1
    fi
  fi

  if wait "${editor_pid}"; then
    editor_exit_status=0
  else
    editor_exit_status=$?
  fi

  if [[ "${editor_exit_status}" -ne 0 ]]; then
    record_failure "smoke_control_failure" "editor_smoke_exit" \
      "editor exited with status ${editor_exit_status}"
  fi

  return "${editor_exit_status}"
}

sample_process_metrics() {
  local pid="${1}"
  local timestamp
  local sample
  local rss_kb
  local vsz_kb
  local cpu_percent

  timestamp="$(date +%s)"
  sample="$(ps -o rss=,vsz=,%cpu= -p "${pid}" | awk '{print $1","$2","$3}')" || return 1

  IFS=',' read -r rss_kb vsz_kb cpu_percent <<< "${sample}"
  printf "%s,%s,%s,%s\n" "${timestamp}" "${rss_kb}" "${vsz_kb}" "${cpu_percent}" >> "${OUTPUT_DIR}/rss.csv"

  if [[ -z "${SOAK_RSS_START_KB}" ]]; then
    SOAK_RSS_START_KB="${rss_kb}"
  fi
  SOAK_RSS_END_KB="${rss_kb}"
  if [[ -z "${SOAK_RSS_PEAK_KB}" || "${rss_kb}" -gt "${SOAK_RSS_PEAK_KB}" ]]; then
    SOAK_RSS_PEAK_KB="${rss_kb}"
  fi
}

run_soak_mode() {
  local end_time
  local editor_pid
  local editor_exit_status
  local timed_out="false"
  local ready_status
  local duration_completed="false"

  SOAK_STATUS="running"
  SOAK_EDITOR_EXIT_STATUS="running"
  SOAK_RSS_START_KB=""
  SOAK_RSS_END_KB=""
  SOAK_RSS_PEAK_KB=""

  : > "${OUTPUT_DIR}/soak.stdout.log"
  : > "${OUTPUT_DIR}/soak.stderr.log"
  printf "timestamp,rss_kb,vsz_kb,cpu_percent\n" > "${OUTPUT_DIR}/rss.csv"

  launch_editor_process "${OUTPUT_DIR}/soak.stdout.log" "${OUTPUT_DIR}/soak.stderr.log"
  editor_pid="${LAUNCHED_EDITOR_PID}"

  if [[ -n "${SCENE_PATH}" ]]; then
    wait_for_editor_api_ready "${editor_pid}" "${EDITOR_SMOKE_TIMEOUT_SECONDS}"
    ready_status=$?

    if [[ "${ready_status}" -eq 2 ]]; then
      wait "${editor_pid}" || true
      SOAK_STATUS="failed"
      SOAK_EDITOR_EXIT_STATUS="failed"
      record_failure "smoke_control_failure" "soak_scene_load" \
        "editor exited before scene load API became ready"
      return 1
    fi

    if [[ "${ready_status}" -eq 1 ]]; then
      stop_process_group_with_grace "${editor_pid}" "${SOAK_TERMINATION_GRACE_SECONDS}"
      wait "${editor_pid}" || true
      SOAK_STATUS="failed"
      SOAK_EDITOR_EXIT_STATUS="failed"
      record_failure "smoke_control_failure" "soak_scene_load" \
        "timed out waiting for editor API readiness before scene load"
      return 1
    fi

    if ! post_editor_command "$(build_scene_load_command)"; then
      stop_process_group_with_grace "${editor_pid}" "${SOAK_TERMINATION_GRACE_SECONDS}"
      wait "${editor_pid}" || true
      SOAK_STATUS="failed"
      SOAK_EDITOR_EXIT_STATUS="failed"
      record_failure "smoke_control_failure" "soak_scene_load" \
        "failed to load scene via editor API"
      return 1
    fi
  fi

  end_time="$(( $(date +%s) + DURATION_SECONDS ))"

  if ! sample_process_metrics "${editor_pid}"; then
    wait "${editor_pid}" || true
    SOAK_STATUS="failed"
    SOAK_EDITOR_EXIT_STATUS="failed"
    record_failure "smoke_control_failure" "soak_launch" \
      "editor exited before soak sampling could start"
    return 1
  fi

  while kill -0 "${editor_pid}" 2>/dev/null; do
    if [[ "$(date +%s)" -ge "${end_time}" ]]; then
      duration_completed="true"
      break
    fi
    sleep "${SAMPLE_INTERVAL_SECONDS}"
    if kill -0 "${editor_pid}" 2>/dev/null; then
      if ! sample_process_metrics "${editor_pid}"; then
        wait "${editor_pid}" || true
        SOAK_STATUS="failed"
        SOAK_EDITOR_EXIT_STATUS="failed"
        record_failure "smoke_control_failure" "soak_sample" \
          "failed to sample editor process metrics"
        return 1
      fi
    fi
  done

  if [[ "${duration_completed}" != "true" && "$(date +%s)" -ge "${end_time}" ]]; then
    duration_completed="true"
  fi

  if kill -0 "${editor_pid}" 2>/dev/null; then
    timed_out="true"
    stop_process_group_with_grace "${editor_pid}" "${SOAK_TERMINATION_GRACE_SECONDS}"
  fi

  if wait "${editor_pid}"; then
    editor_exit_status=0
  else
    editor_exit_status=$?
  fi

  if [[ "${timed_out}" == "true" ]]; then
    SOAK_EDITOR_EXIT_STATUS="timeout"
    SOAK_STATUS="passed"
    return 0
  fi

  SOAK_EDITOR_EXIT_STATUS="${editor_exit_status}"
  if [[ "${editor_exit_status}" -eq 0 && "${duration_completed}" == "true" ]]; then
    SOAK_STATUS="passed"
  elif [[ "${editor_exit_status}" -eq 0 ]]; then
    SOAK_STATUS="failed"
    record_failure "smoke_control_failure" "soak_duration" \
      "editor exited before soak duration completed"
    return 1
  else
    SOAK_STATUS="failed"
    record_failure "smoke_control_failure" "soak_exit" \
      "editor exited during soak with status ${editor_exit_status}"
    return 1
  fi
}

write_env

case "${MODE}" in
  sanitizer)
    if ! run_sanitizer_mode; then
      write_summary
      exit 1
    fi
    ;;
  soak)
    if ! run_soak_mode; then
      write_summary
      exit 1
    fi
    ;;
  all)
    if ! run_sanitizer_mode; then
      :
    fi
    if ! run_soak_mode; then
      :
    fi
    ;;
esac

write_summary

if [[ "${SANITIZER_STATUS}" == "failed" || "${SOAK_STATUS}" == "failed" ]]; then
  exit 1
fi
exit 0
