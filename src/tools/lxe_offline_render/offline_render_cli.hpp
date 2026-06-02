#pragma once

#include "core/offline/offline_render_profile.hpp"

#include <filesystem>
#include <vector>

namespace LX_tools::offline_render {

struct OfflineRenderCliOptions final {
  std::filesystem::path scenePath;
  LX_core::offline::RenderProfileCliOverrides overrides;
};

[[nodiscard]] OfflineRenderCliOptions
parseOfflineRenderCliArguments(const std::vector<std::string> &args);

[[nodiscard]] LX_core::offline::ResolvedRenderProfile
loadResolvedRenderProfile(const OfflineRenderCliOptions &options);

} // namespace LX_tools::offline_render
