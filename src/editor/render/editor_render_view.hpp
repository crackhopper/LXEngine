#pragma once

#include "core/math/vec.hpp"
#include "core/scene/scene.hpp"

#include <optional>
#include <string>

namespace LX_core {
class EditorState;
} // namespace LX_core

namespace LX_editor {

struct EditorRenderView final {
  std::string cameraPath;
  LX_core::CameraResource cameraResource;
  LX_core::VisibilityLayerMask visibleMask =
      LX_core::Layer_All & ~LX_core::Layer_EditorOverlay;
  LX_core::Vec2f viewportExtent{0.0f, 0.0f};
  bool previewEnabled = false;
  bool editorOverlayVisible = true;
};

[[nodiscard]] std::optional<EditorRenderView>
buildEditorRenderView(const LX_core::EditorState &editorState,
                      const LX_core::Scene &scene,
                      const LX_core::Vec2f &viewportExtent);

} // namespace LX_editor
