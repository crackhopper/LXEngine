#include "demos/scene_viewer/window_layout_state.hpp"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>

namespace LX_demo::scene_viewer {

namespace {

[[nodiscard]] std::optional<std::string>
readTextFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::in | std::ios::binary);
  if (!in) {
    return std::nullopt;
  }

  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool writeTextFile(const std::filesystem::path& path, std::string_view content) {
  if (const auto parent = path.parent_path(); !parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      return false;
    }
  }

  std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }

  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  return static_cast<bool>(out);
}

[[nodiscard]] std::optional<int> parseInt(const std::string& value) {
  try {
    size_t consumed = 0;
    const int parsed = std::stoi(value, &consumed);
    if (consumed != value.size()) {
      return std::nullopt;
    }
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<bool> parseBool(const std::string& value) {
  if (value == "0") {
    return false;
  }
  if (value == "1") {
    return true;
  }
  return std::nullopt;
}

[[nodiscard]] bool isValidNativeWindowPlacement(
    const LX_core::WindowPlacement& placement) {
  return placement.width > 0 && placement.height > 0;
}

struct ImGuiGeometry final {
  int x = 0;
  int y = 0;
};

[[nodiscard]] std::optional<ImGuiGeometry>
parseImGuiGeometryValue(const std::string& value) {
  const size_t comma = value.find(',');
  if (comma == std::string::npos || comma == 0 || comma + 1 >= value.size()) {
    return std::nullopt;
  }

  const auto first = parseInt(value.substr(0, comma));
  const auto second = parseInt(value.substr(comma + 1));
  if (!first.has_value() || !second.has_value()) {
    return std::nullopt;
  }

  return ImGuiGeometry{
      .x = *first,
      .y = *second,
  };
}

struct ImGuiLayoutParseResult final {
  bool hasSection = false;
  bool hasUsableSetting = false;
  std::unordered_set<std::string> sections;
  struct SectionProperties final {
    bool hasAnySetting = false;
    bool hasUsablePos = false;
    bool hasUsableSize = false;
  };
  std::unordered_map<std::string, SectionProperties> sectionProperties;
  std::string error;
};

[[nodiscard]] std::string trimLine(std::string line) {
  while (!line.empty() &&
         (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
    line.pop_back();
  }

  size_t offset = 0;
  while (offset < line.size() &&
         (line[offset] == ' ' || line[offset] == '\t')) {
    ++offset;
  }
  return line.substr(offset);
}

[[nodiscard]] ImGuiLayoutParseResult
parseImGuiLayoutText(std::string_view content) {
  ImGuiLayoutParseResult result;
  std::istringstream lines{std::string(content)};
  std::string line;
  bool insideSection = false;
  std::string currentSection;
  while (std::getline(lines, line)) {
    line = trimLine(std::move(line));
    if (line.empty() || line[0] == ';' || line[0] == '#') {
      continue;
    }

    if (line.front() == '[') {
      if (line.back() != ']' || line.size() < 3) {
        result.error = "invalid section header";
        return result;
      }
      result.hasSection = true;
      result.sections.insert(line);
      currentSection = line;
      insideSection = true;
      continue;
    }

    if (!insideSection) {
      result.error = "setting found before any section header";
      return result;
    }

    const size_t separator = line.find('=');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= line.size()) {
      result.error = "invalid setting line";
      return result;
    }

    const std::string key = line.substr(0, separator);
    const std::string value = line.substr(separator + 1);
    result.hasUsableSetting = true;
    auto& properties = result.sectionProperties[currentSection];
    properties.hasAnySetting = true;
    if (key == "Pos") {
      properties.hasUsablePos = parseImGuiGeometryValue(value).has_value();
    } else if (key == "Size") {
      const auto size = parseImGuiGeometryValue(value);
      properties.hasUsableSize =
          size.has_value() && size->x > 0 && size->y > 0;
    }
  }

  if (!result.hasSection) {
    result.error = "no section headers found";
  } else if (!result.hasUsableSetting) {
    result.error = "no usable settings found";
  }
  return result;
}

[[nodiscard]] std::optional<LX_core::WindowPlacement>
parseNativeWindowPlacementWithReason(std::string_view content,
                                     std::string* error) {
  auto setError = [error](const char* text) {
    if (error != nullptr) {
      *error = text;
    }
  };

  std::unordered_map<std::string, std::string> values;
  std::istringstream lines{std::string(content)};
  std::string line;
  while (std::getline(lines, line)) {
    line = trimLine(std::move(line));
    if (line.empty()) {
      continue;
    }

    const size_t separator = line.find('=');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= line.size()) {
      setError("invalid key/value line");
      return std::nullopt;
    }

    const std::string key = line.substr(0, separator);
    const std::string value = line.substr(separator + 1);
    if (!values.emplace(key, value).second) {
      setError("duplicate key");
      return std::nullopt;
    }
  }

  const auto versionIt = values.find("version");
  if (versionIt == values.end() || versionIt->second != "1") {
    setError("missing or unsupported version");
    return std::nullopt;
  }

  const auto xIt = values.find("x");
  const auto yIt = values.find("y");
  const auto widthIt = values.find("width");
  const auto heightIt = values.find("height");
  const auto maximizedIt = values.find("maximized");
  if (xIt == values.end() || yIt == values.end() || widthIt == values.end() ||
      heightIt == values.end() || maximizedIt == values.end()) {
    setError("missing required placement fields");
    return std::nullopt;
  }

  const auto x = parseInt(xIt->second);
  const auto y = parseInt(yIt->second);
  const auto width = parseInt(widthIt->second);
  const auto height = parseInt(heightIt->second);
  const auto maximized = parseBool(maximizedIt->second);
  if (!x.has_value() || !y.has_value() || !width.has_value() ||
      !height.has_value() || !maximized.has_value()) {
    setError("invalid numeric or boolean field");
    return std::nullopt;
  }
  if (*width <= 0 || *height <= 0) {
    setError("non-positive window extent");
    return std::nullopt;
  }

  return LX_core::WindowPlacement{
      .x = *x,
      .y = *y,
      .width = *width,
      .height = *height,
      .maximized = *maximized,
  };
}

} // namespace

