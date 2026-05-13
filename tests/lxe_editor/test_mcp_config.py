from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import tempfile
import textwrap
import unittest

from tests.lxe_editor.api_client import LxeEditorClient


class ManagerMcpConfigTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_root = pathlib.Path(tempfile.mkdtemp(prefix="lxengine-mcp-config-"))
        self.runtime_root = self.temp_root / "runtime"
        self.runtime_data_dir = self.runtime_root / "data" / "lxe_editor"
        self.runtime_data_dir.mkdir(parents=True, exist_ok=True)
        self.config_path = self.temp_root / "config.toml"
        self.token_path = self.runtime_data_dir / "api_token.txt"
        self.token_path.write_text("secret-token\n", encoding="utf-8")
        self.repo_root = pathlib.Path(__file__).resolve().parents[2]

    def tearDown(self) -> None:
        shutil.rmtree(self.temp_root, ignore_errors=True)

    def write_runtime_state(self, text: str) -> None:
        (self.runtime_data_dir / "runtime_state.yaml").write_text(
            textwrap.dedent(text).strip() + "\n",
            encoding="utf-8",
        )

    def write_existing_config(self) -> None:
        self.config_path.write_text(
            textwrap.dedent(
                """
                model = "gpt-test"

                [mcp_servers.other]
                url = "http://127.0.0.1:3999/mcp"
                bearer_token_env_var = "OTHER_TOKEN"
                """
            ).strip()
            + "\n",
            encoding="utf-8",
        )

    def write_existing_crlf_manager_config(self) -> None:
        self.config_path.write_text(
            'model = "gpt-test"\r\n\r\n'
            "[mcp_servers.lxe_manager]\r\n"
            'url = "http://old.example.com/mcp"\r\n'
            'bearer_token_env_var = "OLD_TOKEN"\r\n\r\n'
            "[mcp_servers.other]\r\n"
            'url = "http://127.0.0.1:3999/mcp"\r\n',
            encoding="utf-8",
        )

    def test_client_no_longer_reads_mcp_url_from_runtime_state(self) -> None:
        self.write_runtime_state(
            f"""
            version: 1
            apiHost: 127.0.0.1
            apiPort: 3768
            tokenFile: {self.token_path}
            startedAt: 2026-05-11-210000
            """
        )

        client = LxeEditorClient(self.runtime_root)

        with self.assertRaises(FileNotFoundError) as raised:
            client.read_mcp_url()
        self.assertIn(
            "manager MCP URL is no longer published",
            str(raised.exception),
        )

    def test_use_local_mcp_script_writes_manager_url_config(self) -> None:
        self.write_existing_config()
        script = self.repo_root / "scripts" / "lxe_manager" / "use_local_mcp.sh"
        command = (
            f"source '{script}' >/dev/null && "
            f"cat '{self.config_path}'"
        )
        result = subprocess.run(
            ["bash", "-lc", command],
            check=True,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "LXE_EDITOR_CODEX_CONFIG_PATH": str(self.config_path),
            },
        )

        output = result.stdout
        self.assertIn('model = "gpt-test"', output)
        self.assertIn("[mcp_servers.other]", output)
        self.assertIn('url = "http://127.0.0.1:3880/mcp"', output)
        self.assertIn(
            'bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"',
            output,
        )

    def test_use_local_mcp_script_honors_manager_url_override_and_escapes_toml(self) -> None:
        script = self.repo_root / "scripts" / "lxe_manager" / "use_local_mcp.sh"
        command = (
            f"source '{script}' >/dev/null && "
            f"cat '{self.config_path}'"
        )
        result = subprocess.run(
            ["bash", "-lc", command],
            check=True,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "LXE_EDITOR_CODEX_CONFIG_PATH": str(self.config_path),
                "LXE_MANAGER_URL": 'http://127.0.0.1:4999/mcp?name="quoted"',
            },
        )

        output = result.stdout
        self.assertIn('url = "http://127.0.0.1:4999/mcp?name=\\"quoted\\""', output)
        self.assertIn(
            'bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"',
            output,
        )

    def test_use_remote_mcp_script_writes_manager_url_config(self) -> None:
        self.write_existing_config()
        script = self.repo_root / "scripts" / "lxe_manager" / "use_remote_mcp.sh"
        command = (
            f"source '{script}' 'https://manager.example.com/mcp' >/dev/null && "
            "printf '%s\n' \"$LXE_MANAGER_MCP_BEARER_TOKEN\" && "
            f"cat '{self.config_path}'"
        )
        result = subprocess.run(
            ["bash", "-lc", command],
            check=True,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "LXE_EDITOR_CODEX_CONFIG_PATH": str(self.config_path),
                "LXE_MANAGER_MCP_BEARER_TOKEN": "remote-token",
            },
        )

        output = result.stdout
        self.assertIn("remote-token", output)
        self.assertIn('model = "gpt-test"', output)
        self.assertIn("[mcp_servers.other]", output)
        self.assertIn('url = "https://manager.example.com/mcp"', output)
        self.assertIn(
            'bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"',
            output,
        )

    def test_use_remote_mcp_script_rejects_command_line_token(self) -> None:
        script = self.repo_root / "scripts" / "lxe_manager" / "use_remote_mcp.sh"
        result = subprocess.run(
            [
                "bash",
                "-lc",
                f"source '{script}' 'https://manager.example.com/mcp' 'remote-token'",
            ],
            check=False,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "LXE_EDITOR_CODEX_CONFIG_PATH": str(self.config_path),
            },
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("LXE_MANAGER_MCP_BEARER_TOKEN", result.stderr)

    def test_use_local_mcp_script_replaces_existing_crlf_manager_table(self) -> None:
        self.write_existing_crlf_manager_config()
        script = self.repo_root / "scripts" / "lxe_manager" / "use_local_mcp.sh"
        subprocess.run(
            ["bash", "-lc", f"source '{script}' >/dev/null"],
            check=True,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "LXE_EDITOR_CODEX_CONFIG_PATH": str(self.config_path),
            },
        )

        output = self.config_path.read_text(encoding="utf-8")
        self.assertEqual(output.count("[mcp_servers.lxe_manager]"), 1)
        self.assertNotIn("OLD_TOKEN", output)
        self.assertIn('url = "http://127.0.0.1:3880/mcp"', output)
        self.assertIn("[mcp_servers.other]", output)

    def test_use_local_mcp_script_replaces_existing_table_with_literal_escaped_url(self) -> None:
        self.write_existing_crlf_manager_config()
        script = self.repo_root / "scripts" / "lxe_manager" / "use_local_mcp.sh"
        subprocess.run(
            ["bash", "-lc", f"source '{script}' >/dev/null"],
            check=True,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "LXE_EDITOR_CODEX_CONFIG_PATH": str(self.config_path),
                "LXE_MANAGER_URL": r"http://127.0.0.1:4999/mcp?path=C:\\tmp&token=$1",
            },
        )

        output = self.config_path.read_text(encoding="utf-8")
        self.assertEqual(output.count("[mcp_servers.lxe_manager]"), 1)
        self.assertIn(
            r'url = "http://127.0.0.1:4999/mcp?path=C:\\\\tmp&token=$1"',
            output,
        )

    def test_use_local_mcp_script_preserves_shell_options_when_sourced(self) -> None:
        script = self.repo_root / "scripts" / "lxe_manager" / "use_local_mcp.sh"
        command = textwrap.dedent(
            f"""
            set +e +u +o pipefail
            before="$(set -o | awk '/errexit|nounset|pipefail/ {{print $1 "=" $2}}' | sort)"
            source '{script}' >/dev/null
            after="$(set -o | awk '/errexit|nounset|pipefail/ {{print $1 "=" $2}}' | sort)"
            printf 'before:%s\\nafter:%s\\n' "$before" "$after"
            """
        )
        result = subprocess.run(
            ["bash", "-lc", command],
            check=True,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "LXE_EDITOR_CODEX_CONFIG_PATH": str(self.config_path),
            },
        )

        before_text, after_text = result.stdout.split("\nafter:", 1)
        before_text = before_text.removeprefix("before:")
        self.assertEqual(before_text.splitlines(), [
            "errexit=off",
            "nounset=off",
            "pipefail=off",
        ])
        self.assertEqual(after_text.splitlines(), [
            "errexit=off",
            "nounset=off",
            "pipefail=off",
        ])

    @unittest.skipUnless(shutil.which("pwsh"), "pwsh not available")
    def test_use_local_mcp_powershell_preserves_existing_config(self) -> None:
        self.write_existing_config()
        script = self.repo_root / "scripts" / "lxe_manager" / "use_local_mcp.ps1"
        result = subprocess.run(
            [
                "pwsh",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(script),
            ],
            check=True,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "LXE_EDITOR_CODEX_CONFIG_PATH": str(self.config_path),
                "LXE_MANAGER_URL": 'http://127.0.0.1:4999/mcp?name="quoted"',
            },
        )

        output = self.config_path.read_text(encoding="utf-8")
        self.assertIn("lxe_manager MCP target", result.stdout)
        self.assertIn('model = "gpt-test"', output)
        self.assertIn("[mcp_servers.other]", output)
        self.assertIn('url = "http://127.0.0.1:4999/mcp?name=\\"quoted\\""', output)

    @unittest.skipUnless(shutil.which("pwsh"), "pwsh not available")
    def test_use_remote_mcp_powershell_uses_env_token(self) -> None:
        script = self.repo_root / "scripts" / "lxe_manager" / "use_remote_mcp.ps1"
        subprocess.run(
            [
                "pwsh",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(script),
                "https://manager.example.com/mcp",
            ],
            check=True,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "LXE_EDITOR_CODEX_CONFIG_PATH": str(self.config_path),
                "LXE_MANAGER_MCP_BEARER_TOKEN": "remote-token",
            },
        )

        output = self.config_path.read_text(encoding="utf-8")
        self.assertIn('url = "https://manager.example.com/mcp"', output)
        self.assertIn(
            'bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"',
            output,
        )

    @unittest.skipUnless(shutil.which("pwsh"), "pwsh not available")
    def test_use_local_mcp_powershell_replaces_existing_crlf_manager_table(self) -> None:
        self.write_existing_crlf_manager_config()
        script = self.repo_root / "scripts" / "lxe_manager" / "use_local_mcp.ps1"
        subprocess.run(
            [
                "pwsh",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(script),
            ],
            check=True,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "LXE_EDITOR_CODEX_CONFIG_PATH": str(self.config_path),
            },
        )

        output = self.config_path.read_text(encoding="utf-8")
        self.assertEqual(output.count("[mcp_servers.lxe_manager]"), 1)
        self.assertNotIn("OLD_TOKEN", output)
