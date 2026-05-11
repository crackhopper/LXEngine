#include "demos/scene_viewer/scene_input_routing.hpp"

namespace LX_demo::scene_viewer {

bool shouldProcessSelectionMode(const bool previewEnabled, const bool wantsMouse,
                                const SceneInputEditMode mode) {
  return !previewEnabled && !wantsMouse &&
         mode == SceneInputEditMode::Selection;
}

bool shouldProcessCameraRig(const bool previewEnabled, const bool wantsKeyboard,
                            const bool wantsMouse,
                            const SceneInputEditMode mode) {
  return !previewEnabled && !wantsKeyboard && !wantsMouse &&
         mode != SceneInputEditMode::Selection;
}

} // namespace LX_demo::scene_viewer
