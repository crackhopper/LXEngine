#pragma once

#include "core/platform/window.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace LX_demo::scene_viewer {

class WindowLayoutState final {
public:
  explicit WindowLayoutState(std::filesystem::path rootDir);

  [[nodiscard]] const std::filesystem::path& imguiLayoutPath() const;
  [[nodiscard]] const std::filesystem::path& windowStatePath() const;

  [[nodiscard]] bool restoreImGuiLayout() const;
  [[nodiscard]] bool hasAuthoritativeSceneViewerLayout() const;
  void saveImGuiLayout() const;
  void maybeSaveImGuiLayout() const;

  [[nodiscard]] std::optional<LX_core::WindowPlacement>
  loadNativeWindowPlacement() const;
  void saveNativeWindowPlacement(
      const LX_core::WindowPlacement& placement) const;
  void restoreNativeWindowPlacement(LX_core::Window& window) const;
  void captureNativeWindowPlacement(const LX_core::Window& window) const;

  [[nodiscard]] static bool isUsableImGuiLayout(std::string_view content);
  [[nodiscard]] static bool hasSceneViewerCoreLayout(std::string_view content);
  [[nodiscard]] static std::optional<LX_core::WindowPlacement>
  parseNativeWindowPlacement(std::string_view content);
  [[nodiscard]] static std::string
  serializeNativeWindowPlacement(const LX_core::WindowPlacement& placement);

private:
  std::filesystem::path m_rootDir;
  std::filesystem::path m_imguiLayoutPath;
  std::filesystem::path m_windowStatePath;
  mutable bool m_imguiWriteWarningEmitted = false;
};

} // namespace LX_demo::scene_viewer
