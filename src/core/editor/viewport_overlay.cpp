#include "core/editor/viewport_overlay.hpp"

#include "core/debug_draw/debug_draw.hpp"
#include "core/editor/editor_config.hpp"
#include "core/editor/editor_state.hpp"
#include "core/math/quat.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
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

void appendUniquePath(std::vector<std::string> &paths, std::string_view path) {
  const auto it = std::find(paths.begin(), paths.end(), path);
  if (it != paths.end()) {
    paths.erase(it);
  }
  paths.emplace_back(path);
}

[[nodiscard]] Vec2f clampPointToViewport(const Vec2f &point,
                                         const Vec2f &viewportSize) {
  return Vec2f{std::clamp(point.x, 0.0f, viewportSize.x),
               std::clamp(point.y, 0.0f, viewportSize.y)};
}

[[nodiscard]] std::optional<Vec2f>
projectWorldPointToViewport(const Vec3f &worldPoint, const Mat4f &viewProj,
                            const Vec2f &viewportSize) {
  const Vec4f clip = viewProj * Vec4f{worldPoint.x, worldPoint.y, worldPoint.z, 1.0f};
  if (std::abs(clip.w) <= 1e-6f || clip.w <= 0.0f) {
    return std::nullopt;
  }

  const Vec3f ndc = clip.toVec3();
  const float screenX = (ndc.x * 0.5f + 0.5f) * viewportSize.x;
  const float screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y;
  return Vec2f{screenX, screenY};
}

[[nodiscard]] std::optional<ViewportOverlay::SelectionRect>
projectBoundsToViewportRect(const BoundingBox &bounds, const Mat4f &viewProj,
                            const Vec2f &viewportSize) {
  if (!bounds.isValid()) {
    return std::nullopt;
  }

  const Vec3f corners[8] = {
      {bounds.min.x, bounds.min.y, bounds.min.z},
      {bounds.max.x, bounds.min.y, bounds.min.z},
      {bounds.min.x, bounds.max.y, bounds.min.z},
      {bounds.max.x, bounds.max.y, bounds.min.z},
      {bounds.min.x, bounds.min.y, bounds.max.z},
      {bounds.max.x, bounds.min.y, bounds.max.z},
      {bounds.min.x, bounds.max.y, bounds.max.z},
      {bounds.max.x, bounds.max.y, bounds.max.z},
  };

  bool hasProjectedPoint = false;
  ViewportOverlay::SelectionRect rect{
      Vec2f{viewportSize.x, viewportSize.y}, Vec2f{0.0f, 0.0f}};
  for (const Vec3f &corner : corners) {
    const auto projected =
        projectWorldPointToViewport(corner, viewProj, viewportSize);
    if (!projected.has_value()) {
      continue;
    }
    const Vec2f clamped = clampPointToViewport(*projected, viewportSize);
    rect.min.x = std::min(rect.min.x, clamped.x);
    rect.min.y = std::min(rect.min.y, clamped.y);
    rect.max.x = std::max(rect.max.x, clamped.x);
    rect.max.y = std::max(rect.max.y, clamped.y);
    hasProjectedPoint = true;
  }

  if (!hasProjectedPoint) {
    return std::nullopt;
  }
  return rect;
}

[[nodiscard]] BoundingBox expandedBounds(const BoundingBox &bounds, const float padding) {
  if (!bounds.isValid()) {
    return bounds;
  }
  return BoundingBox{bounds.min - Vec3f{padding, padding, padding},
                     bounds.max + Vec3f{padding, padding, padding}};
}

} // namespace

ViewportOverlay::ViewportOverlay(CommandBus &commandBus, EditorState &editorState,
                                 Scene &scene, EditorConfig config)
    : m_commandBus(commandBus), m_editorState(editorState), m_scene(scene),
      m_config(config) {}

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

ViewportOverlay::SelectionRect
ViewportOverlay::makeSelectionRect(const Vec2f &a, const Vec2f &b,
                                   const Vec2f &viewportSize) {
  const Vec2f clampedA = clampPointToViewport(a, viewportSize);
  const Vec2f clampedB = clampPointToViewport(b, viewportSize);
  return SelectionRect{
      Vec2f{std::min(clampedA.x, clampedB.x), std::min(clampedA.y, clampedB.y)},
      Vec2f{std::max(clampedA.x, clampedB.x), std::max(clampedA.y, clampedB.y)}};
}

