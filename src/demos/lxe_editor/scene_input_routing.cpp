#include "demos/lxe_editor/scene_input_routing.hpp"

namespace LX_demo::lxe_editor {

bool shouldProcessSelectionMode(const bool previewEnabled, const bool wantsMouse,
                                const bool gizmoConsumesMouse,
                                const SceneInputEditMode mode) {
  return !previewEnabled && !wantsMouse && !gizmoConsumesMouse &&
         mode == SceneInputEditMode::Selection;
}

bool shouldProcessCameraRig(const bool previewEnabled, const bool wantsKeyboard,
                            const bool wantsMouse,
                            const bool gizmoConsumesMouse,
                            const SceneInputEditMode mode) {
  (void)mode;
  return !previewEnabled && !wantsKeyboard && !wantsMouse &&
         !gizmoConsumesMouse;
}

} // namespace LX_demo::lxe_editor
