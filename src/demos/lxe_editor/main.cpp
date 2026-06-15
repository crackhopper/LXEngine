// REQ-019: default integration demo.
//
// Wires:
//   runtime asset root -> Window -> VulkanRenderer -> SceneRuntime
//   -> EngineLoop -> ImGui editor panels / overlay -> run().

#include "backend/vulkan/vulkan_renderer.hpp"
#include "core/debug_draw/debug_draw.hpp"
#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/console_panel.hpp"
#include "core/editor/editor_state.hpp"
#include "core/editor/inspector_panel.hpp"
#include "core/editor/scene_tree_panel.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/input/mouse_button.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/window/window.hpp"

#include "api_token_state.hpp"
#include "camera_rig.hpp"
#include "display_launch_options.hpp"
#include "editor_config_state.hpp"
#include "editor_log_file.hpp"
#include "editor_session.hpp"
#include "lxe_editor_api_server.hpp"
#include "lxe_editor_api_service.hpp"
#include "runtime_state.hpp"
#include "scene_input_routing.hpp"
#include "scene_interaction_controller.hpp"
#include "scene_runtime.hpp"
#include "selection_camera_input.hpp"
#include "realtime_render_profile.hpp"
#include "ui_overlay.hpp"

#include "yaml-cpp/yaml.h"

#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using LX_core::backend::VulkanRenderer;
using LX_core::gpu::EngineLoop;

namespace demo = LX_demo::lxe_editor;

