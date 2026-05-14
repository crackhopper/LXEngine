#pragma once

#include "core/platform/window.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_demo::lxe_editor {

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

struct EditorWindowPlacementOverride final {
  std::optional<int> x;
  std::optional<int> y;
  std::optional<int> width;
  std::optional<int> height;
  std::optional<bool> maximized;
};

struct EditorWindowLayoutOverride final {
  std::string id;
  std::optional<bool> visible;
  std::optional<bool> collapsed;
  std::optional<int> x;
  std::optional<int> y;
  std::optional<int> width;
  std::optional<int> height;
};

struct EditorPreferencesOverride final {
  std::optional<float> uiFontScale;
};

struct EditorConfigOverrideDocument final {
  std::optional<EditorWindowPlacementOverride> windowPlacement;
  std::vector<EditorWindowLayoutOverride> layoutWindows;
  EditorPreferencesOverride preferences;
};

struct EditorDisplayProfile final {
  std::string key;
  std::string label;
  bool available = false;
  EditorConfigOverrideDocument overrides;
};

struct EditorDisplayConfigDocument final {
  int version = 2;
  std::string activeDisplay;
  EditorConfigDocument displayDefault;
  std::vector<EditorDisplayProfile> displayProfiles;
};

class EditorConfigState final {
public:
  explicit EditorConfigState(std::filesystem::path rootDir);

  [[nodiscard]] const std::filesystem::path &configPath() const;
  [[nodiscard]] EditorConfigDocument load() const;
  bool save(const EditorConfigDocument &document) const;
  [[nodiscard]] EditorDisplayConfigDocument loadOrCreateForDisplays(
      const std::vector<LX_core::DisplayInfo> &displays) const;
  bool saveDisplayDocument(const EditorDisplayConfigDocument &document,
                           std::string_view activeDisplayKey,
                           const EditorConfigDocument &effectiveConfig) const;
  [[nodiscard]] EditorConfigDocument
  composeEffectiveConfig(const EditorDisplayConfigDocument &document,
                         std::string_view displayKey) const;

private:
  std::filesystem::path m_rootDir;
  std::filesystem::path m_configPath;
};

[[nodiscard]] std::optional<std::reference_wrapper<EditorWindowLayout>>
findEditorWindowLayout(EditorConfigDocument &document, std::string_view id);

[[nodiscard]] std::optional<std::reference_wrapper<const EditorWindowLayout>>
findEditorWindowLayout(const EditorConfigDocument &document,
                       std::string_view id);

} // namespace LX_demo::lxe_editor