float ViewportOverlay::selectionRectArea(const SelectionRect &rect) {
  return std::max(0.0f, rect.max.x - rect.min.x) *
         std::max(0.0f, rect.max.y - rect.min.y);
}

bool ViewportOverlay::selectionRectIsDrag(const SelectionRect &rect) {
  return (rect.max.x - rect.min.x) > 0.0f && (rect.max.y - rect.min.y) > 0.0f;
}

bool ViewportOverlay::selectionRectsIntersect(const SelectionRect &lhs,
                                              const SelectionRect &rhs) {
  return lhs.min.x <= rhs.max.x && lhs.max.x >= rhs.min.x &&
         lhs.min.y <= rhs.max.y && lhs.max.y >= rhs.min.y;
}

bool ViewportOverlay::appendSelectionMode(const bool ctrlHeld,
                                          const bool shiftHeld) {
  return ctrlHeld || shiftHeld;
}

CommandResult
ViewportOverlay::dispatchSelectionPaths(const std::vector<std::string> &paths) {
  if (paths.empty()) {
    return m_commandBus.dispatch("deselect");
  }

  std::ostringstream oss;
  oss << "select";
  for (const auto &path : paths) {
    oss << ' ' << quoteToken(path);
  }
  return m_commandBus.dispatch(oss.str());
}

std::vector<std::string>
ViewportOverlay::gatherBoxSelectionPaths(const Vec2f &dragStart, const Vec2f &dragEnd,
                                         const Vec2f &viewportSize) const {
  const auto editorCameraNode = m_editorState.getEditorCamera();
  if (!editorCameraNode) {
    return {};
  }
  const auto editorCamera = editorCameraNode->getComponent<CameraComponent>();
  if (!editorCamera.has_value()) {
    return {};
  }

  const SelectionRect selectionRect =
      makeSelectionRect(dragStart, dragEnd, viewportSize);
  if (!selectionRectIsDrag(selectionRect)) {
    return {};
  }

  const Mat4f viewProj =
      editorCamera->get().getProjMatrix() * editorCamera->get().getViewMatrix();
  std::vector<std::string> paths;
  for (const auto &renderable : m_scene.getRenderables()) {
    const auto node = std::dynamic_pointer_cast<SceneNode>(renderable);
    if (!node) {
      continue;
    }
    const BoundingBox bounds = node->getWorldBounds();
    if (!bounds.isValid()) {
      continue;
    }

    const auto projectedRect =
        projectBoundsToViewportRect(bounds, viewProj, viewportSize);
    if (!projectedRect.has_value()) {
      continue;
    }
    if (selectionRectsIntersect(selectionRect, *projectedRect)) {
      appendUniquePath(paths, node->getPath());
    }
  }
  return paths;
}

CommandResult ViewportOverlay::dispatchBoxSelection(const Vec2f &dragStart,
                                                    const Vec2f &dragEnd,
                                                    const Vec2f &viewportSize,
                                                    const bool ctrlHeld,
                                                    const bool shiftHeld) {
  const SelectionRect selectionRect =
      makeSelectionRect(dragStart, dragEnd, viewportSize);
  if (!selectionRectIsDrag(selectionRect)) {
    return CommandResult{false, "box selection requires drag area", {}};
  }

  std::vector<std::string> hits = gatherBoxSelectionPaths(dragStart, dragEnd, viewportSize);
  std::vector<std::string> paths;
  const bool appendMode = appendSelectionMode(ctrlHeld, shiftHeld);
  if (appendMode) {
    for (const auto &selected : m_editorState.getSelected()) {
      if (selected) {
        appendUniquePath(paths, selected->getPath());
      }
    }
  }
  for (const auto &path : hits) {
    appendUniquePath(paths, path);
  }

  const float viewportArea = std::max(1.0f, viewportSize.x * viewportSize.y);
  const float thresholdArea = viewportArea * m_config.boxSelectConfirmThreshold;
  const float selectionArea = selectionRectArea(selectionRect);
  if (selectionArea > thresholdArea) {
    m_pendingBoxSelection = PendingBoxSelection{paths, appendMode, paths.size()};
    m_boxSelectPopupRequested = true;
    return CommandResult{true, "box selection pending confirmation", {}};
  }

  return dispatchSelectionPaths(paths);
}