namespace {

[[nodiscard]] demo::ApiEditorMode
toApiEditorMode(const demo::UiOverlay::EditorMode mode) {
  switch (mode) {
  case demo::UiOverlay::EditorMode::Selection:
    return demo::ApiEditorMode::Selection;
  }
  return demo::ApiEditorMode::Unknown;
}

[[nodiscard]] demo::ApiCameraControlMode
toApiCameraControlMode(const demo::UiOverlay::CameraControlMode mode) {
  switch (mode) {
  case demo::UiOverlay::CameraControlMode::Orbit:
    return demo::ApiCameraControlMode::Orbit;
  case demo::UiOverlay::CameraControlMode::FreeFly:
    return demo::ApiCameraControlMode::FreeFly;
  }
  return demo::ApiCameraControlMode::Unknown;
}

struct ApiLaunchOptions final {
  bool enabled = true;
  std::string host = "0.0.0.0";
  std::uint16_t port = 3768;
};

struct RealtimeRenderCliOptions final {
  bool enabled = false;
  std::filesystem::path scenePath;
  std::string profileName;
  std::optional<std::filesystem::path> outputBasePath;
};

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

[[nodiscard]] std::optional<ApiLaunchOptions>
parseApiLaunchOptions(const std::vector<std::string> &args,
                      std::string &errorMessage) {
  ApiLaunchOptions options;
  for (usize i = 1; i < args.size(); ++i) {
    const std::string &arg = args[i];
    if (arg == "--api-disable") {
      options.enabled = false;
      continue;
    }
    if (arg == "--api-enable") {
      options.enabled = true;
      continue;
    }
    if (arg == "--api-host") {
      if (i + 1 >= args.size()) {
        errorMessage = "missing value for --api-host";
        return std::nullopt;
      }
      options.host = args[++i];
      continue;
    }
    if (arg == "--api-port") {
      if (i + 1 >= args.size()) {
        errorMessage = "missing value for --api-port";
        return std::nullopt;
      }
      try {
        const int parsed = std::stoi(args[++i]);
        if (parsed < 0 || parsed > 65535) {
          errorMessage = "api port out of range";
          return std::nullopt;
        }
        options.port = static_cast<std::uint16_t>(parsed);
      } catch (...) {
        errorMessage = "invalid integer for --api-port";
        return std::nullopt;
      }
      continue;
    }
  }
  return options;
}

[[nodiscard]] std::optional<RealtimeRenderCliOptions>
parseRealtimeRenderCliOptions(const std::vector<std::string> &args,
                              std::string &errorMessage) {
  RealtimeRenderCliOptions options;
  for (usize i = 1; i < args.size(); ++i) {
    const std::string &arg = args[i];
    if (arg == "--realtime-render") {
      options.enabled = true;
      continue;
    }
    if (arg == "--scene") {
      if (i + 1 >= args.size()) {
        errorMessage = "missing value for --scene";
        return std::nullopt;
      }
      options.scenePath = args[++i];
      continue;
    }
    if (arg == "--profile") {
      if (i + 1 >= args.size()) {
        errorMessage = "missing value for --profile";
        return std::nullopt;
      }
      options.profileName = args[++i];
      continue;
    }
    if (arg == "--out") {
      if (i + 1 >= args.size()) {
        errorMessage = "missing value for --out";
        return std::nullopt;
      }
      options.outputBasePath = std::filesystem::path(args[++i]);
      continue;
    }
  }

  if (!options.enabled) {
    return options;
  }
  if (options.scenePath.empty()) {
    errorMessage = "--realtime-render requires --scene";
    return std::nullopt;
  }
  if (options.profileName.empty()) {
    errorMessage = "--realtime-render requires --profile";
    return std::nullopt;
  }
  return options;
}

void printDisplayList(const std::vector<LX_core::DisplayInfo> &displays,
                      std::string_view activeDisplayKey) {
  std::cout << "[lxe_editor] displays\n";
  for (const auto &display : displays) {
    const char *active = display.key == activeDisplayKey ? " active" : "";
    std::cout << "  [" << display.index << "] key=" << display.key
              << " label=\"" << display.label << "\" bounds=("
              << display.bounds.x << "," << display.bounds.y << " "
              << display.bounds.width << "x" << display.bounds.height
              << ") usable=(" << display.usableBounds.x << ","
              << display.usableBounds.y << " " << display.usableBounds.width
              << "x" << display.usableBounds.height
              << ") scale=" << display.contentScale << active << "\n";
  }
}

[[nodiscard]] const demo::EditorDisplayProfile *
findDisplayProfile(const demo::EditorDisplayConfigDocument &document,
                   std::string_view key) {
  const auto it = std::find_if(
      document.displayProfiles.begin(), document.displayProfiles.end(),
      [key](const demo::EditorDisplayProfile &profile) {
        return profile.key == key;
      });
  return it == document.displayProfiles.end() ? nullptr : &*it;
}

[[nodiscard]] demo::EditorDisplayProfile *
findDisplayProfile(demo::EditorDisplayConfigDocument &document,
                   std::string_view key) {
  const auto it = std::find_if(
      document.displayProfiles.begin(), document.displayProfiles.end(),
      [key](const demo::EditorDisplayProfile &profile) {
        return profile.key == key;
      });
  return it == document.displayProfiles.end() ? nullptr : &*it;
}

[[nodiscard]] std::string boolJson(const bool value) {
  return value ? "true" : "false";
}

[[nodiscard]] std::string
windowPlacementJson(const std::optional<LX_core::WindowPlacement> &placement) {
  if (!placement.has_value()) {
    return "null";
  }
  std::ostringstream out;
  out << "{\"x\":" << placement->x << ",\"y\":" << placement->y
      << ",\"width\":" << placement->width
      << ",\"height\":" << placement->height
      << ",\"maximized\":" << boolJson(placement->maximized) << "}";
  return out.str();
}

[[nodiscard]] std::string
layoutWindowsJson(const std::vector<demo::EditorWindowLayout> &windows) {
  std::ostringstream out;
  out << "[";
  for (usize i = 0; i < windows.size(); ++i) {
    if (i != 0) {
      out << ",";
    }
    const auto &window = windows[i];
    out << "{\"id\":\"" << demo::apiJsonEscape(window.id) << "\""
        << ",\"visible\":" << boolJson(window.visible)
        << ",\"collapsed\":" << boolJson(window.collapsed)
        << ",\"x\":" << window.x << ",\"y\":" << window.y
        << ",\"width\":" << window.width << ",\"height\":" << window.height
        << "}";
  }
  out << "]";
  return out.str();
}

[[nodiscard]] std::string
configJson(const demo::EditorConfigDocument &document) {
  std::ostringstream out;
  out << "{\"window\":" << windowPlacementJson(document.windowPlacement)
      << ",\"layout\":{\"windows\":"
      << layoutWindowsJson(document.layoutWindows) << "}"
      << ",\"preferences\":{\"uiFontScale\":"
      << document.preferences.uiFontScale << "}}";
  return out.str();
}

[[nodiscard]] std::string windowPlacementOverrideJson(
    const std::optional<demo::EditorWindowPlacementOverride> &placement) {
  if (!placement.has_value()) {
    return "null";
  }
  std::ostringstream out;
  out << "{";
  bool first = true;
  auto addInt = [&](std::string_view key, const std::optional<int> &value) {
    if (!value.has_value()) {
      return;
    }
    out << (first ? "" : ",") << "\"" << key << "\":" << *value;
    first = false;
  };
  addInt("x", placement->x);
  addInt("y", placement->y);
  addInt("width", placement->width);
  addInt("height", placement->height);
  if (placement->maximized.has_value()) {
    out << (first ? "" : ",")
        << "\"maximized\":" << boolJson(*placement->maximized);
  }
  out << "}";
  return out.str();
}

[[nodiscard]] std::string layoutWindowOverridesJson(
    const std::vector<demo::EditorWindowLayoutOverride> &windows) {
  std::ostringstream out;
  out << "[";
  for (usize i = 0; i < windows.size(); ++i) {
    if (i != 0) {
      out << ",";
    }
    const auto &window = windows[i];
    out << "{\"id\":\"" << demo::apiJsonEscape(window.id) << "\"";
    if (window.visible.has_value()) {
      out << ",\"visible\":" << boolJson(*window.visible);
    }
    if (window.collapsed.has_value()) {
      out << ",\"collapsed\":" << boolJson(*window.collapsed);
    }
    if (window.x.has_value()) {
      out << ",\"x\":" << *window.x;
    }
    if (window.y.has_value()) {
      out << ",\"y\":" << *window.y;
    }
    if (window.width.has_value()) {
      out << ",\"width\":" << *window.width;
    }
    if (window.height.has_value()) {
      out << ",\"height\":" << *window.height;
    }
    out << "}";
  }
  out << "]";
  return out.str();
}

[[nodiscard]] std::string
overridesJson(const demo::EditorConfigOverrideDocument &document) {
  std::ostringstream out;
  out << "{\"window\":" << windowPlacementOverrideJson(document.windowPlacement)
      << ",\"layout\":{\"windows\":"
      << layoutWindowOverridesJson(document.layoutWindows) << "}"
      << ",\"preferences\":{";
  if (document.preferences.uiFontScale.has_value()) {
    out << "\"uiFontScale\":" << *document.preferences.uiFontScale;
  }
  out << "}}";
  return out.str();
}

[[nodiscard]] std::string
displayListJson(const std::vector<LX_core::DisplayInfo> &displays,
                const demo::EditorDisplayConfigDocument &document,
                std::string_view activeDisplayKey) {
  std::ostringstream out;
  out << "{\"profiles\":[";
  for (usize i = 0; i < document.displayProfiles.size(); ++i) {
    if (i != 0) {
      out << ",";
    }
    const auto &profile = document.displayProfiles[i];
    out << "{\"key\":\"" << demo::apiJsonEscape(profile.key) << "\""
        << ",\"label\":\"" << demo::apiJsonEscape(profile.label) << "\""
        << ",\"available\":" << boolJson(profile.available)
        << ",\"active\":" << boolJson(profile.key == activeDisplayKey) << "}";
  }
  out << "],\"displayCount\":" << displays.size() << "}";
  return out.str();
}

[[nodiscard]] std::string displayActiveJson(std::string_view activeDisplayKey) {
  return "{\"activeDisplay\":\"" + demo::apiJsonEscape(activeDisplayKey) +
         "\"}";
}

[[nodiscard]] std::string
displayConfigGetJson(const demo::EditorConfigState &configState,
                     const demo::EditorDisplayConfigDocument &document,
                     std::string_view startupDisplayKey,
                     std::string_view requestedKey) {
  if (requestedKey == "default") {
    return "{\"key\":\"default\",\"config\":" +
           configJson(document.displayDefault) + "}";
  }

  const std::string key = requestedKey == "active"
                              ? std::string(startupDisplayKey)
                              : std::string(requestedKey);
  const auto *profile = findDisplayProfile(document, key);
  if (profile == nullptr) {
    return "{\"ok\":false,\"error\":\"unknown display profile: " +
           demo::apiJsonEscape(key) + "\"}";
  }
  const demo::EditorConfigDocument effective =
      configState.composeEffectiveConfig(document, key);
  std::ostringstream out;
  out << "{\"key\":\"" << demo::apiJsonEscape(key) << "\""
      << ",\"profile\":{\"key\":\"" << demo::apiJsonEscape(profile->key)
      << "\",\"label\":\"" << demo::apiJsonEscape(profile->label)
      << "\",\"available\":" << boolJson(profile->available) << "}"
      << ",\"overrides\":" << overridesJson(profile->overrides)
      << ",\"effective\":" << configJson(effective) << "}";
  return out.str();
}

void applyWindowPatch(const YAML::Node &node,
                      demo::EditorConfigDocument &document) {
  if (!node || !node.IsMap()) {
    return;
  }
  LX_core::WindowPlacement placement =
      document.windowPlacement.value_or(LX_core::WindowPlacement{});
  if (const auto value = node["x"]; value) {
    placement.x = value.as<int>();
  }
  if (const auto value = node["y"]; value) {
    placement.y = value.as<int>();
  }
  if (const auto value = node["width"]; value) {
    placement.width = value.as<int>();
  }
  if (const auto value = node["height"]; value) {
    placement.height = value.as<int>();
  }
  if (const auto value = node["maximized"]; value) {
    placement.maximized = value.as<bool>();
  }
  document.windowPlacement = placement;
}

void applyWindowPatch(const YAML::Node &node,
                      demo::EditorConfigOverrideDocument &document) {
  if (!node || !node.IsMap()) {
    return;
  }
  demo::EditorWindowPlacementOverride placement =
      document.windowPlacement.value_or(demo::EditorWindowPlacementOverride{});
  if (const auto value = node["x"]; value) {
    placement.x = value.as<int>();
  }
  if (const auto value = node["y"]; value) {
    placement.y = value.as<int>();
  }
  if (const auto value = node["width"]; value) {
    placement.width = value.as<int>();
  }
  if (const auto value = node["height"]; value) {
    placement.height = value.as<int>();
  }
  if (const auto value = node["maximized"]; value) {
    placement.maximized = value.as<bool>();
  }
  document.windowPlacement = placement;
}

void applyLayoutPatch(const YAML::Node &node,
                      demo::EditorConfigDocument &document) {
  if (!node || !node.IsMap()) {
    return;
  }
  const auto windows = node["windows"];
  if (!windows || !windows.IsSequence()) {
    return;
  }
  for (const auto &windowNode : windows) {
    if (!windowNode["id"]) {
      continue;
    }
    const std::string id = windowNode["id"].as<std::string>();
    if (id.empty()) {
      continue;
    }
    auto layout = demo::findEditorWindowLayout(document, id);
    if (!layout.has_value()) {
      document.layoutWindows.push_back(demo::EditorWindowLayout{.id = id});
      layout = std::ref(document.layoutWindows.back());
    }
    auto &window = layout->get();
    if (const auto value = windowNode["visible"]; value) {
      window.visible = value.as<bool>();
    }
    if (const auto value = windowNode["collapsed"]; value) {
      window.collapsed = value.as<bool>();
    }
    if (const auto value = windowNode["x"]; value) {
      window.x = value.as<int>();
    }
    if (const auto value = windowNode["y"]; value) {
      window.y = value.as<int>();
    }
    if (const auto value = windowNode["width"]; value) {
      window.width = value.as<int>();
    }
    if (const auto value = windowNode["height"]; value) {
      window.height = value.as<int>();
    }
  }
}

void applyLayoutPatch(const YAML::Node &node,
                      demo::EditorConfigOverrideDocument &document) {
  if (!node || !node.IsMap()) {
    return;
  }
  const auto windows = node["windows"];
  if (!windows || !windows.IsSequence()) {
    return;
  }
  for (const auto &windowNode : windows) {
    if (!windowNode["id"]) {
      continue;
    }
    const std::string id = windowNode["id"].as<std::string>();
    if (id.empty()) {
      continue;
    }
    auto it = std::find_if(
        document.layoutWindows.begin(), document.layoutWindows.end(),
        [&id](const demo::EditorWindowLayoutOverride &layout) {
          return layout.id == id;
        });
    if (it == document.layoutWindows.end()) {
      document.layoutWindows.push_back(
          demo::EditorWindowLayoutOverride{.id = id});
      it = std::prev(document.layoutWindows.end());
    }
    if (const auto value = windowNode["visible"]; value) {
      it->visible = value.as<bool>();
    }
    if (const auto value = windowNode["collapsed"]; value) {
      it->collapsed = value.as<bool>();
    }
    if (const auto value = windowNode["x"]; value) {
      it->x = value.as<int>();
    }
    if (const auto value = windowNode["y"]; value) {
      it->y = value.as<int>();
    }
    if (const auto value = windowNode["width"]; value) {
      it->width = value.as<int>();
    }
    if (const auto value = windowNode["height"]; value) {
      it->height = value.as<int>();
    }
  }
}

void applyConfigPatch(const YAML::Node &patch,
                      demo::EditorConfigDocument &document) {
  const YAML::Node root =
      patch["displayDefault"] ? patch["displayDefault"] : patch;
  applyWindowPatch(root["window"], document);
  applyLayoutPatch(root["layout"], document);
  if (const auto preferences = root["preferences"];
      preferences && preferences.IsMap()) {
    if (const auto value = preferences["uiFontScale"]; value) {
      document.preferences.uiFontScale = value.as<float>();
    }
  }
}

void applyConfigPatch(const YAML::Node &patch,
                      demo::EditorConfigOverrideDocument &document) {
  const YAML::Node root = patch["overrides"] ? patch["overrides"] : patch;
  applyWindowPatch(root["window"], document);
  applyLayoutPatch(root["layout"], document);
  if (const auto preferences = root["preferences"];
      preferences && preferences.IsMap()) {
    if (const auto value = preferences["uiFontScale"]; value) {
      document.preferences.uiFontScale = value.as<float>();
    }
  }
}

bool saveDisplayDocumentPreservingActive(
    demo::EditorConfigState &configState,
    const std::vector<LX_core::DisplayInfo> &displays,
    demo::EditorDisplayConfigDocument &document,
    std::string_view currentDisplayKey,
    const demo::EditorConfigDocument &currentDisplayEffectiveConfig) {
  const std::string requestedActiveDisplay =
      document.activeDisplay.empty() ? std::string(currentDisplayKey)
                                     : document.activeDisplay;
  if (!configState.saveDisplayDocument(document, currentDisplayKey,
                                       currentDisplayEffectiveConfig)) {
    return false;
  }
  document = configState.loadOrCreateForDisplays(displays);
  if (requestedActiveDisplay == currentDisplayKey) {
    return true;
  }

  document.activeDisplay = requestedActiveDisplay;
  const demo::EditorConfigDocument selectedEffective =
      configState.composeEffectiveConfig(document, requestedActiveDisplay);
  if (!configState.saveDisplayDocument(document, requestedActiveDisplay,
                                       selectedEffective)) {
    return false;
  }
  document = configState.loadOrCreateForDisplays(displays);
  return true;
}

[[nodiscard]] std::string
displayConfigSetJson(demo::EditorConfigState &configState,
                     const std::vector<LX_core::DisplayInfo> &displays,
                     demo::EditorDisplayConfigDocument &document,
                     demo::EditorConfigDocument &currentEffectiveConfig,
                     LX_core::Window &window,
                     std::string_view activeDisplayKey, std::string_view key,
                     std::string_view patchText) {
  try {
    const YAML::Node patch = YAML::Load(std::string(patchText));
    if (!patch || !patch.IsMap()) {
      return "{\"ok\":false,\"error\":\"display config patch must be a map\"}";
    }
    if (key == "default") {
      applyConfigPatch(patch, document.displayDefault);
    } else {
      auto *profile = findDisplayProfile(document, key);
      if (profile == nullptr) {
        return "{\"ok\":false,\"error\":\"unknown display profile: " +
               demo::apiJsonEscape(key) + "\"}";
      }
      applyConfigPatch(patch, profile->overrides);
    }
    const demo::EditorConfigDocument effective =
        key == "default" || key == activeDisplayKey
            ? configState.composeEffectiveConfig(document, activeDisplayKey)
            : currentEffectiveConfig;
    if (!saveDisplayDocumentPreservingActive(configState, displays, document,
                                             activeDisplayKey, effective)) {
      return "{\"ok\":false,\"error\":\"failed to save editor_config.yaml\"}";
    }
    const bool applied = key == "default" || key == activeDisplayKey;
    if (applied) {
      currentEffectiveConfig = effective;
      if (effective.windowPlacement.has_value()) {
        window.applyPlacement(*effective.windowPlacement);
      }
    }
    return "{\"ok\":true,\"key\":\"" + demo::apiJsonEscape(key) +
           "\",\"saved\":true,\"applied\":" +
           std::string(applied ? "true" : "false") + "}";
  } catch (const std::exception &error) {
    return "{\"ok\":false,\"error\":\"" + demo::apiJsonEscape(error.what()) +
           "\"}";
  }
}

[[nodiscard]] std::string
displaySelectJson(demo::EditorConfigState &configState,
                  const std::vector<LX_core::DisplayInfo> &displays,
                  demo::EditorDisplayConfigDocument &document,
                  const demo::EditorConfigDocument &currentEffectiveConfig,
                  std::string_view currentDisplayKey, std::string_view key) {
  if (findDisplayProfile(document, key) == nullptr) {
    return "{\"ok\":false,\"error\":\"unknown display profile: " +
           demo::apiJsonEscape(key) + "\"}";
  }
  document.activeDisplay = std::string(key);
  if (!saveDisplayDocumentPreservingActive(configState, displays, document,
                                           currentDisplayKey,
                                           currentEffectiveConfig)) {
    return "{\"ok\":false,\"error\":\"failed to save editor_config.yaml\"}";
  }
  return "{\"ok\":true,\"activeDisplay\":\"" + demo::apiJsonEscape(key) +
         "\",\"restartRequired\":true,\"message\":\"restart required to apply "
         "display selection\"}";
}

[[nodiscard]] int currentProcessId() {
#if defined(_WIN32)
  return _getpid();
#else
  return static_cast<int>(getpid());
#endif
}

[[nodiscard]] std::string currentTimestampString() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t timeNow = std::chrono::system_clock::to_time_t(now);
  std::tm tmNow{};
#if defined(_WIN32)
  localtime_s(&tmNow, &timeNow);
#else
  localtime_r(&timeNow, &tmNow);
#endif
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d-%H%M%S", &tmNow);
  return buffer;
}

