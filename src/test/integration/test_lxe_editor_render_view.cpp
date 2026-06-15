#include "editor/app/editor_state.hpp"
#include "editor/render/editor_render_view.hpp"

#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <iostream>

using namespace LX_core;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

SceneNodeSharedPtr makeCameraNode(const char *nodeName, const char *pathName,
                                 VisibilityLayerMask mask) {
  auto node = SceneNode::create(nodeName);
  node->setName(pathName);
  const auto camera = node->addComponent<CameraComponent>();
  EXPECT(camera.has_value(), "camera component should be created");
  if (!camera.has_value()) {
    return node;
  }
  camera->get().setCullingMask(mask);
  camera->get().clearTarget();
  camera->get().setAspect(16.0f / 9.0f);
  camera->get().updateMatrices();
  return node;
}

struct Fixture final {
  SceneSharedPtr scene = Scene::create(nullptr);
  SceneNodeSharedPtr editorCamera =
      makeCameraNode("editor_camera", "editor_cam", Layer_All);
  SceneNodeSharedPtr gameCamera =
      makeCameraNode("game_camera", "game_cam",
                     Layer_All & ~Layer_EditorOverlay);
  EditorState editorState;

  Fixture() {
    scene->addCamera(editorCamera);
    scene->addCamera(gameCamera);
    editorState.setEditorCamera(editorCamera);
    editorState.setPreviewCamera(gameCamera);
  }
};

void testEditorViewUsesEditorCameraWhenPreviewOff() {
  Fixture fixture;
  fixture.editorState.setPreviewEnabled(false);
  (void)fixture.editorState.syncActiveCamera(*fixture.scene);

  const auto view = LX_editor::buildEditorRenderView(
      fixture.editorState, *fixture.scene, Vec2f{1280.0f, 720.0f});

  EXPECT(view.has_value(), "preview-off view should be available");
  if (!view.has_value()) {
    return;
  }
  EXPECT(view->cameraPath == "/editor_cam",
         "preview-off view should use editor camera");
  EXPECT(view->previewEnabled == false,
         "preview-off view should report preview disabled");
  EXPECT(view->viewportExtent.x == 1280.0f &&
             view->viewportExtent.y == 720.0f,
         "view should preserve viewport extent");
  EXPECT((view->visibleMask & Layer_EditorOverlay) == 0,
         "scene draw mask should exclude editor overlay");
  EXPECT(view->cameraResource.active,
         "view should carry an active camera resource");
}

void testPreviewViewUsesGameCameraWithoutTargetMatchDependency() {
  Fixture fixture;
  fixture.editorState.setPreviewEnabled(true);
  (void)fixture.editorState.syncActiveCamera(*fixture.scene);

  RenderTargetDesc hdrTarget;
  hdrTarget.role = RenderTargetRole::Offscreen;
  hdrTarget.colorFormat = ImageFormat::RGBA16Float;
  hdrTarget.depthFormat = ImageFormat::D32Float;
  const RenderTarget liveHdrTarget(hdrTarget);
  const auto gameCamera = fixture.gameCamera->getComponent<CameraComponent>();
  EXPECT(gameCamera.has_value(), "game camera component must exist");
  if (!gameCamera.has_value()) {
    return;
  }
  EXPECT(!gameCamera->get().matchesTarget(liveHdrTarget),
         "test requires old target matching to reject this camera");

  const auto view = LX_editor::buildEditorRenderView(
      fixture.editorState, *fixture.scene, Vec2f{640.0f, 480.0f});

  EXPECT(view.has_value(), "preview view should be available");
  if (!view.has_value()) {
    return;
  }
  EXPECT(view->cameraPath == "/game_cam",
         "preview view should use game camera");
  EXPECT(view->previewEnabled == true,
         "preview view should report preview enabled");
  EXPECT(view->visibleMask != 0,
         "explicit render view should not be filtered out by target mismatch");
  EXPECT((view->visibleMask & Layer_EditorOverlay) == 0,
         "preview view should exclude editor overlay");
}

} // namespace

int main() {
  testEditorViewUsesEditorCameraWhenPreviewOff();
  testPreviewViewUsesGameCameraWithoutTargetMatchDependency();
  if (g_failures != 0) {
    std::cerr << g_failures << " lxe editor render view checks failed\n";
    return 1;
  }
  return 0;
}
