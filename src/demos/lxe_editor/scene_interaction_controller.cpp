#include "demos/lxe_editor/scene_interaction_controller.hpp"

#include "core/debug_draw/debug_draw.hpp"
#include "core/editor/editor_state.hpp"
#include "core/input/mouse_button.hpp"
#include "core/math/bounds.hpp"
#include "core/scene/components/camera_component.hpp"

#include <algorithm>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] LX_core::BoundingBox expandedBounds(
    const LX_core::BoundingBox& bounds, const float padding) {
  if (!bounds.isValid()) {
    return bounds;
  }
  return LX_core::BoundingBox{
      bounds.min - LX_core::Vec3f{padding, padding, padding},
      bounds.max + LX_core::Vec3f{padding, padding, padding}};
}

} // namespace

SceneInteractionController::SceneInteractionController(
    LX_core::CommandBus& commandBus, LX_core::EditorState& editorState,
    LX_core::Scene& scene)
    : m_commandBus(commandBus), m_editorState(editorState), m_scene(scene) {}

LX_core::CommandResult SceneInteractionController::dispatchPickingClick(
    const LX_core::Vec2f& screenPixel, const LX_core::Vec2f& viewportSize) {
  return dispatchPickingClick(
      screenPixel,
      SceneViewRect{.x = 0.0f,
                    .y = 0.0f,
                    .width = viewportSize.x,
                    .height = viewportSize.y});
}

LX_core::CommandResult SceneInteractionController::dispatchPickingClick(
    const LX_core::Vec2f& screenPixel, const SceneViewRect& sceneViewRect) {
  if (!sceneViewRect.isValid()) {
    return LX_core::CommandResult{false, "invalid scene view rect", {}, {}};
  }
  if (!sceneViewRect.contains(screenPixel)) {
    return LX_core::CommandResult{false, "click outside scene view rect", {}, {}};
  }

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

  const LX_core::Ray ray = editorCamera->get().pickRay(
      sceneViewRect.localPixel(screenPixel), sceneViewRect.size());
  const auto hit =
      m_scene.pick(ray, LX_core::Layer_All & ~LX_core::Layer_EditorOverlay);
  if (hit.has_value() && hit->node) {
    LX_core::CommandResult result =
        m_commandBus.dispatch("select \"" + hit->node->getPath() + "\"");
    if (result.ok) {
      m_lastHitMarker = HitMarker{hit->node,
                                  ray.origin + ray.direction * hit->distance};
    }
    return result;
  }
  m_lastHitMarker.reset();
  return m_commandBus.dispatch("deselect");
}

void SceneInteractionController::updateSelectionMode(
    LX_core::IInputState& input, const LX_core::Vec2f& viewportSize) {
  updateSelectionMode(
      input, SceneViewRect{.x = 0.0f,
                           .y = 0.0f,
                           .width = viewportSize.x,
                           .height = viewportSize.y});
}

void SceneInteractionController::updateSelectionMode(
    LX_core::IInputState& input, const SceneViewRect& sceneViewRect) {
  const bool leftDown =
      input.isMouseButtonDown(LX_core::MouseButton::Left);
  if (leftDown && !m_prevLeftDown) {
    (void)dispatchPickingClick(input.getMousePosition(), sceneViewRect);
  }
  m_prevLeftDown = leftDown;
}

void SceneInteractionController::enqueueDebugDraw() const {
  if (m_editorState.isPreviewEnabled()) {
    return;
  }

  const auto primarySelected = m_editorState.getPrimarySelected();
  for (const auto& selected : m_editorState.getSelected()) {
    if (!selected) {
      continue;
    }
    const LX_core::BoundingBox bounds = selected->getWorldBounds();
    if (!bounds.isValid()) {
      continue;
    }

    const bool primary = primarySelected.has_value() &&
                         &primarySelected->get() == selected.get();
    if (primary) {
      LX_core::DebugDraw::wireBox(bounds, LX_core::Vec4f{0.2f, 1.0f, 1.0f, 1.0f});
      LX_core::DebugDraw::wireBox(expandedBounds(bounds, 0.02f),
                                  LX_core::Vec4f{0.2f, 1.0f, 1.0f, 1.0f});
    } else {
      LX_core::DebugDraw::wireBox(bounds, LX_core::DebugDraw::Color::yellow());
    }
  }

  if (!m_lastHitMarker.has_value()) {
    return;
  }

  const auto selected = m_editorState.getSelected();
  const bool markerStillSelected =
      std::any_of(selected.begin(), selected.end(),
                  [this](const LX_core::SceneNodeSharedPtr& node) {
                    const auto markerNode = m_lastHitMarker->node.lock();
                    return node && markerNode && node.get() == markerNode.get();
                  });
  if (!markerStillSelected) {
    return;
  }

  LX_core::DebugDraw::wireSphere(m_lastHitMarker->point, 0.08f,
                                 LX_core::Vec4f{1.0f, 0.35f, 0.15f, 1.0f});
}

std::optional<LX_core::Vec3f> SceneInteractionController::lastHitPoint() const {
  if (!m_lastHitMarker.has_value()) {
    return std::nullopt;
  }
  const auto selected = m_editorState.getSelected();
  const bool markerStillSelected =
      std::any_of(selected.begin(), selected.end(),
                  [this](const LX_core::SceneNodeSharedPtr& node) {
                    const auto markerNode = m_lastHitMarker->node.lock();
                    return node && markerNode && node.get() == markerNode.get();
                  });
  if (!markerStillSelected) {
    return std::nullopt;
  }
  return m_lastHitMarker->point;
}

} // namespace LX_demo::lxe_editor
