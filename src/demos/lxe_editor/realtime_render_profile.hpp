#pragma once

#include "core/offline/offline_render_profile.hpp"
#include "core/platform/types.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace LX_demo::lxe_editor {

struct RealtimeProfileOutputRequest final {
  std::filesystem::path scenePath;
  std::string sceneName;
  std::string profileName;
  LX_core::offline::OutputProfile output;
  std::filesystem::path outputBasePath;
};

struct RealtimeProfileOutputResult final {
  std::filesystem::path linearExrPath;
  std::filesystem::path cpuSrgbPngPath;
  std::filesystem::path pipelineSrgbPngPath;
  std::filesystem::path metadataPath;
  u32 width = 0;
  u32 height = 0;
};

[[nodiscard]] std::filesystem::path makeRealtimeProfileOutputBasePath(
    std::string_view sceneName, std::string_view profileName,
    const LX_core::offline::OutputProfile &output);

[[nodiscard]] std::string realtimeProfileOutputResultJson(
    std::string_view profileName, const RealtimeProfileOutputResult &result);

} // namespace LX_demo::lxe_editor
