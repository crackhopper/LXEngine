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
        self.assertIn('url = "http://127.0.0.1:3880/mcp"', output)
        self.assertIn(
            'bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"',
            output,
        )

    def test_use_local_mcp_script_honors_manager_url_override(self) -> None:
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
                "LXE_MANAGER_URL": "http://127.0.0.1:4999/mcp",
            },
        )

        output = result.stdout
        self.assertIn('url = "http://127.0.0.1:4999/mcp"', output)
        self.assertIn(
            'bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"',
            output,
        )

    def test_use_remote_mcp_script_writes_manager_url_config(self) -> None:
        script = self.repo_root / "scripts" / "lxe_manager" / "use_remote_mcp.sh"
        command = (
            f"source '{script}' 'https://manager.example.com/mcp' 'remote-token' >/dev/null && "
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
            },
        )

        output = result.stdout
        self.assertIn("remote-token", output)
        self.assertIn('url = "https://manager.example.com/mcp"', output)
        self.assertIn(
            'bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"',
            output,
        )
