#include "core/editor/viewport_overlay.hpp"

#include "core/debug_draw/debug_draw.hpp"
#include "core/editor/editor_state.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <imgui.h>
#include <ImGuizmo.h>

namespace LX_core {

ViewportOverlay::ViewportOverlay(CommandBus &commandBus, EditorState &editorState,
                                 Scene &scene)
    : m_commandBus(commandBus), m_editorState(editorState), m_scene(scene) {}

ViewportOverlay::Snapshot ViewportOverlay::makeSnapshot() const {
  Snapshot snapshot;
  snapshot.previewEnabled = m_editorState.isPreviewEnabled();

  if (const auto activeCamera = m_editorState.resolveActiveCamera(m_scene)) {
    snapshot.activeCameraPath = activeCamera->getPath();
  }
  if (const auto editorCamera = m_editorState.getEditorCamera()) {
    snapshot.editorCameraPath = editorCamera->getPath();
  }
  if (const auto previewCamera = m_editorState.getPreviewCamera()) {
    snapshot.previewCameraPath = previewCamera->getPath();
  }
  if (const auto selected = m_editorState.getSelected()) {
    snapshot.selectedPath = selected->getPath();
  }

  snapshot.gizmoOperation = m_gizmoOperation;
  snapshot.hintText = std::string("F preview: ") +
                      (snapshot.previewEnabled ? "ON" : "OFF") +
                      " | active: " +
                      (snapshot.activeCameraPath.empty() ? "<none>"
                                                         : snapshot.activeCameraPath);
  return snapshot;
}

ViewportOverlay::GizmoOperation ViewportOverlay::getGizmoOperation() const {
  return m_gizmoOperation;
}

void ViewportOverlay::setGizmoOperation(const GizmoOperation operation) {
  m_gizmoOperation = operation;
}

bool ViewportOverlay::handleGizmoHotkeys(const int imguiKeyOrChar) {
  if (imguiKeyOrChar == 'W' || imguiKeyOrChar == 'w') {
    m_gizmoOperation = GizmoOperation::Translate;
    return true;
  }
  if (imguiKeyOrChar == 'E' || imguiKeyOrChar == 'e') {
    m_gizmoOperation = GizmoOperation::Rotate;
    return true;
  }
  if (imguiKeyOrChar == 'R' || imguiKeyOrChar == 'r') {
    m_gizmoOperation = GizmoOperation::Scale;
    return true;
  }
  return false;
}

bool ViewportOverlay::shouldRenderEditorOverlay() const {
  return !m_editorState.isPreviewEnabled();
}

CommandResult ViewportOverlay::dispatchPreviewToggle() {
  return m_commandBus.dispatch("preview toggle");
}

CommandResult ViewportOverlay::dispatchGizmoCommit(
    std::string_view path, const GizmoTransformComponents &components) {
  switch (m_gizmoOperation) {
  case GizmoOperation::Translate:
    return m_commandBus.dispatch("move \"" + std::string(path) + "\" " +
                                 std::to_string(components.translation.x) + " " +
                                 std::to_string(components.translation.y) + " " +
                                 std::to_string(components.translation.z));
  case GizmoOperation::Rotate:
    return m_commandBus.dispatch("rotate \"" + std::string(path) + "\" " +
                                 std::to_string(components.rotationEulerDegrees.x) + " " +
                                 std::to_string(components.rotationEulerDegrees.y) + " " +
                                 std::to_string(components.rotationEulerDegrees.z));
  case GizmoOperation::Scale:
    return m_commandBus.dispatch("scale \"" + std::string(path) + "\" " +
                                 std::to_string(components.scale.x) + " " +
                                 std::to_string(components.scale.y) + " " +
                                 std::to_string(components.scale.z));
  }
  return CommandResult{false, "unknown gizmo operation", {}};
}

CommandResult ViewportOverlay::dispatchPickingClick(const Vec2f &screenPixel,
                                                    const Vec2f &viewportSize) {
  const auto editorCameraNode = m_editorState.getEditorCamera();
  if (!editorCameraNode) {
    return CommandResult{false, "no editor camera", {}};
  }
  auto editorCamera = editorCameraNode->getComponent<CameraComponent>();
  if (!editorCamera.has_value()) {
    return CommandResult{false, "editor camera missing component", {}};
  }

  const Ray ray = editorCamera->get().pickRay(screenPixel, viewportSize);
  const auto hit = m_scene.pick(ray, Layer_All & ~Layer_EditorOverlay);
  if (hit.has_value() && hit->node) {
    return m_commandBus.dispatch("select \"" + hit->node->getPath() + "\"");
  }
  return m_commandBus.dispatch("deselect");
}

ViewportOverlay::PanelRect ViewportOverlay::getPanelRect() const {
  return m_lastPanelRect;
}

void ViewportOverlay::enqueueDebugDraw() const {
  if (!shouldRenderEditorOverlay()) {
    return;
  }

  const auto selected = m_editorState.getSelected();
  if (selected) {
    const auto bounds = selected->getWorldBounds();
    if (bounds.isValid()) {
      DebugDraw::wireBox(bounds, DebugDraw::Color::yellow());
    }
  }

  for (const auto &cameraNode : m_scene.getCameras()) {
    if (!cameraNode || cameraNode == m_editorState.getEditorCamera()) {
      continue;
    }
    auto camera = cameraNode->getComponent<CameraComponent>();
    if (!camera.has_value()) {
      continue;
    }
    const Mat4f viewProj = camera->get().getProjMatrix() * camera->get().getViewMatrix();
    DebugDraw::frustum(viewProj, DebugDraw::Color::white());
  }

  for (const auto &light : m_scene.getLights()) {
    const auto directionalLight = std::dynamic_pointer_cast<DirectionalLight>(light);
    if (!directionalLight || !directionalLight->ubo) {
      continue;
    }

    Vec3f origin{0.0f, 0.0f, 0.0f};
    if (SceneNode *lightNode = m_scene.findByPath("/dir_light")) {
      origin = Transform::fromMat4(lightNode->getWorldTransform()).translation;
    }
    Vec3f direction = Vec3f{directionalLight->ubo->param.dir.x, directionalLight->ubo->param.dir.y, directionalLight->ubo->param.dir.z};
    if (direction.length2() <= 1e-6f) {
      direction = Vec3f{0.0f, -1.0f, 0.0f};
    } else {
      direction = direction.normalized();
    }
    DebugDraw::arrow(origin, origin + direction * 2.0f,
                     DebugDraw::Color::yellow());
  }
}

ViewportOverlay::PanelRect ViewportOverlay::computeViewportRect() const {
  if (m_lastPanelRect.size.x > 1.0f && m_lastPanelRect.size.y > 1.0f) {
    return m_lastPanelRect;
  }

  PanelRect rect;
  const ImGuiIO &io = ImGui::GetIO();
  rect.size = Vec2f{io.DisplaySize.x > 0.0f ? io.DisplaySize.x : 1.0f,
                    io.DisplaySize.y > 0.0f ? io.DisplaySize.y : 1.0f};
  return rect;
}

void ViewportOverlay::draw() {
  if (!ImGui::Begin("Viewport")) {
    ImGui::End();
    return;
  }

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 canvasMin = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  if (canvasSize.x < 1.0f) {
    canvasSize.x = 1.0f;
  }
  if (canvasSize.y < 1.0f) {
    canvasSize.y = 1.0f;
  }
  ImGui::InvisibleButton("##viewport_canvas", canvasSize,
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight);
  const bool viewportHovered = ImGui::IsItemHovered();
  m_lastPanelRect.origin = Vec2f{canvasMin.x, canvasMin.y};
  m_lastPanelRect.size = Vec2f{canvasSize.x, canvasSize.y};

  m_gizmoHovered = false;
  const Snapshot snapshot = makeSnapshot();
  if (!shouldRenderEditorOverlay()) {
    m_gizmoUsing = false;
    ImGui::End();
    return;
  }

  const PanelRect rect = computeViewportRect();
  const char *modeText = modeLabel(snapshot.gizmoOperation);
  drawList->PushClipRect(ImVec2(rect.origin.x, rect.origin.y),
                         ImVec2(rect.origin.x + rect.size.x,
                                rect.origin.y + rect.size.y),
                         true);
  drawList->AddText(ImVec2(rect.origin.x + 16.0f, rect.origin.y + 16.0f), IM_COL32(255, 255, 0, 255),
                    snapshot.hintText.c_str());
  drawList->AddText(ImVec2(rect.origin.x + 16.0f, rect.origin.y + 56.0f), IM_COL32(120, 255, 120, 255), modeText);
  if (!snapshot.selectedPath.empty()) {
    const std::string selectedText = "Selected: " + snapshot.selectedPath;
    drawList->AddText(ImVec2(rect.origin.x + 16.0f, rect.origin.y + 36.0f), IM_COL32(255, 255, 255, 255),
                      selectedText.c_str());
  }

  const auto selected = m_editorState.getSelected();
  const auto editorCameraNode = m_editorState.getEditorCamera();
  if (!selected || !editorCameraNode) {
    drawList->PopClipRect();
    ImGui::End();
    return;
  }
  auto editorCamera = editorCameraNode->getComponent<CameraComponent>();
  if (!editorCamera.has_value()) {
    drawList->PopClipRect();
    ImGui::End();
    return;
  }

  ImGuizmo::SetDrawlist(drawList);
  ImGuizmo::SetOrthographic(editorCamera->get().type == CameraType::Orthographic);
  ImGuiIO &io = ImGui::GetIO();
  ImGuizmo::SetRect(rect.origin.x, rect.origin.y, rect.size.x, rect.size.y);

  float view[16] = {};
  float projection[16] = {};
  float objectMatrix[16] = {};
  GizmoAdapter::toFloat16(editorCamera->get().getViewMatrix(), view);
  GizmoAdapter::toFloat16(editorCamera->get().getProjMatrix(), projection);
  GizmoAdapter::toFloat16(selected->getLocalTransform().toMat4(), objectMatrix);

  const bool changed = ImGuizmo::Manipulate(
      view, projection, toImGuizmoOperation(m_gizmoOperation), ImGuizmo::LOCAL,
      objectMatrix, nullptr, nullptr, nullptr, nullptr);
  m_gizmoHovered = ImGuizmo::IsOver();
  const bool usingNow = ImGuizmo::IsUsing();

  if (!m_gizmoUsing && usingNow) {
    m_gizmoPreDragTransform = selected->getLocalTransform();
    m_gizmoDragPath = selected->getPath();
  }

  if (changed) {
    selected->setLocalTransform(Transform::fromMat4(GizmoAdapter::fromFloat16(objectMatrix)));
  }

  if (m_gizmoUsing && !usingNow && !m_gizmoDragPath.empty()) {
    const auto committedTransform = selected->getLocalTransform();
    const auto components = GizmoAdapter::decompose(committedTransform.toMat4());
    if (m_gizmoPreDragTransform.has_value()) {
      selected->setLocalTransform(*m_gizmoPreDragTransform);
    }
    const CommandResult commit = dispatchGizmoCommit(m_gizmoDragPath, components);
    if (!commit.ok) {
      selected->setLocalTransform(committedTransform);
    }
    m_gizmoPreDragTransform.reset();
    m_gizmoDragPath.clear();
  }
  m_gizmoUsing = usingNow;

  const bool mouseInsideViewport = io.MousePos.x >= rect.origin.x && io.MousePos.x <= rect.origin.x + rect.size.x &&
                                  io.MousePos.y >= rect.origin.y && io.MousePos.y <= rect.origin.y + rect.size.y;
  if (!m_gizmoHovered && !m_gizmoUsing && io.MouseClicked[0] && viewportHovered &&
      mouseInsideViewport) {
    (void)dispatchPickingClick(Vec2f{io.MousePos.x - rect.origin.x, io.MousePos.y - rect.origin.y},
                               rect.size);
  }
  drawList->PopClipRect();
  ImGui::End();
}

const char *ViewportOverlay::modeLabel(const GizmoOperation operation) {
  switch (operation) {
  case GizmoOperation::Translate:
    return "W Translate";
  case GizmoOperation::Rotate:
    return "E Rotate";
  case GizmoOperation::Scale:
    return "R Scale";
  }
  return "Gizmo";
}

ImGuizmo::OPERATION ViewportOverlay::toImGuizmoOperation(
    const GizmoOperation operation) {
  switch (operation) {
  case GizmoOperation::Translate:
    return ImGuizmo::TRANSLATE;
  case GizmoOperation::Rotate:
    return ImGuizmo::ROTATE;
  case GizmoOperation::Scale:
    return ImGuizmo::SCALE;
  }
  return ImGuizmo::TRANSLATE;
}

} // namespace LX_core