WindowLayoutState::WindowLayoutState(std::filesystem::path rootDir)
    : m_rootDir(std::move(rootDir)),
      m_imguiLayoutPath(m_rootDir / "layout.ini"),
      m_windowStatePath(m_rootDir / "window_state.ini") {}

const std::filesystem::path& WindowLayoutState::imguiLayoutPath() const {
  return m_imguiLayoutPath;
}

const std::filesystem::path& WindowLayoutState::windowStatePath() const {
  return m_windowStatePath;
}

bool WindowLayoutState::restoreImGuiLayout() const {
  if (ImGui::GetCurrentContext() == nullptr) {
    return false;
  }

  const auto content = readTextFile(m_imguiLayoutPath);
  if (!content.has_value()) {
    return false;
  }

  const auto parsed = parseImGuiLayoutText(*content);
  if (!parsed.hasSection || !parsed.hasUsableSetting) {
    std::cerr << "[scene_viewer] warning: ignoring corrupt ImGui layout file '"
              << m_imguiLayoutPath.string() << "': " << parsed.error << "\n";
    return false;
  }

  ImGui::LoadIniSettingsFromMemory(content->c_str(), content->size());
  ImGui::GetIO().WantSaveIniSettings = false;
  return true;
}

bool WindowLayoutState::hasAuthoritativeSceneViewerLayout() const {
  const auto content = readTextFile(m_imguiLayoutPath);
  return content.has_value() && hasSceneViewerCoreLayout(*content);
}

void WindowLayoutState::saveImGuiLayout() const {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }

  size_t contentSize = 0;
  const char* content = ImGui::SaveIniSettingsToMemory(&contentSize);
  if (content == nullptr || contentSize == 0 ||
      !isUsableImGuiLayout(std::string_view(content, contentSize))) {
    return;
  }

  if (writeTextFile(m_imguiLayoutPath, std::string_view(content, contentSize))) {
    ImGui::GetIO().WantSaveIniSettings = false;
    m_imguiWriteWarningEmitted = false;
    return;
  }

  if (!m_imguiWriteWarningEmitted) {
    std::cerr << "[scene_viewer] warning: failed to write ImGui layout file '"
              << m_imguiLayoutPath.string() << "'\n";
    m_imguiWriteWarningEmitted = true;
  }
}

