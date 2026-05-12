#!/usr/bin/env python3
from __future__ import annotations

import os
import stat
import subprocess
import tempfile
import textwrap
import unittest
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "diagnostics" / "lxe_editor_leak_check.sh"


class LxeEditorLeakCheckScriptTest(unittest.TestCase):
    def test_rejects_unknown_mode_with_usage(self) -> None:
        completed = subprocess.run(
            [str(SCRIPT_PATH), "unknown-mode"],
            text=True,
            capture_output=True,
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("usage:", completed.stderr)

    def test_rejects_missing_values_for_options(self) -> None:
        for option in (
            "--build-dir",
            "--scene",
            "--duration",
            "--sample-interval",
            "--output-dir",
        ):
            with self.subTest(option=option):
                completed = subprocess.run(
                    [str(SCRIPT_PATH), "sanitizer", option],
                    text=True,
                    capture_output=True,
                )
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("missing value for", completed.stderr)
                self.assertIn("usage:", completed.stderr)

    def test_sanitizer_mode_writes_env_summary_and_log(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="1")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
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

    def test_sanitizer_mode_forces_detect_leaks_even_when_inherited_off(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="1")
            output_dir = root / "artifacts"
            env = self._script_env(tools)
            env["ASAN_OPTIONS"] = "detect_leaks=0:foo=bar"

            completed = subprocess.run(
                [
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
            env_text = (output_dir / "env.txt").read_text()
            self.assertIn("asan_options=foo=bar:detect_leaks=1", env_text)
            self.assertNotIn("detect_leaks=0", env_text)

    def test_sanitizer_mode_invokes_configure_build_tests_and_editor_smoke(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="1")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
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
            self.assertIn("stub lxe_editor", stub_log)
            summary = (output_dir / "summary.txt").read_text()
            self.assertIn("sanitizer_status=passed", summary)

    def test_sanitizer_smoke_uses_api_flags_without_positional_scene_arg(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="0")
            output_dir = root / "artifacts"
            env = self._script_env(tools)
            scene_path = root / "sample.scene"
            scene_path.write_text("stub", encoding="utf-8")

            completed = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "sanitizer",
                    "--output-dir",
                    str(output_dir),
                    "--build-dir",
                    str(root / "build-asan"),
                    "--scene",
                    str(scene_path),
                ],
                text=True,
                capture_output=True,
                env=env,
                check=False,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            editor_line = next(
                line
                for line in (root / "stub.log").read_text().splitlines()
                if line.startswith("stub lxe_editor ")
            )
            self.assertIn("--api-host 127.0.0.1 --api-port 37681", editor_line)
            self.assertNotIn(str(scene_path), editor_line)
            summary = self._read_key_values(output_dir / "summary.txt")
            self.assertEqual(summary["sanitizer_status"], "passed")
            self.assertEqual(summary["failure_kind"], "none")

    def test_sanitizer_mode_surfaces_nonzero_editor_exit_code(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="0", editor_exit_code="7")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
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

            self.assertNotEqual(completed.returncode, 0)
            summary = self._read_key_values(output_dir / "summary.txt")
            self.assertEqual(summary["sanitizer_status"], "failed")
            self.assertEqual(summary["failure_kind"], "smoke_control_failure")
            self.assertEqual(summary["failed_step"], "editor_smoke_exit")
            self.assertIn("editor exited with status 7", summary["failure_reason"])
            self.assertNotIn("editor smoke timeout", (output_dir / "sanitizer.log").read_text())

    def test_sanitizer_smoke_times_out_and_terminates_uncooperative_editor(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, ignore_term=True)
            output_dir = root / "artifacts"
            env = self._script_env(tools)
            env["LX_LEAK_CHECK_EDITOR_SMOKE_TIMEOUT_SECONDS"] = "1"
            env["LX_LEAK_CHECK_SOAK_TERMINATION_GRACE_SECONDS"] = "1"

            started = time.monotonic()
            completed = subprocess.run(
                [
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
            elapsed = time.monotonic() - started

            self.assertNotEqual(completed.returncode, 0)
            self.assertLess(elapsed, 10)
            summary = self._read_key_values(output_dir / "summary.txt")
            self.assertEqual(summary["sanitizer_status"], "failed")
            self.assertEqual(summary["failure_kind"], "smoke_control_failure")
            self.assertEqual(summary["failed_step"], "editor_smoke_timeout")
            self.assertIn("editor smoke timeout after 1s", summary["failure_reason"])
            self.assertIn("editor_smoke_timeout_seconds=1", (output_dir / "env.txt").read_text())
            self.assertIn("editor smoke timeout after 1s", (output_dir / "sanitizer.log").read_text())

    def test_sanitizer_summary_classifies_configure_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, cmake_exit_code="3")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
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

            self.assertNotEqual(completed.returncode, 0)
            summary = self._read_key_values(output_dir / "summary.txt")
            self.assertEqual(summary["failure_kind"], "setup_failure")
            self.assertEqual(summary["failed_step"], "configure")
            self.assertIn("cmake configure failed", summary["failure_reason"])

    def test_sanitizer_summary_classifies_build_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, ninja_exit_code="4")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
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

            self.assertNotEqual(completed.returncode, 0)
            summary = self._read_key_values(output_dir / "summary.txt")
            self.assertEqual(summary["failure_kind"], "build_failure")
            self.assertEqual(summary["failed_step"], "build")
            self.assertIn("ninja build failed", summary["failure_reason"])

    def test_sanitizer_summary_classifies_ctest_sanitizer_finding(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(
                root,
                ctest_exit_code="5",
                ctest_stderr="AddressSanitizer: heap-use-after-free",
            )
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
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

            self.assertNotEqual(completed.returncode, 0)
            summary = self._read_key_values(output_dir / "summary.txt")
            self.assertEqual(summary["failure_kind"], "sanitizer_finding")
            self.assertEqual(summary["failed_step"], "ctest")
            self.assertIn("sanitizer signature", summary["failure_reason"])

    def test_sanitizer_summary_classifies_ctest_non_sanitizer_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(
                root,
                ctest_exit_code="6",
                ctest_stderr="one test failed",
            )
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
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

            self.assertNotEqual(completed.returncode, 0)
            summary = self._read_key_values(output_dir / "summary.txt")
            self.assertEqual(summary["failure_kind"], "test_failure")
            self.assertEqual(summary["failed_step"], "ctest")
            self.assertIn("ctest failed without sanitizer signature", summary["failure_reason"])

    def test_soak_mode_writes_rss_csv_and_process_logs(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="2")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
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
            summary = (output_dir / "summary.txt").read_text()
            self.assertIn("soak_status=passed", summary)
            self.assertIn("rss_start_kb=", summary)
            self.assertIn("rss_end_kb=", summary)
            self.assertIn("rss_peak_kb=", summary)

    def test_soak_mode_forcibly_terminates_uncooperative_editor(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, ignore_term=True)
            output_dir = root / "artifacts"
            env = self._script_env(tools)
            env["LX_LEAK_CHECK_SOAK_TERMINATION_GRACE_SECONDS"] = "1"

            started = time.monotonic()
            completed = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "soak",
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
            elapsed = time.monotonic() - started

            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertLess(elapsed, 10)
            summary = (output_dir / "summary.txt").read_text()
            self.assertIn("soak_status=passed", summary)
            self.assertIn("soak_exit_status=timeout", summary)
            self.assertIn("rss_start_kb=", summary)
            self.assertIn("rss_end_kb=", summary)
            self.assertIn("rss_peak_kb=", summary)

    def test_all_mode_runs_both_paths_and_reports_them(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            tools = self._write_stub_tools(root, editor_sleep="1")
            output_dir = root / "artifacts"
            env = self._script_env(tools)

            completed = subprocess.run(
                [
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
            self.assertTrue((output_dir / "env.txt").exists())
            self.assertTrue((output_dir / "summary.txt").exists())
            self.assertTrue((output_dir / "sanitizer.log").exists())
            self.assertTrue((output_dir / "soak.stdout.log").exists())
            self.assertTrue((output_dir / "soak.stderr.log").exists())
            self.assertTrue((output_dir / "rss.csv").exists())
            summary = (output_dir / "summary.txt").read_text()
            self.assertIn("mode=all", summary)
            self.assertIn("sanitizer_status=passed", summary)
            self.assertIn("soak_status=passed", summary)
            self.assertIn("rss_start_kb=", summary)
            self.assertIn("rss_end_kb=", summary)
            self.assertIn("rss_peak_kb=", summary)
            stub_log = (root / "stub.log").read_text().splitlines()
            self.assertTrue(any(line.startswith("stub cmake ") for line in stub_log))
            self.assertTrue(any(line.startswith("stub ninja ") for line in stub_log))
            self.assertTrue(any(line.startswith("stub ctest ") for line in stub_log))
            self.assertTrue(
                stub_log.index(next(line for line in stub_log if line.startswith("stub cmake ")))
                < stub_log.index(next(line for line in stub_log if line.startswith("stub lxe_editor ")))
            )

    def _script_env(self, tools: dict[str, Path]) -> dict[str, str]:
        env = os.environ.copy()
        env["LX_LEAK_CHECK_CMAKE"] = str(tools["cmake"])
        env["LX_LEAK_CHECK_CTEST"] = str(tools["ctest"])
        env["LX_LEAK_CHECK_NINJA"] = str(tools["ninja"])
        env["LX_LEAK_CHECK_LXE_EDITOR_BIN"] = str(tools["editor"])
        env["TMP_STUB_LOG"] = str(tools["stub_log"])
        return env

    def _read_key_values(self, path: Path) -> dict[str, str]:
        pairs: dict[str, str] = {}
        for line in path.read_text().splitlines():
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            pairs[key] = value
        return pairs

    def _write_stub_tools(
        self,
        root: Path,
        *,
        editor_sleep: str = "1",
        editor_exit_code: str = "0",
        ignore_term: bool = False,
        cmake_exit_code: str = "0",
        ninja_exit_code: str = "0",
        ctest_exit_code: str = "0",
        ctest_stderr: str = "",
    ) -> dict[str, Path]:
        bin_dir = root / "bin"
        bin_dir.mkdir(parents=True, exist_ok=True)
        cmake = self._write_executable(
            bin_dir / "cmake",
            """#!/usr/bin/env bash
echo "stub cmake $*" >> "${TMP_STUB_LOG}"
exit """
            + cmake_exit_code
            + """
""",
        )
        ctest = self._write_executable(
            bin_dir / "ctest",
            """#!/usr/bin/env bash
echo "stub ctest $*" >> "${TMP_STUB_LOG}"
"""
            + (f"echo {ctest_stderr!r} >&2\n" if ctest_stderr else "")
            + "exit "
            + ctest_exit_code
            + """
""",
        )
        ninja = self._write_executable(
            bin_dir / "ninja",
            """#!/usr/bin/env bash
echo "stub ninja $*" >> "${TMP_STUB_LOG}"
exit """
            + ninja_exit_code
            + """
""",
        )
        editor = self._write_executable(
            bin_dir / "lxe_editor",
            f"""#!/usr/bin/env bash
trap '' TERM
echo "stub lxe_editor $*" >> "${{TMP_STUB_LOG}}"
echo "editor stdout"
echo "editor stderr" >&2
if [[ "{str(ignore_term).lower()}" == "true" ]]; then
  while true; do
    sleep 1
  done
else
  sleep {editor_sleep}
fi
exit {editor_exit_code}
""",
        )
        stub_log = root / "stub.log"
        return {
            "cmake": cmake,
            "ctest": ctest,
            "ninja": ninja,
            "editor": editor,
            "stub_log": stub_log,
        }

    def _write_executable(self, path: Path, body: str) -> Path:
        path.write_text(textwrap.dedent(body), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IEXEC)
        return path


if __name__ == "__main__":
    unittest.main()
