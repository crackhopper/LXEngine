#pragma once

#include "core/offline/offline_render_profile.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LX_tools::offline_render {

struct OfflineRenderCliOptions final {
  std::filesystem::path scenePath;
  std::string cameraPath;
  LX_core::offline::OfflineRenderCliOverrides overrides;
};

[[nodiscard]] OfflineRenderCliOptions
parseOfflineRenderCliArguments(const std::vector<std::string> &args);

[[nodiscard]] LX_core::offline::ResolvedOfflineRenderProfile
loadResolvedOfflineRenderProfile(const OfflineRenderCliOptions &options);

} // namespace LX_tools::offline_render