void WindowLayoutState::maybeSaveImGuiLayout() const {
  if (ImGui::GetCurrentContext() == nullptr ||
      !ImGui::GetIO().WantSaveIniSettings) {
    m_imguiWriteWarningEmitted = false;
    return;
  }
  saveImGuiLayout();
}

std::optional<LX_core::WindowPlacement>
WindowLayoutState::loadNativeWindowPlacement() const {
  const auto content = readTextFile(m_windowStatePath);
  if (!content.has_value()) {
    return std::nullopt;
  }
  std::string error;
  const auto placement = parseNativeWindowPlacementWithReason(*content, &error);
  if (!placement.has_value()) {
    std::cerr
        << "[scene_viewer] warning: ignoring corrupt native window state file '"
        << m_windowStatePath.string() << "': " << error << "\n";
  }
  return placement;
}

void WindowLayoutState::saveNativeWindowPlacement(
    const LX_core::WindowPlacement& placement) const {
  if (!isValidNativeWindowPlacement(placement)) {
    return;
  }
  if (writeTextFile(m_windowStatePath,
                    serializeNativeWindowPlacement(placement))) {
    return;
  }

  std::cerr << "[scene_viewer] warning: failed to write native window state file '"
            << m_windowStatePath.string() << "'\n";
}

void WindowLayoutState::restoreNativeWindowPlacement(LX_core::Window& window) const {
  const auto placement = loadNativeWindowPlacement();
  if (!placement.has_value()) {
    return;
  }
  const auto sanitized =
      LX_core::sanitizeWindowPlacement(*placement,
                                       window.getUsableBoundsForPlacement(
                                           *placement));
  if (!sanitized.has_value()) {
    std::cerr
        << "[scene_viewer] warning: ignoring native window state outside usable bounds '"
        << m_windowStatePath.string() << "'\n";
    return;
  }
  window.applyPlacement(*sanitized);
}

void WindowLayoutState::captureNativeWindowPlacement(
    const LX_core::Window& window) const {
  saveNativeWindowPlacement(window.getPlacement());
}

bool WindowLayoutState::isUsableImGuiLayout(std::string_view content) {
  const auto parsed = parseImGuiLayoutText(content);
  return parsed.hasSection && parsed.hasUsableSetting;
}

bool WindowLayoutState::hasSceneViewerCoreLayout(std::string_view content) {
  const auto parsed = parseImGuiLayoutText(content);
  if (!parsed.hasSection || !parsed.hasUsableSetting) {
    return false;
  }

  static const std::array<const char*, 4> kRequiredSections = {
      "[Window][Scene Tree]",
      "[Window][Inspector]",
      "[Window][Command Console]",
      "[Window][Viewport]",
  };
  for (const char* section : kRequiredSections) {
    const auto it = parsed.sectionProperties.find(section);
    if (it == parsed.sectionProperties.end()) {
      return false;
    }
    if (!it->second.hasUsablePos || !it->second.hasUsableSize) {
      return false;
    }
  }
  return true;
}

std::optional<LX_core::WindowPlacement>
WindowLayoutState::parseNativeWindowPlacement(std::string_view content) {
  return parseNativeWindowPlacementWithReason(content, nullptr);
}

std::string WindowLayoutState::serializeNativeWindowPlacement(
    const LX_core::WindowPlacement& placement) {
  return "version=1\n"
         "x=" +
         std::to_string(placement.x) + "\n"
         "y=" + std::to_string(placement.y) + "\n"
         "width=" + std::to_string(placement.width) + "\n"
         "height=" + std::to_string(placement.height) + "\n"
         "maximized=" + std::string(placement.maximized ? "1" : "0") + "\n";
}

} // namespace LX_demo::scene_viewer
