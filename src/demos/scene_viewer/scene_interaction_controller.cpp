#include "demos/scene_viewer/scene_interaction_controller.hpp"

#include "core/editor/editor_state.hpp"
#include "core/input/mouse_button.hpp"
#include "core/scene/components/camera_component.hpp"

namespace LX_demo::scene_viewer {

SceneInteractionController::SceneInteractionController(
    LX_core::CommandBus& commandBus, LX_core::EditorState& editorState,
    LX_core::Scene& scene)
    : m_commandBus(commandBus), m_editorState(editorState), m_scene(scene) {}

LX_core::CommandResult SceneInteractionController::dispatchPickingClick(
    const LX_core::Vec2f& screenPixel, const LX_core::Vec2f& viewportSize) {
  const auto editorCameraNode = m_editorState.getEditorCamera();
  if (!editorCameraNode) {
    return LX_core::CommandResult{false, "no editor camera", {}, {}};
  }

  const auto editorCamera =
      editorCameraNode->getComponent<LX_core::CameraComponent>();
  if (!editorCamera.has_value()) {
    return LX_core::CommandResult{false, "editor camera missing component", {},
                                  {}};
  }

  const LX_core::Ray ray = editorCamera->get().pickRay(screenPixel, viewportSize);
  const auto hit =
      m_scene.pick(ray, LX_core::Layer_All & ~LX_core::Layer_EditorOverlay);
  if (hit.has_value() && hit->node) {
    return m_commandBus.dispatch("select \"" + hit->node->getPath() + "\"");
  }
  return m_commandBus.dispatch("deselect");
}

void SceneInteractionController::updateSelectionMode(
    LX_core::IInputState& input, const LX_core::Vec2f& viewportSize) {
  const bool leftDown =
      input.isMouseButtonDown(LX_core::MouseButton::Left);
  if (leftDown && !m_prevLeftDown) {
    (void)dispatchPickingClick(input.getMousePosition(), viewportSize);
  }
  m_prevLeftDown = leftDown;
}

} // namespace LX_demo::scene_viewer
