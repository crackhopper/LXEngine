#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "count_cpp_loc.py"


class CountCppLocScriptTest(unittest.TestCase):
    def test_default_output_excludes_third_party_and_reports_cpp_and_headers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self._write(root / "src" / "main.cpp", "int main() {\n  return 0;\n}\n")
            self._write(root / "src" / "main.hpp", "#pragma once\nvoid run();\n")
            self._write(root / "third_party" / "vendor.cpp", "ignored\nignored\n")
            self._write(
                root / "src" / "infra" / "external" / "imgui.cpp",
                "ignored\nignored\nignored\n",
            )
            self._write(root / "build" / "generated.cpp", "ignored\n")

            result = self._run_script(root)

            self.assertEqual(result["cpp"]["files"], 1)
            self.assertEqual(result["cpp"]["lines"], 3)
            self.assertEqual(result["cpp_and_headers"]["files"], 2)
            self.assertEqual(result["cpp_and_headers"]["lines"], 5)
            self.assertEqual(result["excluded_dirs"], [
                "third_party",
                "src/infra/external",
                "build",
                ".git",
                ".site",
                ".tmp",
                "tmp",
            ])

    def test_code_only_excludes_blank_and_pure_comment_lines(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self._write(
                root / "src" / "sample.cpp",
                """
                // leading comment

                int main() {
                  /* inline block comment kept because line has code after trim? no */
                  return 0; // trailing comment still counts as code
                }

                /*
                  block comment
                */
                """,
            )
            self._write(
                root / "src" / "sample.hpp",
                """
                #pragma once

                /* header comment */
                void run();
                """,
            )

            result = self._run_script(root, "--code-only")

            self.assertEqual(result["cpp"]["lines"], 3)
            self.assertEqual(result["cpp_and_headers"]["lines"], 5)

    def test_by_dir_groups_remaining_files_under_requested_roots(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self._write(root / "src" / "core" / "a.cpp", "1\n2\n")
            self._write(root / "src" / "core" / "a.hpp", "3\n")
            self._write(root / "src" / "test" / "b.cpp", "4\n5\n6\n")

            result = self._run_script(root, "--by-dir", "src/core", "src/test")

            self.assertEqual(result["by_dir"], {
                "src/core": {"files": 2, "lines": 3},
                "src/test": {"files": 1, "lines": 3},
            })

    def _run_script(self, root: Path, *args: str) -> dict:
        completed = subprocess.run(
            ["python3", str(SCRIPT_PATH), "--root", str(root), "--json", *args],
            check=True,
            capture_output=True,
            text=True,
        )
        return json.loads(completed.stdout)

    def _write(self, path: Path, content: str) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(content), encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
