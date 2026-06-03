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

    def test_default_project_opens_builtin_scenes_by_id(self) -> None:
        open_project = self.harness.client.command("project open lxe_default")
        self.assertTrue(open_project["ok"], open_project)
        state = self.harness.client.wait_for(
            lambda: (
                result
                if (
                    result := self.harness.client.get_state()
                )["project"]
                and result["project"]["id"] == "lxe_default"
                and result["project"]["activeScene"]
                == "scenes/lxe_editor.scene.yaml"
                and result["scene"]["sceneName"] == "lxe_editor"
                else None
            )
        )
        self.assertFalse(state["scene"]["dirty"])

        list_response = self.harness.client.command("scene list")
        self.assertTrue(list_response["ok"], list_response)
        scenes = self.harness.client.decode_structured_json(list_response)
        scene_ids = {entry["id"] for entry in scenes["scenes"]}
        self.assertIn("realtime_offline_compare_diagnostic", scene_ids)
        self.assertIn("ibl_metal_sphere", scene_ids)
        self.assertIn("lxe_editor", scene_ids)

        diagnostic_open = self.harness.client.command(
            "scene open realtime_offline_compare_diagnostic"
        )
        self.assertTrue(diagnostic_open["ok"], diagnostic_open)
        self.harness.client.wait_for(
            lambda: (
                result
                if (
                    result := self.harness.client.get_state()
                )["project"]["activeScene"]
                == "scenes/realtime_offline_compare_diagnostic.scene.yaml"
                and result["scene"]["sceneName"]
                == "Realtime Offline Compare Diagnostic"
                else None
            )
        )

        ibl_open = self.harness.client.command("scene open ibl_metal_sphere")
        self.assertTrue(ibl_open["ok"], ibl_open)
        self.harness.client.wait_for(
            lambda: (
                result
                if (
                    result := self.harness.client.get_state()
                )["project"]["activeScene"]
                == "scenes/ibl_metal_sphere.scene.yaml"
                and result["scene"]["sceneName"] == "IBL Metal Sphere"
                else None
            )
        )

        editor_open = self.harness.client.command("scene open lxe_editor")
        self.assertTrue(editor_open["ok"], editor_open)
        self.harness.client.wait_for(
            lambda: (
                result
                if (
                    result := self.harness.client.get_state()
                )["project"]["activeScene"]
                == "scenes/lxe_editor.scene.yaml"
                and result["scene"]["sceneName"] == "lxe_editor"
                else None
            )
        )

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
        self.assertTrue(new_response["ok"], new_response)
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
        self.assertNotIn("source" + "Kind", summary)
        self.assertNotIn("runtimeScenePath", summary)
        self.assertNotIn("permission", summary)

        scene_state = self.harness.client.get_scene()
        self.assertEqual(scene_state["sceneName"], "Empty Project")
        self.assertNotIn("runtimeScenePath", scene_state)
        self.assertNotIn("source" + "Kind", scene_state)
        self.assertNotIn("permission", scene_state)
        self.assertFalse(scene_state["dirty"])

        list_response = self.harness.client.command("scene list")
        self.assertTrue(list_response["ok"], list_response)
        scenes = self.harness.client.decode_structured_json(list_response)
        self.assertIn("main", {entry["id"] for entry in scenes["scenes"]})

        save_response = self.harness.client.command("scene save")
        self.assertTrue(save_response["ok"])
        saved = self.harness.client.decode_structured_json(save_response)
        saved_path = pathlib.Path(saved["path"])
        self.assertTrue(saved_path.is_file())
        self.assertEqual(saved["project"]["id"], project_id)
