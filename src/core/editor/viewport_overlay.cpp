#include "core/editor/viewport_overlay.hpp"

#include "core/debug_draw/debug_draw.hpp"
#include "core/editor/editor_state.hpp"
#include "core/math/quat.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <imgui.h>
#include <ImGuizmo.h>

#include <sstream>

namespace LX_core {
namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

[[nodiscard]] std::string quoteToken(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

[[nodiscard]] std::string joinLines(const std::vector<std::string> &lines) {
  std::ostringstream oss;
  for (usize i = 0; i < lines.size(); ++i) {
    if (i != 0) {
      oss << '\n';
    }
    oss << lines[i];
  }
  return oss.str();
}

[[nodiscard]] Vec3f safeScaleRatio(const Vec3f &before, const Vec3f &after) {
  const auto ratio = [](const float oldValue, const float newValue) {
    return std::abs(oldValue) <= 1e-6f ? 1.0f : newValue / oldValue;
  };
  return Vec3f{ratio(before.x, after.x), ratio(before.y, after.y),
               ratio(before.z, after.z)};
}

[[nodiscard]] Transform applyTransformDelta(
    const ViewportOverlay::GizmoOperation operation, const Transform &nodeBefore,
    const Transform &primaryBefore, const Transform &primaryAfter) {
  Transform updated = nodeBefore;
  switch (operation) {
  case ViewportOverlay::GizmoOperation::Translate:
    updated.translation =
        nodeBefore.translation + (primaryAfter.translation - primaryBefore.translation);
    break;
  case ViewportOverlay::GizmoOperation::Rotate: {
    const Quatf delta =
        (primaryAfter.rotation * primaryBefore.rotation.conjugate()).normalized();
    updated.rotation = (delta * nodeBefore.rotation).normalized();
    break;
  }
  case ViewportOverlay::GizmoOperation::Scale: {
    const Vec3f ratio = safeScaleRatio(primaryBefore.scale, primaryAfter.scale);
    updated.scale = Vec3f{nodeBefore.scale.x * ratio.x, nodeBefore.scale.y * ratio.y,
                          nodeBefore.scale.z * ratio.z};
    break;
  }
  }
  return updated;
}

[[nodiscard]] Quatf eulerDegreesToQuat(const Vec3f &degrees) {
  const Quatf qx =
      Quatf::fromAxisAngle(Vec3f{1.0f, 0.0f, 0.0f}, degrees.x * kDegToRad);
  const Quatf qy =
      Quatf::fromAxisAngle(Vec3f{0.0f, 1.0f, 0.0f}, degrees.y * kDegToRad);
  const Quatf qz =
      Quatf::fromAxisAngle(Vec3f{0.0f, 0.0f, 1.0f}, degrees.z * kDegToRad);
  return (qz * qy * qx).normalized();
}

[[nodiscard]] std::string buildSingleGizmoLine(
    const ViewportOverlay::GizmoOperation operation, std::string_view path,
    const Transform &targetTransform) {
  const GizmoTransformComponents components =
      GizmoAdapter::decompose(targetTransform.toMat4());
  switch (operation) {
  case ViewportOverlay::GizmoOperation::Translate:
    return "move " + quoteToken(path) + " " +
           std::to_string(components.translation.x) + " " +
           std::to_string(components.translation.y) + " " +
           std::to_string(components.translation.z);
  case ViewportOverlay::GizmoOperation::Rotate:
    return "rotate " + quoteToken(path) + " " +
           std::to_string(components.rotationEulerDegrees.x) + " " +
           std::to_string(components.rotationEulerDegrees.y) + " " +
           std::to_string(components.rotationEulerDegrees.z);
  case ViewportOverlay::GizmoOperation::Scale:
    return "scale " + quoteToken(path) + " " +
           std::to_string(components.scale.x) + " " +
           std::to_string(components.scale.y) + " " +
           std::to_string(components.scale.z);
  }
  return {};
}

} // namespace

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
  if (const auto selected = m_editorState.getPrimarySelected()) {
    snapshot.selectedPath = selected->get().getPath();
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
  Transform target;
  target.translation = components.translation;
  target.rotation = eulerDegreesToQuat(components.rotationEulerDegrees);
  target.scale = components.scale;
  return m_commandBus.dispatch(buildSingleGizmoLine(m_gizmoOperation, path, target));
}

CommandResult ViewportOverlay::dispatchGizmoSelectionCommit(
    const std::vector<std::string> &paths,
    const std::vector<Transform> &beforeTransforms,
    const std::vector<Transform> &afterTransforms) {
  if (paths.empty() || paths.size() != beforeTransforms.size() ||
      paths.size() != afterTransforms.size()) {
    return CommandResult{false, "invalid gizmo selection commit state", {}};
  }

  if (paths.size() == 1) {
    return m_commandBus.dispatch(
        buildSingleGizmoLine(m_gizmoOperation, paths.front(), afterTransforms.front()));
  }

  if (m_gizmoOperation == GizmoOperation::Translate) {
    const Vec3f delta =
        afterTransforms.front().translation - beforeTransforms.front().translation;
    std::ostringstream oss;
    oss << "move";
    for (const auto &path : paths) {
      oss << ' ' << quoteToken(path);
    }
    oss << ' ' << delta.x << ' ' << delta.y << ' ' << delta.z;
    return m_commandBus.dispatch(oss.str());
  }

  std::vector<std::string> lines;
  lines.reserve(paths.size());
  for (usize i = 0; i < paths.size(); ++i) {
    lines.push_back(buildSingleGizmoLine(m_gizmoOperation, paths[i], afterTransforms[i]));
  }
  const std::vector<CommandResult> results = m_commandBus.dispatchScript(joinLines(lines));
  if (results.empty()) {
    return CommandResult{false, "empty gizmo commit script", {}};
  }
  for (const auto &result : results) {
    if (!result.ok) {
      return result;
    }
  }
  return CommandResult{true, "committed gizmo selection delta", {}};
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

  for (const auto &selected : m_editorState.getSelected()) {
    if (!selected) {
      continue;
    }
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

  const auto selected = m_editorState.getPrimarySelected();
  const auto editorCameraNode = m_editorState.getEditorCamera();
  if (!selected.has_value() || !editorCameraNode) {
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
  GizmoAdapter::toFloat16(selected->get().getLocalTransform().toMat4(), objectMatrix);

  const bool changed = ImGuizmo::Manipulate(
      view, projection, toImGuizmoOperation(m_gizmoOperation), ImGuizmo::LOCAL,
      objectMatrix, nullptr, nullptr, nullptr, nullptr);
  m_gizmoHovered = ImGuizmo::IsOver();
  const bool usingNow = ImGuizmo::IsUsing();

  if (!m_gizmoUsing && usingNow) {
    m_gizmoDragPaths.clear();
    m_gizmoPreDragTransforms.clear();
    for (const auto &selectedNode : m_editorState.getSelected()) {
      if (!selectedNode) {
        continue;
      }
      m_gizmoDragPaths.push_back(selectedNode->getPath());
      m_gizmoPreDragTransforms.push_back(selectedNode->getLocalTransform());
    }
  }

  if (changed && !m_gizmoPreDragTransforms.empty()) {
    const Transform primaryCommitted =
        Transform::fromMat4(GizmoAdapter::fromFloat16(objectMatrix));
    const Transform primaryBefore = m_gizmoPreDragTransforms.back();
    const auto selectedNodes = m_editorState.getSelected();
    for (usize i = 0; i < selectedNodes.size() && i < m_gizmoPreDragTransforms.size(); ++i) {
      if (!selectedNodes[i]) {
        continue;
      }
      selectedNodes[i]->setLocalTransform(applyTransformDelta(
          m_gizmoOperation, m_gizmoPreDragTransforms[i], primaryBefore, primaryCommitted));
    }
  }

  if (m_gizmoUsing && !usingNow && !m_gizmoDragPaths.empty()) {
    std::vector<Transform> committedTransforms;
    committedTransforms.reserve(m_gizmoDragPaths.size());
    for (const auto &path : m_gizmoDragPaths) {
      if (SceneNode *node = m_scene.findByPath(path)) {
        committedTransforms.push_back(node->getLocalTransform());
      }
    }

    for (usize i = 0; i < m_gizmoDragPaths.size() && i < m_gizmoPreDragTransforms.size(); ++i) {
      if (SceneNode *node = m_scene.findByPath(m_gizmoDragPaths[i])) {
        node->setLocalTransform(m_gizmoPreDragTransforms[i]);
      }
    }

    const CommandResult commit = dispatchGizmoSelectionCommit(
        m_gizmoDragPaths, m_gizmoPreDragTransforms, committedTransforms);
    if (!commit.ok) {
      for (usize i = 0; i < m_gizmoDragPaths.size() && i < committedTransforms.size(); ++i) {
        if (SceneNode *node = m_scene.findByPath(m_gizmoDragPaths[i])) {
          node->setLocalTransform(committedTransforms[i]);
        }
      }
    }
    m_gizmoPreDragTransforms.clear();
    m_gizmoDragPaths.clear();
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
