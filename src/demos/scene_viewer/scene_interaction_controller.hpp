#pragma once

#include "core/editor/command_bus.hpp"
#include "core/input/input_state.hpp"
#include "core/scene/scene.hpp"

namespace LX_core {
class EditorState;
}

namespace LX_demo::scene_viewer {

class SceneInteractionController final {
public:
  SceneInteractionController(LX_core::CommandBus& commandBus,
                             LX_core::EditorState& editorState,
                             LX_core::Scene& scene);

  [[nodiscard]] LX_core::CommandResult dispatchPickingClick(
      const LX_core::Vec2f& screenPixel, const LX_core::Vec2f& viewportSize);
  void updateSelectionMode(LX_core::IInputState& input,
                           const LX_core::Vec2f& viewportSize);

private:
  LX_core::CommandBus& m_commandBus;
  LX_core::EditorState& m_editorState;
  LX_core::Scene& m_scene;
  bool m_prevLeftDown = false;
};

} // namespace LX_demo::scene_viewer
