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

usage() {
  cat >&2 <<'EOF'
usage: scripts/diagnostics/lxe_editor_leak_check.sh <sanitizer|soak|all> [options]
  --build-dir <dir>
  --scene <scene-path>
  --duration <seconds>
  --sample-interval <seconds>
  --output-dir <dir>
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
SOAK_TERMINATION_GRACE_SECONDS="${LX_LEAK_CHECK_SOAK_TERMINATION_GRACE_SECONDS:-10}"

mkdir -p "${OUTPUT_DIR}"
mkdir -p "${BUILD_DIR}"

SANITIZER_STATUS="not_run"
SOAK_STATUS="not_run"
SOAK_EDITOR_EXIT_STATUS="not_run"
SOAK_RSS_START_KB=""
SOAK_RSS_END_KB=""
SOAK_RSS_PEAK_KB=""

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

merge_asan_options

GIT_COMMIT="$(git -C "${REPO_ROOT}" rev-parse HEAD)"

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
    echo "soak_termination_grace_seconds=${SOAK_TERMINATION_GRACE_SECONDS}"
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
    return 1
  fi

  {
    echo "[sanitizer] build"
  } >> "${OUTPUT_DIR}/sanitizer.log"
  if ! "${NINJA_BIN}" -C "${BUILD_DIR}" \
      test_lxe_editor_session test_lxe_editor_interaction test_command_bus \
      lxe_editor >> "${OUTPUT_DIR}/sanitizer.log" 2>&1; then
    SANITIZER_STATUS="failed"
    return 1
  fi

  {
    echo "[sanitizer] ctest"
  } >> "${OUTPUT_DIR}/sanitizer.log"
  if ! "${CTEST_BIN}" --test-dir "${BUILD_DIR}" --output-on-failure \
      -R '^(test_lxe_editor_session|test_lxe_editor_interaction|test_command_bus)$' \
      >> "${OUTPUT_DIR}/sanitizer.log" 2>&1; then
    SANITIZER_STATUS="failed"
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

run_bounded_editor_smoke() {
  local editor_pid
  local timed_out="false"
  local editor_exit_status=0
  local editor_args=()

  if [[ -n "${SCENE_PATH}" ]]; then
    editor_args+=("${SCENE_PATH}")
  fi

  setsid "${EDITOR_BIN}" "${editor_args[@]}" &
  editor_pid=$!

  if ! wait_for_pid_exit "${editor_pid}" "${EDITOR_SMOKE_TIMEOUT_SECONDS}"; then
    timed_out="true"
    stop_process_group_with_grace "${editor_pid}" "${SOAK_TERMINATION_GRACE_SECONDS}"
  fi

  if wait "${editor_pid}"; then
    editor_exit_status=0
  else
    editor_exit_status=$?
  fi

  if [[ "${timed_out}" == "true" ]]; then
    echo "[sanitizer] editor smoke timeout after ${EDITOR_SMOKE_TIMEOUT_SECONDS}s"
    return 1
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
  local editor_args=()

  SOAK_STATUS="running"
  SOAK_EDITOR_EXIT_STATUS="running"
  SOAK_RSS_START_KB=""
  SOAK_RSS_END_KB=""
  SOAK_RSS_PEAK_KB=""

  : > "${OUTPUT_DIR}/soak.stdout.log"
  : > "${OUTPUT_DIR}/soak.stderr.log"
  printf "timestamp,rss_kb,vsz_kb,cpu_percent\n" > "${OUTPUT_DIR}/rss.csv"

  if [[ -n "${SCENE_PATH}" ]]; then
    editor_args+=("${SCENE_PATH}")
  fi

  setsid "${EDITOR_BIN}" "${editor_args[@]}" > "${OUTPUT_DIR}/soak.stdout.log" 2> "${OUTPUT_DIR}/soak.stderr.log" &
  editor_pid=$!
  end_time="$(( $(date +%s) + DURATION_SECONDS ))"

  if ! sample_process_metrics "${editor_pid}"; then
    wait "${editor_pid}" || true
    SOAK_STATUS="failed"
    SOAK_EDITOR_EXIT_STATUS="failed"
    return 1
  fi

  while kill -0 "${editor_pid}" 2>/dev/null; do
    if [[ "$(date +%s)" -ge "${end_time}" ]]; then
      break
    fi
    sleep "${SAMPLE_INTERVAL_SECONDS}"
    if kill -0 "${editor_pid}" 2>/dev/null; then
      if ! sample_process_metrics "${editor_pid}"; then
        wait "${editor_pid}" || true
        SOAK_STATUS="failed"
        SOAK_EDITOR_EXIT_STATUS="failed"
        return 1
      fi
    fi
  done

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
  if [[ "${editor_exit_status}" -eq 0 ]]; then
    SOAK_STATUS="passed"
  else
    SOAK_STATUS="failed"
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
