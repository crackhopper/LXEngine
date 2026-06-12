from __future__ import annotations

import unittest

from tests.lxe_editor.api_client import LxeEditorHarness


class PersistenceBlackBoxTest(unittest.TestCase):
    def test_api_token_persists_across_restart(self) -> None:
        harness = LxeEditorHarness()
        try:
            harness.start()
        except FileNotFoundError as exc:
            raise unittest.SkipTest(str(exc)) from exc
        except Exception as exc:
            raise unittest.SkipTest(f"unable to launch API target: {exc}") from exc

        try:
            first_token = harness.client.read_token()
            harness.restart()
            second_token = harness.client.read_token()
        finally:
            harness.close()

        self.assertTrue(first_token)
        self.assertEqual(first_token, second_token)

    def test_console_history_persists_across_restart(self) -> None:
        harness = LxeEditorHarness()
        try:
            harness.start()
        except FileNotFoundError as exc:
            raise unittest.SkipTest(str(exc)) from exc
        except Exception as exc:
            raise unittest.SkipTest(f"unable to launch API target: {exc}") from exc

        marker = "help"
        try:
            response = harness.client.command(marker)
            self.assertTrue(response.get("ok"), response)

            before_restart = harness.client.decode_structured_json(
                harness.client.command("state history")
            )
            self.assertIn(marker, before_restart.get("lines", []))

            harness.restart()

            after_restart = harness.client.decode_structured_json(
                harness.client.command("state history")
            )
        finally:
            harness.close()

        self.assertIn(marker, after_restart.get("lines", []))
