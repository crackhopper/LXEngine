#pragma once

#include "core/platform/types.hpp"

#include <map>
#include <optional>
#include <string>
#include <unordered_map>

namespace LX_core::offline {

struct OfflineRenderProfile final {
  std::string backend = "vulkan-compute";
  std::string integrator = "primary-ray";
  u32 width = 512;
  u32 height = 512;
  u32 samples = 1;
  u32 maxDepth = 1;
  u32 seed = 1;
  std::string outputFormat = "exr-png";
  std::map<std::string, std::string> extensionYamlByField;
};

struct OfflineRenderProfiles final {
  std::string defaultProfile = "preview";
  std::unordered_map<std::string, OfflineRenderProfile> profiles;

  [[nodiscard]] bool empty() const { return profiles.empty(); }
};

struct OfflineRenderCliOverrides final {
  std::optional<std::string> profileName;
  std::optional<u32> width;
  std::optional<u32> height;
  std::optional<u32> samples;
  std::optional<u32> maxDepth;
  std::optional<u32> seed;
  std::optional<std::string> outputPath;
};

struct ResolvedOfflineRenderProfile final {
  std::string profileName;
  OfflineRenderProfile profile;
  std::optional<std::string> outputPath;
};

[[nodiscard]] OfflineRenderProfile makeDefaultOfflineRenderProfile();
[[nodiscard]] OfflineRenderProfiles makeDefaultOfflineRenderProfiles();

[[nodiscard]] ResolvedOfflineRenderProfile resolveOfflineRenderProfile(
    const OfflineRenderProfiles &profiles,
    const OfflineRenderCliOverrides &overrides);

} // namespace LX_core::offline
