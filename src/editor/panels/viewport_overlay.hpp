#pragma once

#include "editor/commands/command_bus.hpp"
#include "editor/app/editor_config.hpp"
#include "editor/ui/gizmo_adapter.hpp"
#include "core/math/transform.hpp"
#include "core/math/vec.hpp"
#include "editor/runtime/scene_view_rect.hpp"

#include <imgui.h>
// ImGuizmo declares APIs with ImGui types and must be included after imgui.h.
// clang-format off
#include <ImGuizmo.h>
// clang-format on

#include <optional>
#include <string>
#include <vector>

namespace LX_core {

class EditorState;
class Scene;

class ViewportOverlay final {
public:
  enum class GizmoOperation { Translate, Rotate, Scale };
  struct PanelRect final {
    Vec2f origin{0.0f, 0.0f};
    Vec2f size{1.0f, 1.0f};
  };
  struct SelectionRect final {
    Vec2f min{0.0f, 0.0f};
    Vec2f max{0.0f, 0.0f};
  };

  struct Snapshot {
    bool previewEnabled = false;
    std::string activeCameraPath;
    std::string editorCameraPath;
    std::string previewCameraPath;
    std::string selectedPath;
    std::string hintText;
    GizmoOperation gizmoOperation = GizmoOperation::Translate;
  };

  ViewportOverlay(CommandBus &commandBus, EditorState &editorState,
                  Scene &scene, EditorConfig config = {});

  [[nodiscard]] Snapshot makeSnapshot() const;
  [[nodiscard]] GizmoOperation getGizmoOperation() const;
  void setGizmoOperation(GizmoOperation operation);
  bool handleGizmoHotkeys(int imguiKeyOrChar);
  [[nodiscard]] bool shouldRenderEditorOverlay() const;
  [[nodiscard]] CommandResult dispatchPreviewToggle();
  [[nodiscard]] CommandResult
  dispatchGizmoCommit(std::string_view path,
                      const GizmoTransformComponents &components);
  [[nodiscard]] CommandResult
  dispatchGizmoSelectionCommit(const std::vector<std::string> &paths,
                               const std::vector<Transform> &beforeTransforms,
                               const std::vector<Transform> &afterTransforms);
  [[nodiscard]] CommandResult dispatchPickingClick(const Vec2f &screenPixel,
                                                   const Vec2f &viewportSize);
  [[nodiscard]] CommandResult dispatchBoxSelection(const Vec2f &dragStart,
                                                   const Vec2f &dragEnd,
                                                   const Vec2f &viewportSize,
                                                   bool ctrlHeld,
                                                   bool shiftHeld);
  [[nodiscard]] std::vector<std::string>
  gatherBoxSelectionPaths(const Vec2f &dragStart, const Vec2f &dragEnd,
                          const Vec2f &viewportSize) const;
  [[nodiscard]] bool hasPendingBoxSelectionConfirmation() const;
  [[nodiscard]] CommandResult resolvePendingBoxSelection(bool confirm);
  [[nodiscard]] PanelRect getPanelRect() const;
  [[nodiscard]] bool isGizmoCapturingMouse() const;
  void enqueueDebugDraw() const;
  void drawSceneOverlay(const LX_demo::lxe_editor::SceneViewRect &sceneRect);

private:
  struct PendingBoxSelection final {
    std::vector<std::string> paths;
    bool appendMode = false;
    usize hitCount = 0;
  };

  [[nodiscard]] static SelectionRect
  makeSelectionRect(const Vec2f &a, const Vec2f &b, const Vec2f &viewportSize);
  [[nodiscard]] static float selectionRectArea(const SelectionRect &rect);
  [[nodiscard]] static bool selectionRectIsDrag(const SelectionRect &rect);
  [[nodiscard]] static bool selectionRectsIntersect(const SelectionRect &lhs,
                                                    const SelectionRect &rhs);
  [[nodiscard]] static bool appendSelectionMode(bool ctrlHeld, bool shiftHeld);
  [[nodiscard]] CommandResult
  dispatchSelectionPaths(const std::vector<std::string> &paths);
  void clearGizmoInteractionState();
  void drawBoxSelectionConfirmModal();
  [[nodiscard]] PanelRect computeViewportRect() const;
  [[nodiscard]] static ImGuizmo::OPERATION
  toImGuizmoOperation(GizmoOperation operation);

  CommandBus &m_commandBus;
  EditorState &m_editorState;
  Scene &m_scene;
  EditorConfig m_config;
  GizmoOperation m_gizmoOperation = GizmoOperation::Translate;
  PanelRect m_lastPanelRect{};
  bool m_gizmoHovered = false;
  bool m_gizmoUsing = false;
  bool m_boxSelectPopupRequested = false;
  std::optional<PendingBoxSelection> m_pendingBoxSelection;
  std::vector<std::string> m_gizmoDragPaths;
  std::vector<Transform> m_gizmoPreDragTransforms;
};

} // namespace LX_core
