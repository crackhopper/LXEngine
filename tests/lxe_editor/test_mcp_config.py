from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import tempfile
import textwrap
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from tests.lxe_editor.api_client import LxeEditorClient


class DirectMcpConfigTest(unittest.TestCase):
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

    def test_client_reads_direct_mcp_url_from_runtime_state(self) -> None:
        self.write_runtime_state(
            f"""
            version: 1
            apiHost: 127.0.0.1
            apiPort: 3768
            mcpUrl: http://127.0.0.1:3768/mcp
            tokenFile: {self.token_path}
            startedAt: 2026-05-11-210000
            """
        )

        client = LxeEditorClient(self.runtime_root)

        self.assertEqual(client.read_mcp_url(), "http://127.0.0.1:3768/mcp")

    def test_use_local_mcp_script_writes_direct_url_config(self) -> None:
        self.write_runtime_state(
            f"""
            version: 1
            apiHost: 127.0.0.1
            apiPort: 3768
            mcpUrl: http://127.0.0.1:3768/mcp
            tokenFile: {self.token_path}
            startedAt: 2026-05-11-210000
            """
        )

        script = self.repo_root / "scripts" / "lxe_editor" / "use_local_mcp.sh"
        command = (
            f"source '{script}' >/dev/null && "
            "printf '%s\n' \"$LXE_EDITOR_MCP_BEARER_TOKEN\" && "
            f"cat '{self.config_path}'"
        )
        result = subprocess.run(
            ["bash", "-lc", command],
            check=True,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "LXE_EDITOR_RUNTIME_ROOT": str(self.runtime_root),
                "LXE_EDITOR_CODEX_CONFIG_PATH": str(self.config_path),
            },
        )

        output = result.stdout
        self.assertIn("secret-token", output)
        self.assertIn('url = "http://127.0.0.1:3768/mcp"', output)
        self.assertIn('bearer_token_env_var = "LXE_EDITOR_MCP_BEARER_TOKEN"', output)

    def test_use_remote_mcp_script_writes_direct_url_config(self) -> None:
        script = self.repo_root / "scripts" / "lxe_editor" / "use_remote_mcp.sh"
        command = (
            f"source '{script}' 'https://editor.example.com/mcp' 'remote-token' >/dev/null && "
            "printf '%s\n' \"$LXE_EDITOR_MCP_BEARER_TOKEN\" && "
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
        self.assertIn('url = "https://editor.example.com/mcp"', output)
        self.assertIn('bearer_token_env_var = "LXE_EDITOR_MCP_BEARER_TOKEN"', output)

    def test_client_posts_jsonrpc_to_direct_mcp_url_with_bearer_token(self) -> None:
        captured: dict[str, str] = {}

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self) -> None:  # noqa: N802
                length = int(self.headers.get("Content-Length", "0"))
                captured["path"] = self.path
                captured["authorization"] = self.headers.get("Authorization", "")
                captured["body"] = self.rfile.read(length).decode("utf-8")
                payload = (
                    '{"jsonrpc":"2.0","id":1,"result":{"tools":[{"name":"ok"}]}}'
                ).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)

            def log_message(self, format: str, *args) -> None:  # noqa: A003
                return

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            self.write_runtime_state(
                f"""
                version: 1
                mcpUrl: http://127.0.0.1:{server.server_port}/mcp
                tokenFile: {self.token_path}
                startedAt: 2026-05-11-210000
                """
            )
            client = LxeEditorClient(self.runtime_root)
            response = client.mcp_request("tools/list", {})
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5.0)

        self.assertEqual(captured["path"], "/mcp")
        self.assertEqual(captured["authorization"], "Bearer secret-token")
        self.assertIn('"method": "tools/list"', captured["body"])
        self.assertEqual(response["result"]["tools"][0]["name"], "ok")
