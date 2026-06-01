#include "core/offline/offline_render_profile.hpp"

#include <stdexcept>

namespace LX_core::offline {

OfflineRenderProfile makeDefaultOfflineRenderProfile() {
  return OfflineRenderProfile{};
}

OfflineRenderProfiles makeDefaultOfflineRenderProfiles() {
  OfflineRenderProfiles profiles;
  profiles.defaultProfile = "preview";
  profiles.profiles.emplace("preview", makeDefaultOfflineRenderProfile());
  return profiles;
}

ResolvedOfflineRenderProfile resolveOfflineRenderProfile(
    const OfflineRenderProfiles &profiles,
    const OfflineRenderCliOverrides &overrides) {
  const OfflineRenderProfiles effectiveProfiles =
      profiles.empty() ? makeDefaultOfflineRenderProfiles() : profiles;
  const std::string profileName =
      overrides.profileName.value_or(effectiveProfiles.defaultProfile);
  const auto profileIt = effectiveProfiles.profiles.find(profileName);
  if (profileIt == effectiveProfiles.profiles.end()) {
    throw std::runtime_error("offline render profile not found: " +
                             profileName);
  }

  OfflineRenderProfile resolved = profileIt->second;
  if (overrides.width.has_value()) {
    resolved.width = *overrides.width;
  }
  if (overrides.height.has_value()) {
    resolved.height = *overrides.height;
  }
  if (overrides.samples.has_value()) {
    resolved.samples = *overrides.samples;
  }
  if (overrides.maxDepth.has_value()) {
    resolved.maxDepth = *overrides.maxDepth;
  }
  if (overrides.seed.has_value()) {
    resolved.seed = *overrides.seed;
  }

  return ResolvedOfflineRenderProfile{
      .profileName = profileName,
      .profile = std::move(resolved),
      .outputPath = overrides.outputPath,
  };
}

} // namespace LX_core::offline
