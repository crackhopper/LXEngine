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
            [str(SCRIPT_PATH), "unknown-mode"],
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
        env["TMP_STUB_LOG"] = str(tools["stub_log"])
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
