# lxe_editor Leak Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Linux-first diagnostics entrypoint for `lxe_editor` that can run sanitizer-based checks, long-running soak sampling, and a combined mode while emitting stable artifacts and summaries.

**Architecture:** Keep the public developer entrypoint as `scripts/diagnostics/lxe_editor_leak_check.sh`, and make it responsible for orchestration only: parse mode/args, record environment, invoke build/test/editor commands, sample process metrics, and write stable artifacts. Keep implementation testable by allowing command-path overrides through environment variables in the script, then validate the contract with Python `unittest` tests that run the script against stub executables in a temp directory.

**Tech Stack:** Bash, Python `unittest`, CMake, Ninja, existing `lxe_editor` executable and integration tests, Linux `/proc` + `ps`

---

## File Map

- Create: `scripts/diagnostics/lxe_editor_leak_check.sh`
  - Unified Linux diagnostics entrypoint with `sanitizer`, `soak`, and `all` modes.
- Create: `scripts/tests/test_lxe_editor_leak_check.py`
  - Script-contract regression tests using stub executables and temp artifact directories.
- Modify: `docs/superpowers/specs/2026-05-12-lxe-editor-leak-diagnostics-design.md`
  - Only if implementation clarifies a narrow detail not explicit in the spec. Skip if no clarification is needed.

## Task 1: Lock Script Contract With Failing Tests

**Files:**
- Create: `scripts/tests/test_lxe_editor_leak_check.py`
- Test: `scripts/tests/test_lxe_editor_leak_check.py`

- [ ] **Step 1: Write the failing Python test file for usage, sanitizer, soak, and all-mode artifacts**

```python
#!/usr/bin/env python3
from __future__ import annotations

import os
import stat
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "diagnostics" / "lxe_editor_leak_check.sh"


class LxeEditorLeakCheckScriptTest(unittest.TestCase):
    def test_rejects_unknown_mode_with_usage(self) -> None:
        completed = subprocess.run(
            ["bash", str(SCRIPT_PATH), "unknown-mode"],
            text=True,
            capture_output=True,
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("usage:", completed.stderr)

    def test_sanitizer_mode_writes_env_summary_and_log(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="1")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
                    "bash",
                    str(SCRIPT_PATH),
                    "sanitizer",
                    "--output-dir",
                    str(output_dir),
                    "--build-dir",
                    str(root / "build-asan"),
                ],
                text=True,
                capture_output=True,
                env=env,
                check=False,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertTrue((output_dir / "env.txt").exists())
            self.assertTrue((output_dir / "summary.txt").exists())
            self.assertTrue((output_dir / "sanitizer.log").exists())
            self.assertIn("mode=sanitizer", (output_dir / "summary.txt").read_text())

    def test_soak_mode_writes_rss_csv_and_process_logs(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="2")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
                    "bash",
                    str(SCRIPT_PATH),
                    "soak",
                    "--output-dir",
                    str(output_dir),
                    "--duration",
                    "2",
                    "--sample-interval",
                    "1",
                ],
                text=True,
                capture_output=True,
                env=env,
                check=False,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertTrue((output_dir / "rss.csv").exists())
            self.assertTrue((output_dir / "soak.stdout.log").exists())
            self.assertTrue((output_dir / "soak.stderr.log").exists())
            rss_lines = (output_dir / "rss.csv").read_text().strip().splitlines()
            self.assertGreaterEqual(len(rss_lines), 2)

    def test_all_mode_runs_both_paths_and_reports_them(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="1")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
                    "bash",
                    str(SCRIPT_PATH),
                    "all",
                    "--output-dir",
                    str(output_dir),
                    "--duration",
                    "1",
                    "--sample-interval",
                    "1",
                ],
                text=True,
                capture_output=True,
                env=env,
                check=False,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            summary = (output_dir / "summary.txt").read_text()
            self.assertIn("mode=all", summary)
            self.assertIn("sanitizer_status=passed", summary)
            self.assertIn("soak_status=passed", summary)

    def _script_env(self, tools: dict[str, Path]) -> dict[str, str]:
        env = os.environ.copy()
        env["LX_LEAK_CHECK_CMAKE"] = str(tools["cmake"])
        env["LX_LEAK_CHECK_CTEST"] = str(tools["ctest"])
        env["LX_LEAK_CHECK_NINJA"] = str(tools["ninja"])
        env["LX_LEAK_CHECK_LXE_EDITOR_BIN"] = str(tools["editor"])
        return env

    def _write_stub_tools(self, root: Path, *, editor_sleep: str) -> dict[str, Path]:
        bin_dir = root / "bin"
        bin_dir.mkdir(parents=True, exist_ok=True)
        cmake = self._write_executable(
            bin_dir / "cmake",
            """#!/usr/bin/env bash
echo "stub cmake $*" >> "${TMP_STUB_LOG}"
exit 0
""",
        )
        ctest = self._write_executable(
            bin_dir / "ctest",
            """#!/usr/bin/env bash
echo "stub ctest $*" >> "${TMP_STUB_LOG}"
exit 0
""",
        )
        ninja = self._write_executable(
            bin_dir / "ninja",
            """#!/usr/bin/env bash
echo "stub ninja $*" >> "${TMP_STUB_LOG}"
exit 0
""",
        )
        editor = self._write_executable(
            bin_dir / "lxe_editor",
            f"""#!/usr/bin/env bash
echo "editor stdout"
echo "editor stderr" >&2
sleep {editor_sleep}
exit 0
""",
        )
        os.environ["TMP_STUB_LOG"] = str(root / "stub.log")
        return {{"cmake": cmake, "ctest": ctest, "ninja": ninja, "editor": editor}}

    def _write_executable(self, path: Path, body: str) -> Path:
        path.write_text(textwrap.dedent(body), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IEXEC)
        return path


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the new test file and verify it fails because the script does not exist yet**

Run: `python3 -m unittest scripts.tests.test_lxe_editor_leak_check -v`

Expected: FAIL with an error showing `scripts/diagnostics/lxe_editor_leak_check.sh` is missing or not executable.

- [ ] **Step 3: Commit the failing-test checkpoint**

```bash
git add scripts/tests/test_lxe_editor_leak_check.py
git commit -m "test: lock lxe_editor leak check script contract"
```

## Task 2: Add The Unified Script Skeleton, Mode Parsing, And Common Artifacts

**Files:**
- Create: `scripts/diagnostics/lxe_editor_leak_check.sh`
- Test: `scripts/tests/test_lxe_editor_leak_check.py`

- [ ] **Step 1: Write the minimal script skeleton with usage, mode parsing, shared options, and artifact helpers**

```bash
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
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --scene) SCENE_PATH="$2"; shift 2 ;;
    --duration) DURATION_SECONDS="$2"; shift 2 ;;
    --sample-interval) SAMPLE_INTERVAL_SECONDS="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    *) usage; exit 2 ;;
  esac
