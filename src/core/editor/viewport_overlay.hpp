#pragma once

#include "core/editor/command_bus.hpp"
#include "core/editor/gizmo_adapter.hpp"
#include "core/math/vec.hpp"
#include "core/math/transform.hpp"

#include <imgui.h>
#include <ImGuizmo.h>

#include <optional>
#include <string>
#include <vector>

struct ImDrawList;

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

  struct Snapshot {
    bool previewEnabled = false;
    std::string activeCameraPath;
    std::string editorCameraPath;
    std::string previewCameraPath;
    std::string selectedPath;
    std::string hintText;
    GizmoOperation gizmoOperation = GizmoOperation::Translate;
  };

  ViewportOverlay(CommandBus &commandBus, EditorState &editorState, Scene &scene);

  [[nodiscard]] Snapshot makeSnapshot() const;
  [[nodiscard]] GizmoOperation getGizmoOperation() const;
  void setGizmoOperation(GizmoOperation operation);
  bool handleGizmoHotkeys(int imguiKeyOrChar);
  [[nodiscard]] bool shouldRenderEditorOverlay() const;
  [[nodiscard]] CommandResult dispatchPreviewToggle();
  [[nodiscard]] CommandResult dispatchGizmoCommit(std::string_view path,
                                                  const GizmoTransformComponents &components);
  [[nodiscard]] CommandResult
  dispatchGizmoSelectionCommit(const std::vector<std::string> &paths,
                               const std::vector<Transform> &beforeTransforms,
                               const std::vector<Transform> &afterTransforms);
  [[nodiscard]] CommandResult dispatchPickingClick(const Vec2f &screenPixel,
                                                   const Vec2f &viewportSize);
  [[nodiscard]] PanelRect getPanelRect() const;
  void enqueueDebugDraw() const;
  void draw();

private:
  [[nodiscard]] PanelRect computeViewportRect() const;
  [[nodiscard]] static const char *modeLabel(GizmoOperation operation);
  [[nodiscard]] static ImGuizmo::OPERATION toImGuizmoOperation(GizmoOperation operation);

  CommandBus &m_commandBus;
  EditorState &m_editorState;
  Scene &m_scene;
  GizmoOperation m_gizmoOperation = GizmoOperation::Translate;
  PanelRect m_lastPanelRect{};
  bool m_gizmoHovered = false;
  bool m_gizmoUsing = false;
  std::vector<std::string> m_gizmoDragPaths;
  std::vector<Transform> m_gizmoPreDragTransforms;
};

} // namespace LX_core
