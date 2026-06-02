#include "core/offline/offline_render_profile.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace LX_core::offline {

OutputProfile makeDefaultOutputProfile() {
  return OutputProfile{};
}

OfflineRenderSettings makeDefaultOfflineRenderSettings() {
  return OfflineRenderSettings{};
}

RenderProfileDocument makeDefaultRenderProfileDocument() {
  RenderProfileDocument document;
  document.defaultOutputProfile = "preview";
  document.outputProfiles.emplace("preview", makeDefaultOutputProfile());
  document.offline = makeDefaultOfflineRenderSettings();
  document.offline.profileName = document.defaultOutputProfile;
  return document;
}

ResolvedRenderProfile resolveRenderProfileDocument(
    const RenderProfileDocument &document,
    const RenderProfileCliOverrides &overrides) {
  // A default-constructed core document represents the built-in preview setup;
  // scene loading validates explicit YAML documents before reaching this point.
  const RenderProfileDocument effective =
      document.empty() ? makeDefaultRenderProfileDocument() : document;

  OfflineRenderSettings offline = effective.offline;
  if (overrides.profileName.has_value()) {
    offline.profileName = *overrides.profileName;
  } else if (offline.profileName.empty()) {
    offline.profileName = effective.defaultOutputProfile;
  }

  const auto profileIt = effective.outputProfiles.find(offline.profileName);
  if (profileIt == effective.outputProfiles.end()) {
    throw std::runtime_error("output profile not found: " +
                             offline.profileName);
  }

  OutputProfile output = profileIt->second;
  if (overrides.width.has_value()) {
    output.width = *overrides.width;
  }
  if (overrides.height.has_value()) {
    output.height = *overrides.height;
  }
  if (overrides.samples.has_value()) {
    offline.samples = *overrides.samples;
  }
  if (overrides.maxBounce.has_value()) {
    offline.maxBounce = *overrides.maxBounce;
  }
  if (overrides.seed.has_value()) {
    offline.seed = *overrides.seed;
  }

  if (output.width == 0 || output.height == 0) {
    throw std::runtime_error("output profile " + offline.profileName +
                             " width/height must be positive");
  }
  if (offline.samples == 0 || offline.maxBounce == 0) {
    throw std::runtime_error("offlineRender samples/maxBounce must be positive");
  }

  return ResolvedRenderProfile{
      .profileName = offline.profileName,
      .output = std::move(output),
      .offline = std::move(offline),
      .outputPath = overrides.outputPath,
  };
}

CameraProjection resolveOutputCameraProjection(const CameraProjection &base,
                                               const OutputProfile &output) {
  CameraProjection projection = base;
  const float outputAspect =
      output.height == 0
          ? projection.aspect
          : static_cast<float>(output.width) / static_cast<float>(output.height);
  projection.aspect = output.cameraOverrides.aspect.value_or(outputAspect);
  if (output.cameraOverrides.fovY.has_value()) {
    projection.fovYDegrees = *output.cameraOverrides.fovY;
  }
  if (output.cameraOverrides.nearPlane.has_value()) {
    projection.nearPlane = *output.cameraOverrides.nearPlane;
  }
  if (output.cameraOverrides.farPlane.has_value()) {
    projection.farPlane = *output.cameraOverrides.farPlane;
  }

  if (projection.type == CameraType::Orthographic) {
    const float centerX = 0.5f * (projection.left + projection.right);
    const float centerY = 0.5f * (projection.bottom + projection.top);
    const float currentHeight =
        std::max(projection.top - projection.bottom, 0.0001f);
    const float height =
        output.cameraOverrides.orthographicHeight.value_or(currentHeight);
    const float width = height * std::max(projection.aspect, 0.0001f);
    projection.left = centerX - width * 0.5f;
    projection.right = centerX + width * 0.5f;
    projection.bottom = centerY - height * 0.5f;
    projection.top = centerY + height * 0.5f;
  }

  return projection;
}

} // namespace LX_core::offline