done

case "${MODE}" in
  sanitizer|soak|all) ;;
  *) usage; exit 2 ;;
esac

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ -z "${OUTPUT_DIR}" ]]; then
  OUTPUT_DIR="${REPO_ROOT}/artifacts/diagnostics/lxe_editor/${TIMESTAMP}"
fi
if [[ -z "${BUILD_DIR}" ]]; then
  BUILD_DIR="${REPO_ROOT}/build-asan"
fi

CMAKE_BIN="${LX_LEAK_CHECK_CMAKE:-cmake}"
CTEST_BIN="${LX_LEAK_CHECK_CTEST:-ctest}"
NINJA_BIN="${LX_LEAK_CHECK_NINJA:-ninja}"
EDITOR_BIN="${LX_LEAK_CHECK_LXE_EDITOR_BIN:-${REPO_ROOT}/build/src/demos/lxe_editor/lxe_editor}"

mkdir -p "${OUTPUT_DIR}"

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
    git -C "${REPO_ROOT}" rev-parse HEAD | sed 's/^/git_commit=/'
  } > "${OUTPUT_DIR}/env.txt"
}

write_summary_stub() {
  {
    echo "mode=${MODE}"
    echo "sanitizer_status=not_run"
    echo "soak_status=not_run"
  } > "${OUTPUT_DIR}/summary.txt"
}

write_env
write_summary_stub
touch "${OUTPUT_DIR}/sanitizer.log"
```

- [ ] **Step 2: Keep the script green only for argument parsing and common artifact creation**

```bash
if [[ "${MODE}" == "sanitizer" ]]; then
  exit 0
fi

if [[ "${MODE}" == "soak" ]]; then
  : > "${OUTPUT_DIR}/soak.stdout.log"
  : > "${OUTPUT_DIR}/soak.stderr.log"
  printf "timestamp,rss_kb,vsz_kb,cpu_percent\n" > "${OUTPUT_DIR}/rss.csv"
  exit 0
fi

if [[ "${MODE}" == "all" ]]; then
  : > "${OUTPUT_DIR}/soak.stdout.log"
  : > "${OUTPUT_DIR}/soak.stderr.log"
  printf "timestamp,rss_kb,vsz_kb,cpu_percent\n" > "${OUTPUT_DIR}/rss.csv"
  {
    echo "mode=all"
    echo "sanitizer_status=passed"
    echo "soak_status=passed"
  } > "${OUTPUT_DIR}/summary.txt"
  exit 0
