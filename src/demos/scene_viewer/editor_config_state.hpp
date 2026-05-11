#pragma once

#include "core/platform/window.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_demo::scene_viewer {

struct EditorWindowLayout final {
  std::string id;
  bool visible = true;
  bool collapsed = false;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct EditorPreferences final {
  float uiFontScale = 1.0f;
};

struct EditorConfigDocument final {
  int version = 1;
  std::optional<LX_core::WindowPlacement> windowPlacement;
  std::vector<EditorWindowLayout> layoutWindows;
  EditorPreferences preferences;
};

class EditorConfigState final {
public:
  explicit EditorConfigState(std::filesystem::path rootDir);

  [[nodiscard]] const std::filesystem::path& configPath() const;
  [[nodiscard]] EditorConfigDocument load() const;
  bool save(const EditorConfigDocument& document) const;

private:
  std::filesystem::path m_rootDir;
  std::filesystem::path m_configPath;
};

[[nodiscard]] std::optional<std::reference_wrapper<EditorWindowLayout>>
findEditorWindowLayout(EditorConfigDocument& document, std::string_view id);

[[nodiscard]] std::optional<std::reference_wrapper<const EditorWindowLayout>>
findEditorWindowLayout(const EditorConfigDocument& document, std::string_view id);

} // namespace LX_demo::scene_viewer