bool ViewportOverlay::hasPendingBoxSelectionConfirmation() const {
  return m_pendingBoxSelection.has_value();
}

CommandResult ViewportOverlay::resolvePendingBoxSelection(const bool confirm) {
  if (!m_pendingBoxSelection.has_value()) {
    return CommandResult{false, "no pending box selection confirmation", {}};
  }

  PendingBoxSelection pending = *m_pendingBoxSelection;
  m_pendingBoxSelection.reset();
  if (!confirm) {
    return CommandResult{true, "box selection cancelled", {}};
  }
  return dispatchSelectionPaths(pending.paths);
}

ViewportOverlay::PanelRect ViewportOverlay::getPanelRect() const {
  return m_lastPanelRect;
}

void ViewportOverlay::enqueueDebugDraw() const {
  if (!shouldRenderEditorOverlay()) {
    return;
  }

  const auto primarySelected = m_editorState.getPrimarySelected();
  for (const auto &selected : m_editorState.getSelected()) {
    if (!selected) {
      continue;
    }
    const BoundingBox bounds = selected->getWorldBounds();
    if (bounds.isValid()) {
      const bool primary = primarySelected.has_value() &&
                           &primarySelected->get() == selected.get();
      if (primary) {
        DebugDraw::wireBox(bounds, Vec4f{0.2f, 1.0f, 1.0f, 1.0f});
        DebugDraw::wireBox(expandedBounds(bounds, 0.02f),
                           Vec4f{0.2f, 1.0f, 1.0f, 1.0f});
      } else {
        DebugDraw::wireBox(bounds, DebugDraw::Color::yellow());
      }
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

void ViewportOverlay::drawBoxSelectionRect(ImDrawList &drawList,
                                           const SelectionRect &rect,
                                           const PanelRect &panelRect) const {
  const ImVec2 min{panelRect.origin.x + rect.min.x, panelRect.origin.y + rect.min.y};
  const ImVec2 max{panelRect.origin.x + rect.max.x, panelRect.origin.y + rect.max.y};
  drawList.AddRectFilled(min, max, IM_COL32(80, 180, 255, 48));
  drawList.AddRect(min, max, IM_COL32(120, 220, 255, 220), 0.0f, ImDrawFlags_None,
                   1.5f);
}

void ViewportOverlay::drawBoxSelectionConfirmModal() {
  if (m_boxSelectPopupRequested) {
    ImGui::OpenPopup("Confirm Large Box Select");
    m_boxSelectPopupRequested = false;
  }

  if (!m_pendingBoxSelection.has_value()) {
    return;
  }

  if (ImGui::BeginPopupModal("Confirm Large Box Select", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("框选了 %zu 个节点，确认全选?", m_pendingBoxSelection->hitCount);
    if (ImGui::Button("Confirm")) {
      (void)resolvePendingBoxSelection(true);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      (void)resolvePendingBoxSelection(false);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void ViewportOverlay::draw() {
  const bool transparentViewportBg =
      expEnvEnabled("LX_SCENE_VIEWER_VIEWPORT_NO_BG");
  if (transparentViewportBg) {
    ImGui::SetNextWindowBgAlpha(0.0f);
  }

  ImGuiWindowFlags viewportFlags = 0;
  if (transparentViewportBg) {
    viewportFlags |= ImGuiWindowFlags_NoBackground;
  }

  if (!ImGui::Begin("Viewport", nullptr, viewportFlags)) {
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
  if (!editorCameraNode) {
    drawList->PopClipRect();
    drawBoxSelectionConfirmModal();
    ImGui::End();
    return;
  }
  auto editorCamera = editorCameraNode->getComponent<CameraComponent>();
  if (!editorCamera.has_value()) {
    drawList->PopClipRect();
    drawBoxSelectionConfirmModal();
    ImGui::End();
    return;
  }

  ImGuizmo::SetDrawlist(drawList);
  ImGuizmo::SetOrthographic(editorCamera->get().type == CameraType::Orthographic);
  ImGuiIO &io = ImGui::GetIO();
  ImGuizmo::SetRect(rect.origin.x, rect.origin.y, rect.size.x, rect.size.y);

  float view[16] = {};
  float projection[16] = {};
  GizmoAdapter::toFloat16(editorCamera->get().getViewMatrix(), view);
  GizmoAdapter::toFloat16(editorCamera->get().getProjMatrix(), projection);

  bool changed = false;
  bool usingNow = false;
  if (selected.has_value()) {
    float objectMatrix[16] = {};
    GizmoAdapter::toFloat16(selected->get().getLocalTransform().toMat4(), objectMatrix);
    changed = ImGuizmo::Manipulate(
        view, projection, toImGuizmoOperation(m_gizmoOperation), ImGuizmo::LOCAL,
        objectMatrix, nullptr, nullptr, nullptr, nullptr);
    m_gizmoHovered = ImGuizmo::IsOver();
    usingNow = ImGuizmo::IsUsing();

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
      for (usize i = 0; i < selectedNodes.size() && i < m_gizmoPreDragTransforms.size();
           ++i) {
        if (!selectedNodes[i]) {
          continue;
        }
        selectedNodes[i]->setLocalTransform(applyTransformDelta(
            m_gizmoOperation, m_gizmoPreDragTransforms[i], primaryBefore,
            primaryCommitted));
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

      for (usize i = 0; i < m_gizmoDragPaths.size() &&
                       i < m_gizmoPreDragTransforms.size();
           ++i) {
        if (SceneNode *node = m_scene.findByPath(m_gizmoDragPaths[i])) {
          node->setLocalTransform(m_gizmoPreDragTransforms[i]);
        }
      }

      const CommandResult commit = dispatchGizmoSelectionCommit(
          m_gizmoDragPaths, m_gizmoPreDragTransforms, committedTransforms);
      if (!commit.ok) {
        for (usize i = 0; i < m_gizmoDragPaths.size() && i < committedTransforms.size();
             ++i) {
          if (SceneNode *node = m_scene.findByPath(m_gizmoDragPaths[i])) {
            node->setLocalTransform(committedTransforms[i]);
          }
        }
      }
      m_gizmoPreDragTransforms.clear();
      m_gizmoDragPaths.clear();
    }
  }
  m_gizmoUsing = usingNow;

  const bool mouseInsideViewport = io.MousePos.x >= rect.origin.x &&
                                   io.MousePos.x <= rect.origin.x + rect.size.x &&
                                   io.MousePos.y >= rect.origin.y &&
                                   io.MousePos.y <= rect.origin.y + rect.size.y;
  const Vec2f localMouse{io.MousePos.x - rect.origin.x, io.MousePos.y - rect.origin.y};
  if (!m_gizmoHovered && !m_gizmoUsing && io.MouseClicked[0] && viewportHovered &&
      mouseInsideViewport) {
    m_boxSelectTracking = true;
    m_boxSelectActive = false;
    m_boxSelectStart = localMouse;
    m_boxSelectRect = makeSelectionRect(localMouse, localMouse, rect.size);
    m_boxSelectCtrlHeld = io.KeyCtrl;
    m_boxSelectShiftHeld = io.KeyShift;
  }

  if (m_boxSelectTracking) {
    m_boxSelectRect = makeSelectionRect(m_boxSelectStart, localMouse, rect.size);
    const float width = m_boxSelectRect.max.x - m_boxSelectRect.min.x;
    const float height = m_boxSelectRect.max.y - m_boxSelectRect.min.y;
    if (width >= m_config.boxSelectDragThresholdPixels &&
        height >= m_config.boxSelectDragThresholdPixels) {
      m_boxSelectActive = true;
    }
  }

  if (m_boxSelectActive) {
    drawBoxSelectionRect(*drawList, m_boxSelectRect, rect);
  }

  if (m_boxSelectTracking && io.MouseReleased[0]) {
    if (m_boxSelectActive) {
      (void)dispatchBoxSelection(m_boxSelectStart, localMouse, rect.size,
                                 m_boxSelectCtrlHeld, m_boxSelectShiftHeld);
    } else if (mouseInsideViewport) {
      (void)dispatchPickingClick(localMouse, rect.size);
    }
    m_boxSelectTracking = false;
    m_boxSelectActive = false;
  }
  drawList->PopClipRect();
  drawBoxSelectionConfirmModal();
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
