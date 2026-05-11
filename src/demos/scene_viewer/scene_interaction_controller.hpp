#pragma once

#include "core/editor/command_bus.hpp"
#include "core/math/vec.hpp"
#include "core/input/input_state.hpp"
#include "core/scene/scene.hpp"

#include <optional>

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
  void enqueueDebugDraw() const;
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
  std::optional<HitMarker> m_lastHitMarker;
};

} // namespace LX_demo::scene_viewer