fi
```

- [ ] **Step 3: Run the script tests and confirm the contract passes for parsing and base artifacts**

Run: `python3 -m unittest scripts.tests.test_lxe_editor_leak_check -v`

Expected: PASS for unknown-mode usage and base artifact expectations, while sanitizer/soak/all are still only stub-success paths.

- [ ] **Step 4: Commit the script-skeleton checkpoint**

```bash
git add scripts/diagnostics/lxe_editor_leak_check.sh scripts/tests/test_lxe_editor_leak_check.py
git commit -m "feat: add lxe_editor diagnostics script skeleton"
```

## Task 3: Implement Sanitizer Mode With Separate Build Directory And Short Smoke

**Files:**
- Modify: `scripts/diagnostics/lxe_editor_leak_check.sh`
- Test: `scripts/tests/test_lxe_editor_leak_check.py`

- [ ] **Step 1: Extend the test file so sanitizer mode asserts the stub tools were actually invoked**

```python
    def test_sanitizer_mode_invokes_configure_build_tests_and_editor_smoke(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="1")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
                    "bash",
                    str(SCRIPT_PATH),
                    "sanitizer",
                    "--output-dir",
                    str(output_dir),
                    "--build-dir",
                    str(root / "build-asan"),
                ],
                text=True,
                capture_output=True,
                env=env,
                check=False,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            stub_log = (root / "stub.log").read_text()
            self.assertIn("stub cmake", stub_log)
            self.assertIn("stub ninja", stub_log)
            self.assertIn("stub ctest", stub_log)
            sanitizer_log = (output_dir / "sanitizer.log").read_text()
            self.assertIn("editor stdout", sanitizer_log)
```

- [ ] **Step 2: Run the tests and verify the new sanitizer-invocation assertion fails**

Run: `python3 -m unittest scripts.tests.test_lxe_editor_leak_check -v`

Expected: FAIL because `sanitizer` mode currently only creates files and does not run configure/build/test/editor steps.

- [ ] **Step 3: Implement the real sanitizer pipeline in the script**

```bash
run_sanitizer_mode() {
  local status="passed"
  export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}"

  {
    echo "[sanitizer] configure"
    "${CMAKE_BIN}" -S "${REPO_ROOT}" -B "${BUILD_DIR}" -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
      -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer"

    echo "[sanitizer] build"
    "${NINJA_BIN}" -C "${BUILD_DIR}" \
      test_lxe_editor_session test_lxe_editor_interaction test_command_bus lxe_editor

    echo "[sanitizer] tests"
    "${CTEST_BIN}" --test-dir "${BUILD_DIR}" --output-on-failure \
      -R 'test_(lxe_editor_session|lxe_editor_interaction|command_bus)$'

    echo "[sanitizer] editor smoke"
    if [[ -n "${SCENE_PATH}" ]]; then
      "${EDITOR_BIN}" "${SCENE_PATH}"
    else
      "${EDITOR_BIN}"
    fi
  } > "${OUTPUT_DIR}/sanitizer.log" 2>&1 || status="failed"

  SANITIZER_STATUS="${status}"
}
```

- [ ] **Step 4: Update summary generation so sanitizer mode records the final sanitizer status**

```bash
SANITIZER_STATUS="not_run"
SOAK_STATUS="not_run"

write_summary() {
  {
    echo "mode=${MODE}"
    echo "sanitizer_status=${SANITIZER_STATUS}"
    echo "soak_status=${SOAK_STATUS}"
  } > "${OUTPUT_DIR}/summary.txt"
}
```

- [ ] **Step 5: Run the script tests and confirm sanitizer mode now exercises the stub commands**

Run: `python3 -m unittest scripts.tests.test_lxe_editor_leak_check -v`

Expected: PASS for sanitizer assertions, with `stub.log` showing `cmake`, `ninja`, and `ctest` calls, and `sanitizer.log` containing the editor smoke output.

- [ ] **Step 6: Commit the sanitizer-mode checkpoint**

```bash
git add scripts/diagnostics/lxe_editor_leak_check.sh scripts/tests/test_lxe_editor_leak_check.py
git commit -m "feat: add lxe_editor sanitizer diagnostics mode"
```

## Task 4: Implement Soak Sampling, `all` Mode, And Final Summary Semantics

**Files:**
- Modify: `scripts/diagnostics/lxe_editor_leak_check.sh`
- Modify: `scripts/tests/test_lxe_editor_leak_check.py`
- Test: `scripts/tests/test_lxe_editor_leak_check.py`

- [ ] **Step 1: Add a failing soak-summary assertion that checks RSS rows and final summary fields**

```python
    def test_soak_summary_reports_rss_start_end_and_peak(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="2")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
                    "bash",
                    str(SCRIPT_PATH),
                    "soak",
                    "--output-dir",
                    str(output_dir),
                    "--duration",
                    "2",
                    "--sample-interval",
                    "1",
                ],
                text=True,
                capture_output=True,
                env=env,
                check=False,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            summary = (output_dir / "summary.txt").read_text()
            self.assertIn("soak_status=passed", summary)
            self.assertIn("rss_start_kb=", summary)
            self.assertIn("rss_end_kb=", summary)
            self.assertIn("rss_peak_kb=", summary)
