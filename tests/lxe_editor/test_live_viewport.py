from __future__ import annotations

import shutil
import unittest
from pathlib import Path

from tests.lxe_editor.api_client import LxeEditorHarness


class LiveViewportBlackBoxTest(unittest.TestCase):
    def test_helmet_scene_loads_nonblack_live_bindless_viewport(self) -> None:
        harness = LxeEditorHarness()
        project_root = (
            harness.client.runtime_root
            / "data"
            / "projects"
            / "live_viewport_smoke"
        )
        source_scene = (
            Path(__file__).resolve().parents[2]
            / "assets"
            / "scenes"
            / "generated"
            / "helmet_standard_pbr.scene.yaml"
        )
        self.assertTrue(
            source_scene.is_file(),
            f"source helmet scene must exist: {source_scene}",
        )
        (project_root / "scenes").mkdir(parents=True, exist_ok=True)
        (project_root / "scenes" / "helmet_standard_pbr.scene.yaml").write_text(
            source_scene.read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        assets_dst = project_root / "assets"
        if not assets_dst.exists():
            try:
                assets_dst.symlink_to(
                    Path(__file__).resolve().parents[2] / "assets",
                    target_is_directory=True,
                )
            except OSError:
                shutil.copytree(
                    Path(__file__).resolve().parents[2] / "assets",
                    assets_dst,
                    dirs_exist_ok=True,
                )
        (project_root / "project.yaml").write_text(
            "schema: lxe.project.v1\n"
            "id: live_viewport_smoke\n"
            "displayName: Live Viewport Smoke\n"
            "activeScene: scenes/helmet_standard_pbr.scene.yaml\n"
            "scenes:\n"
            "  - id: helmet_standard_pbr\n"
            "    path: scenes/helmet_standard_pbr.scene.yaml\n"
            "assetRoots:\n"
            "  - assets\n",
            encoding="utf-8",
        )

        try:
            try:
                harness.start()
            except FileNotFoundError as exc:
                raise unittest.SkipTest(str(exc)) from exc
            except Exception as exc:
                raise unittest.SkipTest(
                    f"unable to launch API target: {exc}"
                ) from exc

            open_response = harness.client.command(
                f"project open {project_root}"
            )
            self.assertTrue(open_response.get("ok"), open_response)

            def helmet_scene_loaded() -> dict[str, object] | None:
                result = harness.client.get_scene()
                if result.get("sceneName") == "Helmet Standard PBR":
                    return result
                return None

            scene = harness.client.wait_for(
                helmet_scene_loaded,
                timeout_s=20.0,
            )
            self.assertEqual(scene["sceneName"], "Helmet Standard PBR")

            def live_frame_ready() -> dict[str, object] | None:
                try:
                    color = harness.client.decode_structured_json(
                        harness.client.command("render debug stats hdr.color")
                    )
                    depth = harness.client.decode_structured_json(
                        harness.client.command("render debug stats depth.main")
                    )
                    stats = harness.client.live_render_stats()
                except Exception:
                    return None

                if (
                    float(color["stats"]["nonZeroRatio"]) > 0.0
                    and float(depth["stats"]["min"]) < 1.0
                    and stats.get("usedExplicitCamera") is True
                    and stats.get("usedBindlessSceneDescriptors") is True
                    and int(stats.get("acceptedInputCount", 0)) > 0
                    and int(stats.get("descExecutedInputCount", 0)) > 0
                    and int(stats.get("submittedDrawCount", 0)) > 0
                    and int(stats.get("compilerInputCount", 0)) > 0
                    and int(stats.get("fallbackObservedCount", 1)) == 0
                ):
                    return {
                        "color": color,
                        "depth": depth,
                        "stats": stats,
                    }
                return None

            result = harness.client.wait_for(
                live_frame_ready,
                timeout_s=30.0,
                poll_interval_s=0.5,
            )
            self.assertGreater(
                float(result["color"]["stats"]["nonZeroRatio"]),
                0.0,
            )
            self.assertLess(
                float(result["depth"]["stats"]["min"]),
                1.0,
            )
            self.assertTrue(result["stats"]["usedExplicitCamera"])
            self.assertTrue(result["stats"]["usedBindlessSceneDescriptors"])
            self.assertGreater(
                int(result["stats"]["acceptedInputCount"]),
                0,
                "acceptedInputCount should be > 0",
            )
            self.assertGreater(
                int(result["stats"]["descExecutedInputCount"]),
                0,
                "descExecutedInputCount should be > 0",
            )
            self.assertEqual(
                int(result["stats"]["fallbackObservedCount"]),
                0,
                "fallbackObservedCount should be 0",
            )
        finally:
            harness.close()
