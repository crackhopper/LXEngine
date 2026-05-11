from __future__ import annotations

import unittest

from tests.lxe_editor.api_client import LxeEditorHarness


class EditorWorkflowBlackBoxTest(unittest.TestCase):
    harness: LxeEditorHarness

    @classmethod
    def setUpClass(cls) -> None:
        cls.harness = LxeEditorHarness()
        try:
            cls.harness.start()
        except FileNotFoundError as exc:
            raise unittest.SkipTest(str(exc)) from exc
        except Exception as exc:
            raise unittest.SkipTest(f"unable to launch API target: {exc}") from exc

    @classmethod
    def tearDownClass(cls) -> None:
        cls.harness.close()

    def test_mode_preview_and_camera_reset(self) -> None:
        summary = self.harness.client.get_summary()
        self.assertIn(summary["mode"], {"selection", "orbit", "freefly", "unknown"})

        mode_response = self.harness.client.set_mode("selection")
        self.assertTrue(mode_response["ok"])
        selection_summary = self.harness.client.wait_for(
            lambda: (
                result
                if (result := self.harness.client.get_summary())["mode"] == "selection"
                else None
            )
        )
        self.assertEqual(selection_summary["mode"], "selection")

        preview_on = self.harness.client.set_preview(True)
        self.assertTrue(preview_on["ok"])
        toolbar = self.harness.client.wait_for(
            lambda: (
                result
                if (result := self.harness.client.get_toolbar())["previewEnabled"] is True
                else None
            )
        )
        self.assertTrue(toolbar["previewEnabled"])

        preview_off = self.harness.client.set_preview(False)
        self.assertTrue(preview_off["ok"])
        toolbar = self.harness.client.wait_for(
            lambda: (
                result
                if (result := self.harness.client.get_toolbar())["previewEnabled"] is False
                else None
            )
        )
        self.assertFalse(toolbar["previewEnabled"])

        reset_response = self.harness.client.reset_editor_camera_to_game()
        self.assertTrue(reset_response["ok"])
