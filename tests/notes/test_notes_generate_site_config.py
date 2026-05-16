from __future__ import annotations

import json
import pathlib
import tempfile
import unittest
from unittest import mock

import yaml

from scripts.notes import generate_site_config


class NotesGenerateSiteConfigTest(unittest.TestCase):
    def test_chat_client_config_keeps_host_omitted_for_browser_inference(self) -> None:
        cfg = {"extra": {"notes_chat": {"enabled": True}}}

        with mock.patch.dict("os.environ", {}, clear=True):
            config = generate_site_config.chat_client_config(cfg)

        self.assertNotIn("host", config)
        self.assertNotIn("port", config)

    def test_chat_client_config_uses_configured_host_and_runtime_port(self) -> None:
        cfg = {"extra": {"notes_chat": {"enabled": True, "host": "192.168.66.12"}}}

        with mock.patch.dict("os.environ", {"NOTES_CHAT_CLIENT_PORT": "8112"}, clear=True):
            config = generate_site_config.chat_client_config(cfg)

        self.assertEqual(config, {"host": "192.168.66.12", "port": 8112})

    def test_inject_writes_chat_config_before_chat_script(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            notes_dir = root / "notes"
            notes_dir.mkdir()
            mkdocs_src = root / "mkdocs.yml"
            mkdocs_gen = root / "mkdocs.gen.yml"
            mkdocs_src.write_text(
                "\n".join(
                    [
                        "site_name: Test",
                        "docs_dir: notes",
                        "extra:",
                        "  notes_chat:",
                        "    enabled: true",
                        "    host: 192.168.66.12",
                        "extra_javascript:",
                        "  - assets/javascripts/mathjax.js",
                        "",
                    ]
                ),
                encoding="utf-8",
            )

            with (
                mock.patch.object(generate_site_config, "REPO_ROOT", root),
                mock.patch.object(generate_site_config, "NOTES_DIR", notes_dir),
                mock.patch.object(generate_site_config, "REQ_DIR", notes_dir / "requirements"),
                mock.patch.object(generate_site_config, "NAV_CONFIG", notes_dir / "nav.yml"),
                mock.patch.object(generate_site_config, "MKDOCS_SRC", mkdocs_src),
                mock.patch.object(generate_site_config, "MKDOCS_GEN", mkdocs_gen),
                mock.patch.dict("os.environ", {"NOTES_CHAT_CLIENT_PORT": "8112"}, clear=True),
            ):
                generate_site_config.inject_into_mkdocs([], [], ["README.md"])

            generated = yaml.safe_load(mkdocs_gen.read_text(encoding="utf-8"))
            extra_javascript = generated["extra_javascript"]
            self.assertLess(
                extra_javascript.index(generate_site_config.CHAT_CONFIG_JS),
                extra_javascript.index(generate_site_config.CHAT_JS),
            )
            config_js = notes_dir / generate_site_config.CHAT_CONFIG_JS
            self.assertTrue(config_js.is_file())
            payload = config_js.read_text(encoding="utf-8").split(" = ", 1)[1].rstrip(";\n")
            self.assertEqual(json.loads(payload), {"host": "192.168.66.12", "port": 8112})


if __name__ == "__main__":
    unittest.main()