```

- [ ] **Step 2: Run the tests and verify the new soak-summary assertion fails**

Run: `python3 -m unittest scripts.tests.test_lxe_editor_leak_check -v`

Expected: FAIL because the script currently creates an empty/stub RSS file and does not compute summary metrics.

- [ ] **Step 3: Implement real soak process management and RSS sampling in the script**

```bash
run_soak_mode() {
  local status="passed"
  local end_time="$(( $(date +%s) + DURATION_SECONDS ))"

  : > "${OUTPUT_DIR}/soak.stdout.log"
  : > "${OUTPUT_DIR}/soak.stderr.log"
  printf "timestamp,rss_kb,vsz_kb,cpu_percent\n" > "${OUTPUT_DIR}/rss.csv"

  if [[ -n "${SCENE_PATH}" ]]; then
    "${EDITOR_BIN}" "${SCENE_PATH}" > "${OUTPUT_DIR}/soak.stdout.log" 2> "${OUTPUT_DIR}/soak.stderr.log" &
  else
    "${EDITOR_BIN}" > "${OUTPUT_DIR}/soak.stdout.log" 2> "${OUTPUT_DIR}/soak.stderr.log" &
  fi
  local editor_pid=$!

  while kill -0 "${editor_pid}" 2>/dev/null && [[ "$(date +%s)" -lt "${end_time}" ]]; do
    local sample
    sample="$(ps -o rss=,vsz=,%cpu= -p "${editor_pid}" | awk '{print $1","$2","$3}')"
    printf "%s,%s\n" "$(date +%s)" "${sample}" >> "${OUTPUT_DIR}/rss.csv"
    sleep "${SAMPLE_INTERVAL_SECONDS}"
  done

  if kill -0 "${editor_pid}" 2>/dev/null; then
    kill "${editor_pid}" || true
    wait "${editor_pid}" || true
  else
    wait "${editor_pid}" || status="failed"
  fi

  SOAK_STATUS="${status}"
}

append_soak_summary_fields() {
  local stats
  stats="$(awk -F, 'NR==2{start=$2} NR>=2{end=$2; if($2>peak) peak=$2} END{printf "rss_start_kb=%s\nrss_end_kb=%s\nrss_peak_kb=%s\n", start, end, peak}' "${OUTPUT_DIR}/rss.csv")"
  printf "%s" "${stats}" >> "${OUTPUT_DIR}/summary.txt"
}
```

- [ ] **Step 4: Wire `all` mode to run sanitizer first, soak second, then emit a full summary**

```bash
SANITIZER_STATUS="not_run"
SOAK_STATUS="not_run"

case "${MODE}" in
  sanitizer)
    run_sanitizer_mode
    ;;
  soak)
    run_soak_mode
    ;;
  all)
    run_sanitizer_mode
    run_soak_mode
    ;;
esac

write_summary
if [[ -f "${OUTPUT_DIR}/rss.csv" ]] && [[ "$(wc -l < "${OUTPUT_DIR}/rss.csv")" -ge 2 ]]; then
  append_soak_summary_fields
fi

if [[ "${SANITIZER_STATUS}" == "failed" || "${SOAK_STATUS}" == "failed" ]]; then
  exit 1
fi
exit 0
```

- [ ] **Step 5: Run the script test suite and confirm soak/all modes now produce real summaries**

Run: `python3 -m unittest scripts.tests.test_lxe_editor_leak_check -v`

Expected: PASS, with `summary.txt` containing `mode=...`, `sanitizer_status=...`, `soak_status=...`, and RSS start/end/peak fields for soak-capable runs.

- [ ] **Step 6: Run one real developer smoke pass against the repo tools**

Run: `bash scripts/diagnostics/lxe_editor_leak_check.sh sanitizer --output-dir artifacts/diagnostics/lxe_editor/manual-sanitizer`

Expected: The script creates `env.txt`, `summary.txt`, and `sanitizer.log` under the chosen output directory. If the local machine lacks the graphics prerequisites for some tests or editor smoke, `summary.txt` and `sanitizer.log` should still distinguish environment/setup failure from sanitizer findings.

- [ ] **Step 7: Commit the soak/all-mode checkpoint**

```bash
git add scripts/diagnostics/lxe_editor_leak_check.sh scripts/tests/test_lxe_editor_leak_check.py
git commit -m "feat: add lxe_editor soak diagnostics mode"
```
