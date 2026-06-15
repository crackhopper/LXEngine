#pragma once

#include "core/math/vec.hpp"
#include "editor/runtime/editor_camera_state.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {

struct EditorSceneStateDocument final {
  std::optional<EditorCameraState> editorCamera;
  std::optional<LX_core::Vec3f> orbitTarget;
  std::vector<std::string> selectedPaths;
};

[[nodiscard]] std::filesystem::path
editorSceneStatePathForScenePath(const std::filesystem::path& scenePath);

[[nodiscard]] std::optional<EditorSceneStateDocument>
loadEditorSceneStateIfPresent(const std::filesystem::path& scenePath);

void saveEditorSceneStateForScenePath(
    const std::filesystem::path& scenePath,
    const EditorSceneStateDocument& document);

} // namespace LX_demo::lxe_editor