[[nodiscard]] std::string runtimeClientHost(std::string_view host) {
  if (host == "0.0.0.0") {
    return "127.0.0.1";
  }
  return std::string(host);
}

struct ClosePromptState final {
  bool open = false;
  bool popupOpened = false;
  bool confirmedClose = false;
  std::optional<std::string> saveError;
};

void drawClosePrompt(ClosePromptState &state, demo::LxeEditorSession &session) {
  if (state.open && !state.popupOpened) {
    ImGui::OpenPopup("Save Scene Before Exit");
    state.popupOpened = true;
  }

  if (!state.open) {
    return;
  }

  if (ImGui::BeginPopupModal("Save Scene Before Exit", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Current scene has unsaved changes.");
    ImGui::TextUnformatted("Save to the scene workspace before closing?");
    if (state.saveError.has_value()) {
      ImGui::Spacing();
      ImGui::TextWrapped("Save failed: %s", state.saveError->c_str());
    }

    if (ImGui::Button("Save")) {
      const auto result = session.saveScene(std::nullopt);
      if (result.ok) {
        state.confirmedClose = true;
        state.open = false;
        state.popupOpened = false;
        state.saveError.reset();
        ImGui::CloseCurrentPopup();
      } else {
        state.saveError = result.message;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
      state.confirmedClose = true;
      state.open = false;
      state.popupOpened = false;
      state.saveError.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      state.open = false;
      state.popupOpened = false;
      state.saveError.reset();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}

} // namespace

int main(int argc, char **argv) {
  expSetEnvVK();
  if (!initializeRuntimeAssetRoot()) {
    std::cerr << "[lxe_editor] failed to initialize runtime asset root\n";
    return 1;
  }
  demo::ScopedEditorLogFile editorLogFile(demo::editorLogFilePath());

  std::vector<std::string> launchArgs;
  launchArgs.reserve(static_cast<usize>(argc));
  for (int i = 0; i < argc; ++i) {
    launchArgs.emplace_back(argv[i]);
  }

  const auto displayArgResult = demo::parseDisplayLaunchOptions(launchArgs);
  if (displayArgResult.error.has_value()) {
    std::cerr << "[lxe_editor] " << *displayArgResult.error << "\n";
    return 1;
  }

  std::string apiArgError;
  const auto apiOptions =
      parseApiLaunchOptions(displayArgResult.remainingArgs, apiArgError);
  if (!apiOptions.has_value()) {
    std::cerr << "[lxe_editor] " << apiArgError << "\n";
    return 1;
  }
  std::string realtimeRenderArgError;
  const auto realtimeRenderOptions = parseRealtimeRenderCliOptions(
      displayArgResult.remainingArgs, realtimeRenderArgError);
  if (!realtimeRenderOptions.has_value()) {
    std::cerr << "[lxe_editor] " << realtimeRenderArgError << "\n";
    return 1;
  }

  try {
    LX_infra::Window::Initialize();
    const std::vector<LX_core::DisplayInfo> displays =
        LX_infra::Window::enumerateDisplays();
    if (displays.empty()) {
      std::cerr << "[lxe_editor] no displays available\n";
      return 1;
    }

    if (!demo::shouldLoadDisplayConfigDocument(displayArgResult.options)) {
      printDisplayList(displays, std::string_view{});
      return 0;
    }

    demo::EditorConfigState configState(resolveRuntimePath("data/lxe_editor"));
    demo::EditorDisplayConfigDocument displayConfig =
        configState.loadOrCreateForDisplays(displays);

    std::string displayError;
    const auto startupDisplayIndex = demo::selectStartupDisplayIndex(
        displays, displayArgResult.options.displaySelector,
        displayConfig.activeDisplay, displayError);
    if (!startupDisplayIndex.has_value()) {
      std::cerr << "[lxe_editor] " << displayError << "\n";
      return 1;
    }

    const LX_core::DisplayInfo &startupDisplay = displays[*startupDisplayIndex];
    displayConfig.activeDisplay = startupDisplay.key;
    std::string currentDisplayKey = startupDisplay.key;
    demo::EditorConfigDocument editorConfig =
        configState.composeEffectiveConfig(displayConfig, startupDisplay.key);
    if (!editorConfig.windowPlacement.has_value()) {
      editorConfig.windowPlacement =
          LX_core::makeDefaultWindowPlacementForDisplay(
              startupDisplay, kWindowWidth, kWindowHeight);
    }

    auto window = std::make_shared<LX_infra::Window>(
        "lxe_editor", kWindowWidth, kWindowHeight,
        LX_infra::WindowCreateOptions{.initialPlacement =
                                          editorConfig.windowPlacement,
                                      .displayKey = startupDisplay.key});

    auto vulkanRenderer =
        std::make_shared<VulkanRenderer>(VulkanRenderer::Token{});
    LX_core::gpu::RendererSharedPtr renderer = vulkanRenderer;
    renderer->initialize(window, "lxe_editor");

    if (realtimeRenderOptions->enabled) {
      demo::SceneRuntime runtime;
      runtime.loadFromDocumentPath(realtimeRenderOptions->scenePath);
      const demo::SceneDocument &document = runtime.document();
      const LX_core::offline::RenderProfileDocument profiles =
          document.hasRenderProfileDocument()
              ? document.renderProfileDocument()
              : LX_core::offline::makeDefaultRenderProfileDocument();
      const LX_core::offline::ResolvedRenderProfile resolved =
          LX_core::offline::resolveRenderProfileDocument(
              profiles,
              LX_core::offline::RenderProfileCliOverrides{
                  .profileName = realtimeRenderOptions->profileName,
                  .outputPath = realtimeRenderOptions->outputBasePath,
              });
      if (!resolved.output.materialTag.empty()) {
        runtime.scene()->setActiveMaterialTagForRenderables(
            resolved.output.materialTag);
      }
      const std::filesystem::path outputBasePath =
          realtimeRenderOptions->outputBasePath.value_or(
              demo::makeRealtimeProfileOutputBasePath(
                  document.sceneName(), resolved.profileName, resolved.output));
      const auto result = vulkanRenderer->generateRealtimeProfileOutput(
          runtime.scene(), resolved.output, outputBasePath);
      std::cout << demo::realtimeProfileOutputResultJson(
                       resolved.profileName,
                       demo::RealtimeProfileOutputResult{
                           .linearExrPath = result.linearExrPath,
                           .cpuSrgbPngPath = result.cpuSrgbPngPath,
                           .pipelineSrgbPngPath = result.pipelineSrgbPngPath,
                           .metadataPath = result.metadataPath,
                           .width = result.width,
                           .height = result.height,
                       })
                << "\n";
      renderer->shutdown();
      return 0;
    }

    demo::CameraRig rig;
    LX_core::EditorState editorState;
    demo::UiOverlay ui;
    demo::LxeEditorSession session(rig, ui, editorState);
    session.editorConfig() = editorConfig;
    demo::LxeEditorSession::DisplayCommandHooks displayCommandHooks{
        .displayListJson =
            [&]() {
              return displayListJson(displays, displayConfig,
                                     currentDisplayKey);
            },
        .displayActiveJson =
            [&]() { return displayActiveJson(currentDisplayKey); },
        .displayConfigGetJson =
            [&](std::string_view key) {
              return displayConfigGetJson(configState, displayConfig,
                                          currentDisplayKey, key);
            },
        .displayConfigSet =
            [&](std::string_view key, std::string_view patch) {
              return displayConfigSetJson(configState, displays, displayConfig,
                                          session.editorConfig(), *window,
                                          currentDisplayKey, key, patch);
            },
        .displaySelect =
            [&](std::string_view key) {
              return displaySelectJson(configState, displays, displayConfig,
                                       session.editorConfig(),
                                       currentDisplayKey, key);
            },
        .displayNext =
            [&]() {
              if (displays.size() <= 1) {
                return std::string(
                    "{\"ok\":false,\"error\":\"no alternate display "
                    "available\"}");
              }

              session.editorConfig().windowPlacement = window->getPlacement();
              displayConfig.activeDisplay = currentDisplayKey;
              if (!configState.saveDisplayDocument(displayConfig,
                                                   currentDisplayKey,
                                                   session.editorConfig())) {
                return std::string(
                    "{\"ok\":false,\"error\":\"failed to save current display "
                    "config\"}");
              }
              displayConfig = configState.loadOrCreateForDisplays(displays);

              auto currentIt = std::find_if(
                  displays.begin(), displays.end(),
                  [&](const LX_core::DisplayInfo &display) {
                    return display.key == currentDisplayKey;
                  });
              if (currentIt == displays.end()) {
                currentIt = displays.begin();
              }
              const usize currentIndex =
                  static_cast<usize>(std::distance(displays.begin(),
                                                   currentIt));
              const usize nextIndex = (currentIndex + 1) % displays.size();
              const LX_core::DisplayInfo &nextDisplay = displays[nextIndex];

              demo::EditorConfigDocument nextConfig =
                  configState.composeEffectiveConfig(displayConfig,
                                                     nextDisplay.key);
              if (!nextConfig.windowPlacement.has_value()) {
                nextConfig.windowPlacement =
                    LX_core::makeDefaultWindowPlacementForDisplay(
                        nextDisplay, kWindowWidth, kWindowHeight);
              }
              window->applyPlacement(*nextConfig.windowPlacement);
              nextConfig.windowPlacement = window->getPlacement();
              session.editorConfig() = nextConfig;
              ui.reloadLayoutFromConfig();
              currentDisplayKey = nextDisplay.key;

              displayConfig.activeDisplay = currentDisplayKey;
              if (!configState.saveDisplayDocument(displayConfig,
                                                   currentDisplayKey,
                                                   session.editorConfig())) {
                return std::string(
                    "{\"ok\":false,\"error\":\"failed to save next display "
                    "config\"}");
              }
              displayConfig = configState.loadOrCreateForDisplays(displays);

              return "{\"ok\":true,\"activeDisplay\":\"" +
                     demo::apiJsonEscape(currentDisplayKey) +
                     "\",\"applied\":true,\"restartRequired\":false}";
            },
    };
    demo::LxeEditorSession::RenderDebugCommandHooks renderDebugCommandHooks{
        .dumpRenderTarget =
            [vulkanRenderer](
                std::string_view targetName,
                const std::optional<std::string> &cameraPath,
                const std::filesystem::path &path) {
              const std::string target{targetName};
              const auto dumped =
                  target == "Forward" || target == "forward" ||
                          target == "DebugOverlay" ||
                          target == "debugOverlay" ||
                          target == "debug_overlay"
                      ? vulkanRenderer->dumpDebugRenderTarget(targetName,
                                                              cameraPath, path)
                      : vulkanRenderer->dumpFrameGraphAttachment(targetName,
                                                                 path);
              return demo::LxeEditorSession::RenderDebugDumpResult{
                  .path = dumped.path,
                  .screenPath = dumped.screenPath,
                  .width = dumped.width,
                  .height = dumped.height,
                  .format = dumped.format,
                  .minValue = dumped.minValue,
                  .maxValue = dumped.maxValue,
                  .meanValue = dumped.meanValue,
                  .nonZeroRatio = dumped.nonZeroRatio,
              };
            },
    };
    demo::LxeEditorSession::RealtimeRenderProfileHooks
        realtimeRenderProfileHooks{
            .generate =
                [vulkanRenderer](
                    LX_core::SceneSharedPtr scene,
                    const demo::RealtimeProfileOutputRequest &request) {
                  const auto generated =
                      vulkanRenderer->generateRealtimeProfileOutput(
                          std::move(scene), request.output,
                          request.outputBasePath);
                  return demo::RealtimeProfileOutputResult{
                      .linearExrPath = generated.linearExrPath,
                      .cpuSrgbPngPath = generated.cpuSrgbPngPath,
                      .pipelineSrgbPngPath = generated.pipelineSrgbPngPath,
                      .metadataPath = generated.metadataPath,
                      .width = generated.width,
                      .height = generated.height,
                  };
                },
        };
    session.initialize(displayCommandHooks, renderDebugCommandHooks,
                       realtimeRenderProfileHooks);
    ClosePromptState closePrompt;
    demo::ApiTokenState apiTokenState(resolveRuntimePath("data/lxe_editor"));
    const std::string apiToken =
        apiOptions->enabled ? apiTokenState.loadOrCreateToken() : std::string{};
    demo::LxeEditorApiServer apiServer(demo::LxeEditorApiServerConfig{
        .enabled = apiOptions->enabled,
        .host = apiOptions->host,
        .port = apiOptions->port,
        .token = apiToken,
    });
    if (apiOptions->enabled) {
      std::string serverError;
      if (!apiServer.start(&serverError)) {
        throw std::runtime_error(serverError);
      }
      std::cout << "[lxe_editor] api listening on " << apiServer.config().host
                << ":" << apiServer.boundPort()
                << " token_file=" << apiTokenState.tokenPath() << "\n";
    }
    const std::uint16_t apiBoundPort =
        apiOptions->enabled ? static_cast<std::uint16_t>(apiServer.boundPort())
                            : 0;
    const std::string runtimeHost = runtimeClientHost(apiServer.config().host);
    demo::saveLxeEditorRuntimeState(
        resolveRuntimePath("data/lxe_editor"),
        demo::LxeEditorRuntimeState{
            .pid = currentProcessId(),
            .httpHost = apiOptions->enabled ? runtimeHost : std::string{},
            .httpPort = apiBoundPort,
            .wsHost = apiOptions->enabled ? runtimeHost : std::string{},
            .wsPort = apiBoundPort,
            .tokenFile = apiTokenState.tokenPath().string(),
            .startedAt = currentTimestampString(),
        });
    auto makeApiService =
        [&](const demo::LxeEditorApiService *previous = nullptr)
        -> std::unique_ptr<demo::LxeEditorApiService> {
      demo::LxeEditorApiService::Hooks hooks{
          .sceneSummary =
              [&]() {
                return demo::ApiSceneSummary{
                    .sceneName = session.scene()->getSceneName(),
                    .dirty = session.isSceneDirty(),
                };
              },
          .projectSummary =
              [&]() -> std::optional<demo::ApiProjectSummary> {
            const auto projectId = session.currentProjectId();
            const auto projectDisplayName = session.currentProjectDisplayName();
            const auto projectActiveScene = session.currentProjectActiveScene();
            const auto projectRoot = session.currentProjectRoot();
            if (!projectId.has_value() || !projectDisplayName.has_value() ||
                !projectActiveScene.has_value() || !projectRoot.has_value()) {
              return std::nullopt;
            }
            return demo::ApiProjectSummary{
                .id = *projectId,
                .displayName = *projectDisplayName,
                .path = projectRoot->string(),
                .dirty = session.isProjectDirty(),
                .activeScene = *projectActiveScene,
            };
          },
          .activeSceneEventKey =
              [&]() -> std::optional<std::string> {
            const auto loadedScene = session.runtimeScenePath();
            if (!loadedScene.has_value()) {
              return std::nullopt;
            }
            return loadedScene->lexically_normal().string();
          },
          .toolbarSnapshot =
              [&]() {
                return demo::ApiToolbarSnapshot{
                    .mode = toApiEditorMode(ui.currentEditorMode()),
                    .camera =
                        toApiCameraControlMode(ui.currentCameraControlMode()),
                    .previewEnabled = editorState.isPreviewEnabled(),
                    .debugEnabled = session.debugEnabled(),
                };
              },
          .lastHitPoint =
              [&]() { return session.sceneInteraction().lastHitPoint(); },
          .recordCommandHistoryLine =
              [&session](std::string_view line) {
                session.recordCommandHistoryLine(line);
              },
          .recording = [&session]()
              -> std::optional<
                  std::reference_wrapper<demo::RecordingController>> {
            return session.recording();
          },
          .displayListJson = displayCommandHooks.displayListJson,
          .displayActiveJson = displayCommandHooks.displayActiveJson,
          .displayConfigGetJson =
              [&](const std::string &key) {
                return displayCommandHooks.displayConfigGetJson(key);
              },
          .displayConfigSet =
              [&](const std::string &key, const std::string &patch) {
                return displayCommandHooks.displayConfigSet(key, patch);
              },
          .displaySelect =
              [&](const std::string &key) {
                return displayCommandHooks.displaySelect(key);
              },
      };
      if (previous) {
        return std::make_unique<demo::LxeEditorApiService>(
            session.commandBus(), editorState, *session.scene(),
            std::move(hooks), *previous);
      }
      return std::make_unique<demo::LxeEditorApiService>(
          session.commandBus(), editorState, *session.scene(),
          std::move(hooks));
    };
    usize apiBindingsGeneration = session.bindingsGeneration();
    auto apiService = makeApiService();

    vulkanRenderer->setDrawUiCallback([&] {
      ui.drawFrame(LX_core::Vec2f{static_cast<float>(window->getWidth()),
                                  static_cast<float>(window->getHeight())});
      drawClosePrompt(closePrompt, session);
      session.editorConfig().windowPlacement = window->getPlacement();
      if (ui.consumeConfigDirty()) {
        (void)saveDisplayDocumentPreservingActive(
            configState, displays, displayConfig, currentDisplayKey,
            session.editorConfig());
      }
    });

    EngineLoop loop;
    loop.initialize(window, renderer);
    LX_core::DebugDraw::setMaterialProvider([] {
      return LX_infra::loadGenericMaterial(
          "assets/materials/debug_line.material");
    });
    loop.startScene(session.scene());

    ui.attachClock(loop.getClock());

    window->onClose([&]() {
      if (!session.isDirty()) {
        return true;
      }
      closePrompt.open = true;
      closePrompt.confirmedClose = false;
      closePrompt.saveError.reset();
      return false;
    });

    auto input = window->getInputState();

    loop.setUpdateHook([&](LX_core::Scene &, const LX_core::Clock &clock) {
      if (closePrompt.confirmedClose) {
        loop.stop();
        return;
      }
      session.flushPendingSceneOpen(loop);
      if (apiBindingsGeneration != session.bindingsGeneration()) {
        apiBindingsGeneration = session.bindingsGeneration();
        apiService = makeApiService(apiService.get());
      }
      apiService->refresh();
      apiServer.pump(*apiService);
      session.pollCommandHistory(loop);
      apiService->refresh();

      const bool imguiReady = ImGui::GetCurrentContext() != nullptr;
      const auto io =
          imguiReady ? std::optional<std::reference_wrapper<const ImGuiIO>>(
                           std::cref(ImGui::GetIO()))
                     : std::nullopt;
      const bool wantsKeyboard = io && io->get().WantCaptureKeyboard;
      const bool wantsMouse = io && io->get().WantCaptureMouse;

      if (!wantsKeyboard) {
        ui.handleHotkeys(*input);
      }

      const int windowWidth = window->getWidth();
      const int windowHeight = window->getHeight();
      session.setWindowSize(LX_core::Vec2f{static_cast<float>(windowWidth),
                                           static_cast<float>(windowHeight)});
      const bool hasValidExtent = windowWidth > 0 && windowHeight > 0;
      const float aspect = hasValidExtent ? static_cast<float>(windowWidth) /
                                                static_cast<float>(windowHeight)
                                          : session.editorCamera().getAspect();
      if (hasValidExtent) {
        session.editorCamera().setAspect(aspect);
        session.gameCamera().setAspect(aspect);
      }
      session.gameCamera().updateMatrices();
      (void)session.runtime().updateProceduralMaterials(
          static_cast<float>(clock.totalTime()),
          LX_core::Vec2f{static_cast<float>(windowWidth),
                         static_cast<float>(windowHeight)});

      const demo::SceneInputEditMode inputMode =
          demo::SceneInputEditMode::Selection;
      const bool gizmoConsumesMouse = ui.isGizmoCapturingMouse();
      bool cameraUpdated = false;
      const bool processSelection = demo::shouldProcessSelectionMode(
          editorState.isPreviewEnabled(), wantsMouse, gizmoConsumesMouse,
          inputMode);
      if (processSelection) {
        session.sceneInteraction().updateSelectionMode(
            *input,
            ui.sceneViewRect(LX_core::Vec2f{static_cast<float>(windowWidth),
                                            static_cast<float>(windowHeight)}));
        session.editorCamera().updateMatrices();
      } else {
        session.sceneInteraction().cancelPendingSelectionClick(*input);
      }
      if (demo::shouldProcessCameraRig(editorState.isPreviewEnabled(),
                                       wantsKeyboard, wantsMouse,
                                       gizmoConsumesMouse)) {
        rig.handleOrbitTargetControls(
            *input, *session.scene(),
            ui.sceneViewRect(LX_core::Vec2f{static_cast<float>(windowWidth),
                                            static_cast<float>(windowHeight)}),
            clock.deltaTime());
        demo::SelectionCameraInput cameraInput(*input,
                                               ui.selectionNavigationMode());
        rig.update(cameraInput, clock.deltaTime());
        cameraUpdated = true;
      }
      if (!cameraUpdated) {
        session.editorCamera().updateMatrices();
      }
      const bool suppressEditorDebugHelpers =
          gizmoConsumesMouse ||
          input->isMouseButtonDown(LX_core::MouseButton::Left);
      session.sceneInteraction().enqueueDebugDraw(suppressEditorDebugHelpers);
      rig.enqueueDebugDraw();
      input->nextFrame();
    });

    loop.run();
    std::filesystem::remove(resolveRuntimePath("data/lxe_editor") /
                            "runtime_state.yaml");
    apiServer.stop();
    session.editorConfig().windowPlacement = window->getPlacement();
    (void)saveDisplayDocumentPreservingActive(configState, displays,
                                              displayConfig, currentDisplayKey,
                                              session.editorConfig());
    session.persistEditorData();
    renderer->shutdown();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "[lxe_editor] fatal: " << e.what() << "\n";
    return 2;
  }
}
