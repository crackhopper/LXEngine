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

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
      ;;
    --scene)
      SCENE_PATH="${2:-}"
      shift 2
      ;;
    --duration)
      DURATION_SECONDS="${2:-}"
      shift 2
      ;;
    --sample-interval)
      SAMPLE_INTERVAL_SECONDS="${2:-}"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="${2:-}"
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

mkdir -p "${OUTPUT_DIR}"
mkdir -p "${BUILD_DIR}"

SANITIZER_STATUS="not_run"
SOAK_STATUS="not_run"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}"

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
    echo "asan_options=${ASAN_OPTIONS}"
    git -C "${REPO_ROOT}" rev-parse HEAD | sed 's/^/git_commit=/'
  } > "${OUTPUT_DIR}/env.txt"
}

write_summary() {
  {
    echo "mode=${MODE}"
    echo "sanitizer_status=${SANITIZER_STATUS}"
    echo "soak_status=${SOAK_STATUS}"
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
  if [[ -n "${SCENE_PATH}" ]]; then
    if ! "${EDITOR_BIN}" "${SCENE_PATH}" >> "${OUTPUT_DIR}/sanitizer.log" 2>&1; then
      SANITIZER_STATUS="failed"
      return 1
    fi
  else
    if ! "${EDITOR_BIN}" >> "${OUTPUT_DIR}/sanitizer.log" 2>&1; then
      SANITIZER_STATUS="failed"
      return 1
    fi
  fi

  SANITIZER_STATUS="passed"
}

write_stub_artifacts() {
  : > "${OUTPUT_DIR}/sanitizer.log"
  if [[ "${MODE}" == "soak" || "${MODE}" == "all" ]]; then
    : > "${OUTPUT_DIR}/soak.stdout.log"
    : > "${OUTPUT_DIR}/soak.stderr.log"
    {
      printf "timestamp,rss_kb,vsz_kb,cpu_percent\n"
      printf "stub,0,0,0\n"
    } > "${OUTPUT_DIR}/rss.csv"
  fi
}

write_env
write_stub_artifacts

if [[ "${MODE}" == "sanitizer" ]]; then
  if ! run_sanitizer_mode; then
    write_summary
    exit 1
  fi
  write_summary
  exit 0
fi

if [[ "${MODE}" == "soak" ]]; then
  write_summary
  exit 0
fi

if [[ "${MODE}" == "all" ]]; then
  write_summary
  exit 0
fi
