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

    def test_project_init_scene_list_open_and_save(self) -> None:
        templates_response = self.harness.client.command("project templates")
        self.assertTrue(templates_response["ok"])
        templates = self.harness.client.decode_structured_json(templates_response)
        self.assertIn("empty", {entry["id"] for entry in templates["templates"]})

        init_response = self.harness.client.command(
            "project init empty blackbox_scene_io"
        )
        self.assertTrue(init_response["ok"])
        scene_state = self.harness.client.wait_for(
            lambda: (
                result
                if (
                    result := self.harness.client.get_scene()
                )["currentDocumentPath"].endswith(
                    "data/projects/blackbox_scene_io/scenes/main.scene.yaml"
                )
                else None
            )
        )
        self.assertFalse(scene_state["dirty"])

        list_response = self.harness.client.command("scene list")
        self.assertTrue(list_response["ok"])
        scenes = self.harness.client.decode_structured_json(list_response)
        self.assertIn("main", {entry["id"] for entry in scenes["scenes"]})

        save_response = self.harness.client.command("scene save")
        self.assertTrue(save_response["ok"])
        saved = self.harness.client.decode_structured_json(save_response)
        saved_path = pathlib.Path(saved["path"])
        self.assertTrue(saved_path.is_file())
        self.assertEqual(saved["project"]["id"], "blackbox_scene_io")
