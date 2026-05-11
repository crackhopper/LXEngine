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
constexpr float kMinUiFontScale = 0.75f;
constexpr float kMaxUiFontScale = 2.0f;

[[nodiscard]] float clampUiFontScale(const float value) {
  return std::clamp(value, kMinUiFontScale, kMaxUiFontScale);
}

[[nodiscard]] std::optional<LX_core::WindowPlacement>
loadWindowPlacement(const YAML::Node& node) {
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

void saveWindowPlacement(YAML::Emitter& out,
                         const LX_core::WindowPlacement& placement) {
  out << YAML::Key << "window" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "x" << YAML::Value << placement.x;
  out << YAML::Key << "y" << YAML::Value << placement.y;
  out << YAML::Key << "width" << YAML::Value << placement.width;
  out << YAML::Key << "height" << YAML::Value << placement.height;
  out << YAML::Key << "maximized" << YAML::Value << placement.maximized;
  out << YAML::EndMap;
}

[[nodiscard]] std::optional<EditorWindowLayout>
loadLayoutWindow(const YAML::Node& node) {
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

void saveLayoutWindows(YAML::Emitter& out,
                       const std::vector<EditorWindowLayout>& windows) {
  out << YAML::Key << "layout" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "windows" << YAML::Value << YAML::BeginSeq;
  for (const auto& window : windows) {
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

void sortUniqueWindows(std::vector<EditorWindowLayout>& windows) {
  std::stable_sort(windows.begin(), windows.end(),
                   [](const EditorWindowLayout& lhs,
                      const EditorWindowLayout& rhs) { return lhs.id < rhs.id; });
  windows.erase(std::unique(windows.begin(), windows.end(),
                            [](const EditorWindowLayout& lhs,
                               const EditorWindowLayout& rhs) {
                              return lhs.id == rhs.id;
                            }),
                windows.end());
}

} // namespace

EditorConfigState::EditorConfigState(std::filesystem::path rootDir)
    : m_rootDir(std::move(rootDir)),
      m_configPath(m_rootDir / "editor_config.yaml") {}

const std::filesystem::path& EditorConfigState::configPath() const {
  return m_configPath;
}

EditorConfigDocument EditorConfigState::load() const {
  EditorConfigDocument document;
  if (!std::filesystem::exists(m_configPath)) {
    return document;
  }

  try {
    const YAML::Node root = YAML::LoadFile(m_configPath.string());
    if (const auto versionNode = root["version"]; versionNode) {
      document.version = versionNode.as<int>();
    }
    if (document.version != kEditorConfigVersion) {
      std::cerr << "[lxe_editor] unsupported editor config version in "
                << m_configPath << ", using defaults\n";
      return EditorConfigDocument{};
    }

    document.windowPlacement = loadWindowPlacement(root["window"]);

    if (const auto windowsNode = root["layout"]["windows"];
        windowsNode && windowsNode.IsSequence()) {
      for (const auto& windowNode : windowsNode) {
        const auto window = loadLayoutWindow(windowNode);
        if (window.has_value()) {
          document.layoutWindows.push_back(*window);
        }
      }
      sortUniqueWindows(document.layoutWindows);
    }

    if (const auto preferencesNode = root["preferences"]; preferencesNode) {
      if (const auto fontScaleNode = preferencesNode["uiFontScale"];
          fontScaleNode) {
        document.preferences.uiFontScale =
            clampUiFontScale(fontScaleNode.as<float>());
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "[lxe_editor] failed to load editor config " << m_configPath
              << ": " << e.what() << "\n";
    return EditorConfigDocument{};
  }

  return document;
}

bool EditorConfigState::save(const EditorConfigDocument& sourceDocument) const {
  EditorConfigDocument document = sourceDocument;
  document.version = kEditorConfigVersion;
  document.preferences.uiFontScale =
      clampUiFontScale(document.preferences.uiFontScale);
  sortUniqueWindows(document.layoutWindows);

  std::error_code ec;
  std::filesystem::create_directories(m_rootDir, ec);
  if (ec) {
    std::cerr << "[lxe_editor] failed to create editor config directory "
              << m_rootDir << ": " << ec.message() << "\n";
    return false;
  }

  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "version" << YAML::Value << document.version;
  if (document.windowPlacement.has_value()) {
    saveWindowPlacement(out, *document.windowPlacement);
  }
  saveLayoutWindows(out, document.layoutWindows);
  out << YAML::Key << "preferences" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "uiFontScale" << YAML::Value
      << document.preferences.uiFontScale;
  out << YAML::EndMap;
  out << YAML::EndMap;

  std::ofstream file(m_configPath, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!file) {
    std::cerr << "[lxe_editor] failed to open editor config for write "
              << m_configPath << "\n";
    return false;
  }
  file << out.c_str();
  return static_cast<bool>(file);
}

std::optional<std::reference_wrapper<EditorWindowLayout>>
findEditorWindowLayout(EditorConfigDocument& document, std::string_view id) {
  const auto it = std::find_if(
      document.layoutWindows.begin(), document.layoutWindows.end(),
      [id](const EditorWindowLayout& layout) { return layout.id == id; });
  if (it == document.layoutWindows.end()) {
    return std::nullopt;
  }
  return std::ref(*it);
}

std::optional<std::reference_wrapper<const EditorWindowLayout>>
findEditorWindowLayout(const EditorConfigDocument& document, std::string_view id) {
  const auto it = std::find_if(
      document.layoutWindows.begin(), document.layoutWindows.end(),
      [id](const EditorWindowLayout& layout) { return layout.id == id; });
  if (it == document.layoutWindows.end()) {
    return std::nullopt;
  }
  return std::cref(*it);
}

} // namespace LX_demo::lxe_editor
