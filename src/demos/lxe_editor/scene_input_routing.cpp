#include "demos/lxe_editor/scene_input_routing.hpp"

namespace LX_demo::lxe_editor {

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

} // namespace LX_demo::lxe_editor
