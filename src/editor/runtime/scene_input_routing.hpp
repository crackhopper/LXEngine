#pragma once

namespace LX_demo::lxe_editor {

enum class SceneInputEditMode { Selection };

[[nodiscard]] bool shouldProcessSelectionMode(bool previewEnabled,
                                              bool wantsMouse,
                                              bool gizmoConsumesMouse,
                                              SceneInputEditMode mode);

[[nodiscard]] bool shouldProcessCameraRig(bool previewEnabled,
                                          bool wantsKeyboard,
                                          bool wantsMouse,
                                          bool gizmoConsumesMouse);

} // namespace LX_demo::lxe_editor
