#include "demos/lxe_editor/editor_config_state.hpp"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace LX_demo::lxe_editor {
namespace {

constexpr int kEditorConfigVersion = 1;
constexpr int kEditorDisplayConfigVersion = 2;
constexpr float kMinUiFontScale = 0.75f;
constexpr float kMaxUiFontScale = 2.0f;

[[nodiscard]] float clampUiFontScale(const float value) {
  return std::clamp(value, kMinUiFontScale, kMaxUiFontScale);
}

[[nodiscard]] std::optional<LX_core::WindowPlacement>
loadWindowPlacement(const YAML::Node &node) {
  if (!node || !node.IsMap()) {
    return std::nullopt;
  }

  LX_core::WindowPlacement placement{};
  placement.x = node["x"] ? node["x"].as<int>() : 0;
  placement.y = node["y"] ? node["y"].as<int>() : 0;
  placement.width = node["width"] ? node["width"].as<int>() : 0;
  placement.height = node["height"] ? node["height"].as<int>() : 0;
  placement.maximized =
      node["maximized"] ? node["maximized"].as<bool>() : false;
  if (placement.width <= 0 || placement.height <= 0) {
    return std::nullopt;
  }
  return placement;
}

[[nodiscard]] std::optional<EditorWindowPlacementOverride>
loadWindowPlacementOverride(const YAML::Node &node) {
  if (!node || !node.IsMap()) {
    return std::nullopt;
  }

  EditorWindowPlacementOverride placement;
  if (const auto xNode = node["x"]; xNode) {
    placement.x = xNode.as<int>();
  }
  if (const auto yNode = node["y"]; yNode) {
    placement.y = yNode.as<int>();
  }
  if (const auto widthNode = node["width"]; widthNode) {
    placement.width = widthNode.as<int>();
  }
  if (const auto heightNode = node["height"]; heightNode) {
    placement.height = heightNode.as<int>();
  }
  if (const auto maximizedNode = node["maximized"]; maximizedNode) {
    placement.maximized = maximizedNode.as<bool>();
  }
  if (!placement.x.has_value() && !placement.y.has_value() &&
      !placement.width.has_value() && !placement.height.has_value() &&
      !placement.maximized.has_value()) {
    return std::nullopt;
  }
  return placement;
}

void saveWindowPlacement(YAML::Emitter &out,
                         const LX_core::WindowPlacement &placement) {
  out << YAML::Key << "window" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "x" << YAML::Value << placement.x;
  out << YAML::Key << "y" << YAML::Value << placement.y;
  out << YAML::Key << "width" << YAML::Value << placement.width;
  out << YAML::Key << "height" << YAML::Value << placement.height;
  out << YAML::Key << "maximized" << YAML::Value << placement.maximized;
  out << YAML::EndMap;
}

void saveWindowPlacementOverride(
    YAML::Emitter &out, const EditorWindowPlacementOverride &placement) {
  out << YAML::Key << "window" << YAML::Value << YAML::BeginMap;
  if (placement.x.has_value()) {
    out << YAML::Key << "x" << YAML::Value << *placement.x;
  }
  if (placement.y.has_value()) {
    out << YAML::Key << "y" << YAML::Value << *placement.y;
  }
  if (placement.width.has_value()) {
    out << YAML::Key << "width" << YAML::Value << *placement.width;
  }
  if (placement.height.has_value()) {
    out << YAML::Key << "height" << YAML::Value << *placement.height;
  }
  if (placement.maximized.has_value()) {
    out << YAML::Key << "maximized" << YAML::Value << *placement.maximized;
  }
  out << YAML::EndMap;
}

[[nodiscard]] std::optional<EditorWindowLayout>
loadLayoutWindow(const YAML::Node &node) {
  if (!node || !node.IsMap() || !node["id"]) {
    return std::nullopt;
  }

  EditorWindowLayout layout;
  layout.id = node["id"].as<std::string>();
  if (layout.id.empty()) {
    return std::nullopt;
  }
  layout.visible = node["visible"] ? node["visible"].as<bool>() : true;
  layout.collapsed = node["collapsed"] ? node["collapsed"].as<bool>() : false;
  layout.x = node["x"] ? node["x"].as<int>() : 0;
  layout.y = node["y"] ? node["y"].as<int>() : 0;
  layout.width = node["width"] ? node["width"].as<int>() : 0;
  layout.height = node["height"] ? node["height"].as<int>() : 0;
  return layout;
}

[[nodiscard]] std::optional<EditorWindowLayoutOverride>
loadLayoutWindowOverride(const YAML::Node &node) {
  if (!node || !node.IsMap() || !node["id"]) {
    return std::nullopt;
  }

  EditorWindowLayoutOverride layout;
  layout.id = node["id"].as<std::string>();
  if (layout.id.empty()) {
    return std::nullopt;
  }
  if (const auto visibleNode = node["visible"]; visibleNode) {
    layout.visible = visibleNode.as<bool>();
  }
  if (const auto collapsedNode = node["collapsed"]; collapsedNode) {
    layout.collapsed = collapsedNode.as<bool>();
  }
  if (const auto xNode = node["x"]; xNode) {
    layout.x = xNode.as<int>();
  }
  if (const auto yNode = node["y"]; yNode) {
    layout.y = yNode.as<int>();
  }
  if (const auto widthNode = node["width"]; widthNode) {
    layout.width = widthNode.as<int>();
  }
  if (const auto heightNode = node["height"]; heightNode) {
    layout.height = heightNode.as<int>();
  }
  if (!layout.visible.has_value() && !layout.collapsed.has_value() &&
      !layout.x.has_value() && !layout.y.has_value() &&
      !layout.width.has_value() && !layout.height.has_value()) {
    return std::nullopt;
  }
  return layout;
}

void saveLayoutWindows(YAML::Emitter &out,
                       const std::vector<EditorWindowLayout> &windows) {
  out << YAML::Key << "layout" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "windows" << YAML::Value << YAML::BeginSeq;
  for (const auto &window : windows) {
    out << YAML::BeginMap;
    out << YAML::Key << "id" << YAML::Value << window.id;
    out << YAML::Key << "visible" << YAML::Value << window.visible;
    out << YAML::Key << "collapsed" << YAML::Value << window.collapsed;
    out << YAML::Key << "x" << YAML::Value << window.x;
    out << YAML::Key << "y" << YAML::Value << window.y;
    out << YAML::Key << "width" << YAML::Value << window.width;
    out << YAML::Key << "height" << YAML::Value << window.height;
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;
  out << YAML::EndMap;
}

void saveLayoutWindowsOverride(
    YAML::Emitter &out,
    const std::vector<EditorWindowLayoutOverride> &windows) {
  out << YAML::Key << "layout" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "windows" << YAML::Value << YAML::BeginSeq;
  for (const auto &window : windows) {
    out << YAML::BeginMap;
    out << YAML::Key << "id" << YAML::Value << window.id;
    if (window.visible.has_value()) {
      out << YAML::Key << "visible" << YAML::Value << *window.visible;
    }
    if (window.collapsed.has_value()) {
      out << YAML::Key << "collapsed" << YAML::Value << *window.collapsed;
    }
    if (window.x.has_value()) {
      out << YAML::Key << "x" << YAML::Value << *window.x;
    }
    if (window.y.has_value()) {
      out << YAML::Key << "y" << YAML::Value << *window.y;
    }
    if (window.width.has_value()) {
      out << YAML::Key << "width" << YAML::Value << *window.width;
    }
    if (window.height.has_value()) {
      out << YAML::Key << "height" << YAML::Value << *window.height;
    }
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;
  out << YAML::EndMap;
}

void sortUniqueWindows(std::vector<EditorWindowLayout> &windows) {
  std::stable_sort(
      windows.begin(), windows.end(),
      [](const EditorWindowLayout &lhs, const EditorWindowLayout &rhs) {
        return lhs.id < rhs.id;
      });
  windows.erase(std::unique(windows.begin(), windows.end(),
                            [](const EditorWindowLayout &lhs,
                               const EditorWindowLayout &rhs) {
                              return lhs.id == rhs.id;
                            }),
                windows.end());
}

void sortUniqueWindowOverrides(
    std::vector<EditorWindowLayoutOverride> &windows) {
  std::stable_sort(
      windows.begin(), windows.end(),
      [](const EditorWindowLayoutOverride &lhs,
         const EditorWindowLayoutOverride &rhs) { return lhs.id < rhs.id; });
  windows.erase(std::unique(windows.begin(), windows.end(),
                            [](const EditorWindowLayoutOverride &lhs,
                               const EditorWindowLayoutOverride &rhs) {
                              return lhs.id == rhs.id;
                            }),
                windows.end());
}

[[nodiscard]] EditorConfigDocument loadConfigDocument(const YAML::Node &root) {
  EditorConfigDocument document;
  if (const auto versionNode = root["version"]; versionNode) {
    document.version = versionNode.as<int>();
  }

  document.windowPlacement = loadWindowPlacement(root["window"]);

  if (const auto layoutNode = root["layout"];
      layoutNode && layoutNode.IsMap()) {
    if (const auto windowsNode = layoutNode["windows"];
        windowsNode && windowsNode.IsSequence()) {
      for (const auto &windowNode : windowsNode) {
        const auto window = loadLayoutWindow(windowNode);
        if (window.has_value()) {
          document.layoutWindows.push_back(*window);
        }
      }
      sortUniqueWindows(document.layoutWindows);
    }
  }

  if (const auto preferencesNode = root["preferences"];
      preferencesNode && preferencesNode.IsMap()) {
    if (const auto fontScaleNode = preferencesNode["uiFontScale"];
        fontScaleNode) {
      document.preferences.uiFontScale =
          clampUiFontScale(fontScaleNode.as<float>());
    }
  }

  return document;
}

void saveConfigDocument(YAML::Emitter &out,
                        const EditorConfigDocument &document,
                        bool includeVersion) {
  out << YAML::BeginMap;
  if (includeVersion) {
    out << YAML::Key << "version" << YAML::Value << document.version;
  }
  if (document.windowPlacement.has_value()) {
    saveWindowPlacement(out, *document.windowPlacement);
  }
  saveLayoutWindows(out, document.layoutWindows);
  out << YAML::Key << "preferences" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "uiFontScale" << YAML::Value
      << document.preferences.uiFontScale;
  out << YAML::EndMap;
  out << YAML::EndMap;
}

[[nodiscard]] EditorConfigOverrideDocument
loadConfigOverrideDocument(const YAML::Node &root) {
  EditorConfigOverrideDocument document;
  document.windowPlacement = loadWindowPlacementOverride(root["window"]);

  if (const auto layoutNode = root["layout"];
      layoutNode && layoutNode.IsMap()) {
    if (const auto windowsNode = layoutNode["windows"];
        windowsNode && windowsNode.IsSequence()) {
      for (const auto &windowNode : windowsNode) {
        const auto window = loadLayoutWindowOverride(windowNode);
        if (window.has_value()) {
          document.layoutWindows.push_back(*window);
        }
      }
      sortUniqueWindowOverrides(document.layoutWindows);
    }
  }

  if (const auto preferencesNode = root["preferences"];
      preferencesNode && preferencesNode.IsMap()) {
    if (const auto fontScaleNode = preferencesNode["uiFontScale"];
        fontScaleNode) {
      document.preferences.uiFontScale =
          clampUiFontScale(fontScaleNode.as<float>());
    }
  }

  return document;
}

void saveConfigOverrideDocument(YAML::Emitter &out,
                                const EditorConfigOverrideDocument &document) {
  out << YAML::BeginMap;
  if (document.windowPlacement.has_value()) {
    saveWindowPlacementOverride(out, *document.windowPlacement);
  }
  if (!document.layoutWindows.empty()) {
    saveLayoutWindowsOverride(out, document.layoutWindows);
  }
  if (document.preferences.uiFontScale.has_value()) {
    out << YAML::Key << "preferences" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "uiFontScale" << YAML::Value
        << *document.preferences.uiFontScale;
    out << YAML::EndMap;
  }
  out << YAML::EndMap;
}

[[nodiscard]] bool
isCurrentDisplay(const std::vector<LX_core::DisplayInfo> &displays,
                 std::string_view key) {
  return std::any_of(displays.begin(), displays.end(),
                     [key](const LX_core::DisplayInfo &display) {
                       return display.key == key;
                     });
}

void normalizeDisplayProfiles(EditorDisplayConfigDocument &document);

void syncDisplayProfiles(EditorDisplayConfigDocument &document,
                         const std::vector<LX_core::DisplayInfo> &displays) {
  normalizeDisplayProfiles(document);

  for (auto &profile : document.displayProfiles) {
    profile.available = false;
  }

  for (const auto &display : displays) {
    const auto it = std::find_if(
        document.displayProfiles.begin(), document.displayProfiles.end(),
        [&display](const EditorDisplayProfile &profile) {
          return profile.key == display.key;
        });
    if (it == document.displayProfiles.end()) {
      document.displayProfiles.push_back(EditorDisplayProfile{
          .key = display.key,
          .label = display.label,
          .available = true,
          .overrides = EditorConfigOverrideDocument{},
      });
    } else {
      it->label = display.label;
      it->available = true;
    }
  }

  if (!isCurrentDisplay(displays, document.activeDisplay)) {
    document.activeDisplay =
        displays.empty() ? std::string{} : displays.front().key;
  }
}

[[nodiscard]] EditorDisplayConfigDocument
loadDisplayConfigDocument(const YAML::Node &root) {
  EditorDisplayConfigDocument document;
  if (const auto versionNode = root["version"]; versionNode) {
    document.version = versionNode.as<int>();
  }
  if (const auto activeNode = root["activeDisplay"]; activeNode) {
    document.activeDisplay = activeNode.as<std::string>();
  }
  if (const auto defaultNode = root["displayDefault"];
      defaultNode && defaultNode.IsMap()) {
    document.displayDefault = loadConfigDocument(defaultNode);
  }
  document.displayDefault.version = kEditorConfigVersion;

  if (const auto profilesNode = root["displayProfiles"];
      profilesNode && profilesNode.IsSequence()) {
    for (const auto &profileNode : profilesNode) {
      if (!profileNode || !profileNode.IsMap() || !profileNode["key"]) {
        continue;
      }
      EditorDisplayProfile profile;
      profile.key = profileNode["key"].as<std::string>();
      if (profile.key.empty()) {
        continue;
      }
      profile.label = profileNode["label"]
                          ? profileNode["label"].as<std::string>()
                          : profile.key;
      profile.available = profileNode["available"]
                              ? profileNode["available"].as<bool>()
                              : false;
      if (const auto overridesNode = profileNode["overrides"];
          overridesNode && overridesNode.IsMap()) {
        profile.overrides = loadConfigOverrideDocument(overridesNode);
      }
      document.displayProfiles.push_back(std::move(profile));
    }
  }
  return document;
}

[[nodiscard]] bool
hasWindowOverrideFields(const EditorWindowPlacementOverride &placement) {
  return placement.x.has_value() || placement.y.has_value() ||
         placement.width.has_value() || placement.height.has_value() ||
         placement.maximized.has_value();
}

[[nodiscard]] bool
hasLayoutOverrideFields(const EditorWindowLayoutOverride &layout) {
  return layout.visible.has_value() || layout.collapsed.has_value() ||
         layout.x.has_value() || layout.y.has_value() ||
         layout.width.has_value() || layout.height.has_value();
}

[[nodiscard]] bool
hasConfigOverrides(const EditorConfigOverrideDocument &document) {
  return (document.windowPlacement.has_value() &&
          hasWindowOverrideFields(*document.windowPlacement)) ||
         !document.layoutWindows.empty() ||
         document.preferences.uiFontScale.has_value();
}

void normalizeConfigOverrides(EditorConfigOverrideDocument &document) {
  if (document.windowPlacement.has_value() &&
      !hasWindowOverrideFields(*document.windowPlacement)) {
    document.windowPlacement = std::nullopt;
  }
  document.layoutWindows.erase(
      std::remove_if(document.layoutWindows.begin(),
                     document.layoutWindows.end(),
                     [](const EditorWindowLayoutOverride &layout) {
                       return !hasLayoutOverrideFields(layout);
                     }),
      document.layoutWindows.end());
  sortUniqueWindowOverrides(document.layoutWindows);
}

void normalizeDisplayProfiles(EditorDisplayConfigDocument &document) {
  std::vector<EditorDisplayProfile> normalized;
  normalized.reserve(document.displayProfiles.size());
  for (auto &profile : document.displayProfiles) {
    if (profile.key.empty()) {
      continue;
    }
    normalizeConfigOverrides(profile.overrides);
    const auto duplicate =
        std::find_if(normalized.begin(), normalized.end(),
                     [&profile](const EditorDisplayProfile &existing) {
                       return existing.key == profile.key;
                     });
    if (duplicate == normalized.end()) {
      normalized.push_back(std::move(profile));
    }
  }
  document.displayProfiles = std::move(normalized);
}

void mergeWindowPlacement(
    EditorConfigDocument &effective,
    const EditorWindowPlacementOverride &overridePlacement) {
  LX_core::WindowPlacement placement =
      effective.windowPlacement.value_or(LX_core::WindowPlacement{});
  if (overridePlacement.x.has_value()) {
    placement.x = *overridePlacement.x;
  }
  if (overridePlacement.y.has_value()) {
    placement.y = *overridePlacement.y;
  }
  if (overridePlacement.width.has_value()) {
    placement.width = *overridePlacement.width;
  }
  if (overridePlacement.height.has_value()) {
    placement.height = *overridePlacement.height;
  }
  if (overridePlacement.maximized.has_value()) {
    placement.maximized = *overridePlacement.maximized;
  }
  effective.windowPlacement = placement;
}

void mergeLayoutOverride(EditorConfigDocument &effective,
                         const EditorWindowLayoutOverride &overrideLayout) {
  auto existing = findEditorWindowLayout(effective, overrideLayout.id);
  if (!existing.has_value()) {
    effective.layoutWindows.push_back(EditorWindowLayout{
        .id = overrideLayout.id,
        .visible = overrideLayout.visible.value_or(true),
        .collapsed = overrideLayout.collapsed.value_or(false),
        .x = overrideLayout.x.value_or(0),
        .y = overrideLayout.y.value_or(0),
        .width = overrideLayout.width.value_or(0),
        .height = overrideLayout.height.value_or(0),
    });
    return;
  }

  EditorWindowLayout &layout = existing->get();
  if (overrideLayout.visible.has_value()) {
    layout.visible = *overrideLayout.visible;
  }
  if (overrideLayout.collapsed.has_value()) {
    layout.collapsed = *overrideLayout.collapsed;
  }
  if (overrideLayout.x.has_value()) {
    layout.x = *overrideLayout.x;
  }
  if (overrideLayout.y.has_value()) {
    layout.y = *overrideLayout.y;
  }
  if (overrideLayout.width.has_value()) {
    layout.width = *overrideLayout.width;
  }
  if (overrideLayout.height.has_value()) {
    layout.height = *overrideLayout.height;
  }
}

[[nodiscard]] EditorConfigOverrideDocument
diffEffectiveConfig(const EditorConfigDocument &displayDefault,
                    const EditorConfigDocument &effectiveConfig) {
  EditorConfigOverrideDocument overrides;

  if (effectiveConfig.windowPlacement.has_value()) {
    EditorWindowPlacementOverride placementOverride;
    const auto &effective = *effectiveConfig.windowPlacement;
    const auto defaultPlacement = displayDefault.windowPlacement;
    if (!defaultPlacement.has_value() || effective.x != defaultPlacement->x) {
      placementOverride.x = effective.x;
    }
    if (!defaultPlacement.has_value() || effective.y != defaultPlacement->y) {
      placementOverride.y = effective.y;
    }
    if (!defaultPlacement.has_value() ||
        effective.width != defaultPlacement->width) {
      placementOverride.width = effective.width;
    }
    if (!defaultPlacement.has_value() ||
        effective.height != defaultPlacement->height) {
      placementOverride.height = effective.height;
    }
    if (!defaultPlacement.has_value() ||
        effective.maximized != defaultPlacement->maximized) {
      placementOverride.maximized = effective.maximized;
    }
    if (hasWindowOverrideFields(placementOverride)) {
      overrides.windowPlacement = placementOverride;
    }
  }

  for (const auto &effectiveLayout : effectiveConfig.layoutWindows) {
    EditorWindowLayoutOverride layoutOverride;
    layoutOverride.id = effectiveLayout.id;
    const auto defaultLayout =
        findEditorWindowLayout(displayDefault, effectiveLayout.id);
    if (!defaultLayout.has_value()) {
      layoutOverride.visible = effectiveLayout.visible;
      layoutOverride.collapsed = effectiveLayout.collapsed;
      layoutOverride.x = effectiveLayout.x;
      layoutOverride.y = effectiveLayout.y;
      layoutOverride.width = effectiveLayout.width;
      layoutOverride.height = effectiveLayout.height;
      overrides.layoutWindows.push_back(layoutOverride);
      continue;
    }

    const auto &baseline = defaultLayout->get();
    if (effectiveLayout.visible != baseline.visible) {
      layoutOverride.visible = effectiveLayout.visible;
    }
    if (effectiveLayout.collapsed != baseline.collapsed) {
      layoutOverride.collapsed = effectiveLayout.collapsed;
    }
    if (effectiveLayout.x != baseline.x) {
      layoutOverride.x = effectiveLayout.x;
    }
    if (effectiveLayout.y != baseline.y) {
      layoutOverride.y = effectiveLayout.y;
    }
    if (effectiveLayout.width != baseline.width) {
      layoutOverride.width = effectiveLayout.width;
    }
    if (effectiveLayout.height != baseline.height) {
      layoutOverride.height = effectiveLayout.height;
    }
    if (hasLayoutOverrideFields(layoutOverride)) {
      overrides.layoutWindows.push_back(layoutOverride);
    }
  }

  if (effectiveConfig.preferences.uiFontScale !=
      displayDefault.preferences.uiFontScale) {
    overrides.preferences.uiFontScale =
        clampUiFontScale(effectiveConfig.preferences.uiFontScale);
  }
  sortUniqueWindowOverrides(overrides.layoutWindows);
  return overrides;
}

bool writeYamlToConfig(const std::filesystem::path &rootDir,
                       const std::filesystem::path &configPath,
                       const YAML::Emitter &out) {
  std::error_code ec;
  std::filesystem::create_directories(rootDir, ec);
  if (ec) {
    std::cerr << "[lxe_editor] failed to create editor config directory "
              << rootDir << ": " << ec.message() << "\n";
    return false;
  }

  std::filesystem::path tempPath = configPath;
  tempPath += ".tmp";
  std::filesystem::path backupPath = configPath;
  backupPath += ".bak";

  std::ofstream file(tempPath,
                     std::ios::out | std::ios::binary | std::ios::trunc);
  if (!file) {
    std::cerr << "[lxe_editor] failed to open editor config for write "
              << tempPath << "\n";
    return false;
  }
  file << out.c_str();
  file.flush();
  if (!file) {
    std::cerr << "[lxe_editor] failed to flush editor config " << tempPath
              << "\n";
    file.close();
    std::filesystem::remove(tempPath, ec);
    return false;
  }
  file.close();
  if (!file) {
    std::cerr << "[lxe_editor] failed to close editor config " << tempPath
              << "\n";
    std::filesystem::remove(tempPath, ec);
    return false;
  }

  std::filesystem::rename(tempPath, configPath, ec);
  if (!ec) {
    return true;
  }

  if (!std::filesystem::exists(configPath)) {
    std::cerr << "[lxe_editor] failed to install editor config " << configPath
              << ": " << ec.message() << "\n";
    std::filesystem::remove(tempPath, ec);
    return false;
  }

  ec.clear();
  std::filesystem::remove(backupPath, ec);
  ec.clear();
  std::filesystem::rename(configPath, backupPath, ec);
  if (ec) {
    std::cerr << "[lxe_editor] failed to preserve previous editor config "
              << configPath << ": " << ec.message() << "\n";
    std::filesystem::remove(tempPath, ec);
    return false;
  }

  ec.clear();
  std::filesystem::rename(tempPath, configPath, ec);
  if (ec) {
    std::cerr << "[lxe_editor] failed to install editor config " << configPath
              << ": " << ec.message() << "\n";
    std::error_code restoreEc;
    std::filesystem::rename(backupPath, configPath, restoreEc);
    if (restoreEc) {
      std::cerr << "[lxe_editor] failed to restore previous editor config "
                << configPath << ": " << restoreEc.message() << "\n";
    }
    std::filesystem::remove(tempPath, restoreEc);
    return false;
  }

  std::filesystem::remove(backupPath, ec);
  return true;
}

} // namespace

EditorConfigState::EditorConfigState(std::filesystem::path rootDir)
    : m_rootDir(std::move(rootDir)),
      m_configPath(m_rootDir / "editor_config.yaml") {}

const std::filesystem::path &EditorConfigState::configPath() const {
  return m_configPath;
}

EditorConfigDocument EditorConfigState::load() const {
  EditorConfigDocument document;
  if (!std::filesystem::exists(m_configPath)) {
    return document;
  }

  try {
    const YAML::Node root = YAML::LoadFile(m_configPath.string());
    document = loadConfigDocument(root);
    if (document.version != kEditorConfigVersion) {
      std::cerr << "[lxe_editor] unsupported editor config version in "
                << m_configPath << ", using defaults\n";
      return EditorConfigDocument{};
    }
  } catch (const std::exception &e) {
    std::cerr << "[lxe_editor] failed to load editor config " << m_configPath
              << ": " << e.what() << "\n";
    return EditorConfigDocument{};
  }

  return document;
}

bool EditorConfigState::save(const EditorConfigDocument &sourceDocument) const {
  EditorConfigDocument document = sourceDocument;
  document.version = kEditorConfigVersion;
  document.preferences.uiFontScale =
      clampUiFontScale(document.preferences.uiFontScale);
  sortUniqueWindows(document.layoutWindows);

  YAML::Emitter out;
  saveConfigDocument(out, document, true);
  return writeYamlToConfig(m_rootDir, m_configPath, out);
}

EditorDisplayConfigDocument EditorConfigState::loadOrCreateForDisplays(
    const std::vector<LX_core::DisplayInfo> &displays) const {
  EditorDisplayConfigDocument document;
  bool shouldPersistLoadedDocument = false;
  if (!std::filesystem::exists(m_configPath)) {
    syncDisplayProfiles(document, displays);
    (void)saveDisplayDocument(document, document.activeDisplay,
                              document.displayDefault);
    return document;
  }

  try {
    const YAML::Node root = YAML::LoadFile(m_configPath.string());
    int version = kEditorConfigVersion;
    if (const auto versionNode = root["version"]; versionNode) {
      version = versionNode.as<int>();
    }

    if (version == kEditorDisplayConfigVersion) {
      document = loadDisplayConfigDocument(root);
      shouldPersistLoadedDocument = true;
    } else if (version == kEditorConfigVersion) {
      document.displayDefault = loadConfigDocument(root);
      document.displayDefault.version = kEditorConfigVersion;
      shouldPersistLoadedDocument = true;
    } else {
      std::cerr << "[lxe_editor] unsupported editor display config version in "
                << m_configPath << ", using defaults\n";
    }
  } catch (const std::exception &e) {
    std::cerr << "[lxe_editor] failed to load editor display config "
              << m_configPath << ": " << e.what() << "\n";
    document = EditorDisplayConfigDocument{};
  }

  document.version = kEditorDisplayConfigVersion;
  syncDisplayProfiles(document, displays);
  if (shouldPersistLoadedDocument) {
    const EditorConfigDocument effectiveConfig =
        composeEffectiveConfig(document, document.activeDisplay);
    (void)saveDisplayDocument(document, document.activeDisplay,
                              effectiveConfig);
  }
  return document;
}

bool EditorConfigState::saveDisplayDocument(
    const EditorDisplayConfigDocument &sourceDocument,
    std::string_view activeDisplayKey,
    const EditorConfigDocument &effectiveConfig) const {
  EditorDisplayConfigDocument document = sourceDocument;
  document.version = kEditorDisplayConfigVersion;
  document.activeDisplay = std::string(activeDisplayKey);
  document.displayDefault.version = kEditorConfigVersion;
  document.displayDefault.preferences.uiFontScale =
      clampUiFontScale(document.displayDefault.preferences.uiFontScale);
  sortUniqueWindows(document.displayDefault.layoutWindows);
  normalizeDisplayProfiles(document);

  const auto activeIt = std::find_if(
      document.displayProfiles.begin(), document.displayProfiles.end(),
      [activeDisplayKey](const EditorDisplayProfile &profile) {
        return profile.key == activeDisplayKey;
      });
  if (activeIt != document.displayProfiles.end()) {
    activeIt->overrides =
        diffEffectiveConfig(document.displayDefault, effectiveConfig);
  } else if (!activeDisplayKey.empty()) {
    document.displayProfiles.push_back(EditorDisplayProfile{
        .key = std::string(activeDisplayKey),
        .label = std::string(activeDisplayKey),
        .available = false,
        .overrides =
            diffEffectiveConfig(document.displayDefault, effectiveConfig),
    });
  }
  normalizeDisplayProfiles(document);

  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "version" << YAML::Value << document.version;
  out << YAML::Key << "activeDisplay" << YAML::Value << document.activeDisplay;
  out << YAML::Key << "displayDefault" << YAML::Value;
  saveConfigDocument(out, document.displayDefault, false);
  out << YAML::Key << "displayProfiles" << YAML::Value << YAML::BeginSeq;
  for (auto profile : document.displayProfiles) {
    out << YAML::BeginMap;
    out << YAML::Key << "key" << YAML::Value << profile.key;
    out << YAML::Key << "label" << YAML::Value << profile.label;
    out << YAML::Key << "available" << YAML::Value << profile.available;
    out << YAML::Key << "overrides" << YAML::Value;
    if (hasConfigOverrides(profile.overrides)) {
      saveConfigOverrideDocument(out, profile.overrides);
    } else {
      out << YAML::BeginMap << YAML::EndMap;
    }
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;
  out << YAML::EndMap;

  return writeYamlToConfig(m_rootDir, m_configPath, out);
}

EditorConfigDocument EditorConfigState::composeEffectiveConfig(
    const EditorDisplayConfigDocument &document,
    std::string_view displayKey) const {
  EditorConfigDocument effective = document.displayDefault;
  effective.version = kEditorConfigVersion;

  const auto profileIt = std::find_if(
      document.displayProfiles.begin(), document.displayProfiles.end(),
      [displayKey](const EditorDisplayProfile &profile) {
        return profile.key == displayKey;
      });
  if (profileIt == document.displayProfiles.end()) {
    return effective;
  }

  const EditorConfigOverrideDocument &overrides = profileIt->overrides;
  if (overrides.windowPlacement.has_value()) {
    mergeWindowPlacement(effective, *overrides.windowPlacement);
  }
  if (overrides.preferences.uiFontScale.has_value()) {
    effective.preferences.uiFontScale =
        clampUiFontScale(*overrides.preferences.uiFontScale);
  }
  for (const auto &layoutOverride : overrides.layoutWindows) {
    mergeLayoutOverride(effective, layoutOverride);
  }
  sortUniqueWindows(effective.layoutWindows);
  return effective;
}

std::optional<std::reference_wrapper<EditorWindowLayout>>
findEditorWindowLayout(EditorConfigDocument &document, std::string_view id) {
  const auto it = std::find_if(
      document.layoutWindows.begin(), document.layoutWindows.end(),
      [id](const EditorWindowLayout &layout) { return layout.id == id; });
  if (it == document.layoutWindows.end()) {
    return std::nullopt;
  }
  return std::ref(*it);
}

std::optional<std::reference_wrapper<const EditorWindowLayout>>
findEditorWindowLayout(const EditorConfigDocument &document,
                       std::string_view id) {
  const auto it = std::find_if(
      document.layoutWindows.begin(), document.layoutWindows.end(),
      [id](const EditorWindowLayout &layout) { return layout.id == id; });
  if (it == document.layoutWindows.end()) {
    return std::nullopt;
  }
  return std::cref(*it);
}

} // namespace LX_demo::lxe_editor
