#pragma once

#include "editor/commands/command_bus.hpp"
#include "core/math/vec.hpp"
#include "core/input/input_state.hpp"
#include "core/scene/scene.hpp"

#include "scene_view_rect.hpp"

#include <optional>
#include <functional>

namespace LX_core {
class EditorState;
}

namespace LX_demo::lxe_editor {

class SceneInteractionController final {
public:
  using DebugEnabledFn = std::function<bool()>;
  using AppendDebugLineFn = std::function<void(std::string_view)>;
  using ResolveHelperOwnerFn =
      std::function<LX_core::SceneNodeSharedPtr(const std::string&)>;
  using BoxSelectionDispatchFn =
      std::function<LX_core::CommandResult(const LX_core::Vec2f& dragStart,
                                           const LX_core::Vec2f& dragEnd,
                                           const SceneViewRect& sceneViewRect,
                                           bool ctrlHeld,
                                           bool shiftHeld)>;

  SceneInteractionController(LX_core::CommandBus& commandBus,
                             LX_core::EditorState& editorState,
                             LX_core::Scene& scene);
  void setDebugLoggingHooks(DebugEnabledFn debugEnabled,
                            AppendDebugLineFn appendDebugLine);
  void setResolveHelperOwner(ResolveHelperOwnerFn resolveHelperOwner);
  void setBoxSelectionDispatch(BoxSelectionDispatchFn dispatchBoxSelection);

  [[nodiscard]] LX_core::CommandResult dispatchPickingClick(
      const LX_core::Vec2f& screenPixel, const LX_core::Vec2f& viewportSize);
  [[nodiscard]] LX_core::CommandResult dispatchPickingClick(
      const LX_core::Vec2f& screenPixel, const SceneViewRect& sceneViewRect);
  void updateSelectionMode(LX_core::IInputState& input,
                           const LX_core::Vec2f& viewportSize);
  void updateSelectionMode(LX_core::IInputState& input,
                           const SceneViewRect& sceneViewRect);
  void cancelPendingSelectionClick(const LX_core::IInputState& input);
  void enqueueDebugDraw(bool suppressEditorHelpers = false) const;
  [[nodiscard]] std::optional<LX_core::Vec3f> lastHitPoint() const;

private:
  struct HitMarker final {
    std::weak_ptr<LX_core::SceneNode> node;
    LX_core::Vec3f point{0.0f, 0.0f, 0.0f};
  };

  LX_core::CommandBus& m_commandBus;
  LX_core::EditorState& m_editorState;
  LX_core::Scene& m_scene;
  bool m_prevLeftDown = false;
  bool m_leftPressArmed = false;
  LX_core::Vec2f m_leftPressStart{0.0f, 0.0f};
  std::optional<HitMarker> m_lastHitMarker;
  DebugEnabledFn m_debugEnabled;
  AppendDebugLineFn m_appendDebugLine;
  ResolveHelperOwnerFn m_resolveHelperOwner;
  BoxSelectionDispatchFn m_dispatchBoxSelection;
};

} // namespace LX_demo::lxe_editor
