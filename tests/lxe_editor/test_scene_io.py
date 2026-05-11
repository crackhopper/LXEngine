from __future__ import annotations

import pathlib
import unittest

from tests.lxe_editor.api_client import LxeEditorHarness


class SceneIoBlackBoxTest(unittest.TestCase):
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

    def test_scene_list_load_and_user_save_redirect(self) -> None:
        list_response = self.harness.client.command("scene list")
        self.assertTrue(list_response["ok"])
        catalog = self.harness.client.decode_structured_json(list_response)
        entries = catalog.get("entries", [])
        self.assertTrue(entries, "scene catalog should expose at least one entry")
        self.assertIn(
            "lxe_editor.scene.yaml",
            {entry["id"] for entry in entries},
        )

        load_response = self.harness.client.command("scene load lxe_editor.scene.yaml")
        self.assertTrue(load_response["ok"])
        scene_state = self.harness.client.wait_for(
            lambda: (
                result
                if (result := self.harness.client.get_scene())["currentDocumentPath"].endswith(
                    "assets/scenes/lxe_editor.scene.yaml"
                )
                else None
            )
        )
        self.assertEqual(scene_state["sourceKind"], "asset")
        self.assertEqual(scene_state["permission"], "user")

        save_response = self.harness.client.command("scene save")
        self.assertTrue(save_response["ok"])
        saved = self.harness.client.decode_structured_json(save_response)
        self.assertEqual(saved["kind"], "local")
        self.assertTrue(saved["redirectedFromAsset"])
        saved_path = pathlib.Path(saved["path"])
        self.assertTrue(saved_path.is_file())
        self.assertEqual(
            saved_path.parent.resolve(),
            (self.harness.runtime_root / "data" / "scenes").resolve(),
        )
