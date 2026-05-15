from __future__ import annotations

import pathlib
import unittest
import uuid

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
        project_id = f"blackbox_scene_io_{uuid.uuid4().hex[:8]}"

        templates_response = self.harness.client.command("project templates")
        self.assertTrue(templates_response["ok"])
        templates = self.harness.client.decode_structured_json(templates_response)
        self.assertIn("empty", {entry["id"] for entry in templates["templates"]})

        init_response = self.harness.client.command(f"project init empty {project_id}")
        self.assertTrue(init_response["ok"])
        state = self.harness.client.wait_for(
            lambda: (
                result
                if (
                    result := self.harness.client.get_state()
                )["project"]
                and result["project"]["id"] == project_id
                and result["project"]["activeScene"] == "scenes/main.scene.yaml"
                and result["scene"]["sceneName"] == "Empty Project"
                else None
            )
        )
        self.assertFalse(state["scene"]["dirty"])

        new_response = self.harness.client.command("scene new blackbox_aux")
        self.assertTrue(new_response["ok"])
        state = self.harness.client.wait_for(
            lambda: (
                result
                if (
                    result := self.harness.client.get_state()
                )["project"]
                and result["project"]["activeScene"]
                == "scenes/blackbox_aux.scene.yaml"
                and result["scene"]["sceneName"] == "blackbox_aux"
                else None
            )
        )
        self.assertTrue(state["project"]["dirty"])

        open_response = self.harness.client.command("scene open main")
        self.assertTrue(open_response["ok"])
        state = self.harness.client.wait_for(
            lambda: (
                result
                if (
                    result := self.harness.client.get_state()
                )["project"]
                and result["project"]["activeScene"] == "scenes/main.scene.yaml"
                and result["scene"]["sceneName"] == "Empty Project"
                else None
            )
        )
        self.assertFalse(state["scene"]["dirty"])

        summary = self.harness.client.get_summary()
        self.assertEqual(summary["project"]["id"], project_id)
        self.assertEqual(
            summary["project"]["activeScene"], "scenes/main.scene.yaml"
        )
        self.assertNotIn("sourceKind", summary)
        self.assertNotIn("currentDocumentPath", summary)
        self.assertNotIn("permission", summary)

        scene_state = self.harness.client.get_scene()
        self.assertEqual(scene_state["sceneName"], "Empty Project")
        self.assertNotIn("currentDocumentPath", scene_state)
        self.assertNotIn("sourceKind", scene_state)
        self.assertNotIn("permission", scene_state)
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
        self.assertEqual(saved["project"]["id"], project_id)
