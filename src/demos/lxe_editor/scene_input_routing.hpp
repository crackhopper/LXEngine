#pragma once

namespace LX_demo::lxe_editor {

enum class SceneInputEditMode { Selection, Orbit, FreeFly };

[[nodiscard]] bool shouldProcessSelectionMode(bool previewEnabled,
                                              bool wantsMouse,
                                              SceneInputEditMode mode);

[[nodiscard]] bool shouldProcessCameraRig(bool previewEnabled,
                                          bool wantsKeyboard,
                                          bool wantsMouse,
                                          SceneInputEditMode mode);

} // namespace LX_demo::lxe_editor
