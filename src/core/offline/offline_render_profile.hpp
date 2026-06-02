#pragma once

#include "core/platform/types.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

namespace LX_core::offline {

struct OutputCameraOverrides final {
  std::optional<float> fovY;
  std::optional<float> aspect;
  std::optional<float> nearPlane;
  std::optional<float> farPlane;
  std::optional<float> focusDistance;
  std::optional<float> orthographicHeight;
  std::optional<u32> cullingMask;
};

struct OutputProfile final {
  std::string cameraPath = "/game_cam";
  u32 width = 512;
  u32 height = 512;
  std::string outputFormat = "exr-png";
  std::filesystem::path outDir = "artifacts";
  OutputCameraOverrides cameraOverrides;
  std::map<std::string, std::string> extensionYamlByField;
};

struct OfflineRenderSettings final {
  std::string integrator = "primary-ray";
  u32 samples = 1;
  u32 maxBounce = 1;
  u32 seed = 1;
  std::string profileName;
  bool shadows = true;
  std::map<std::string, std::string> extensionYamlByField;
};

struct RenderProfileDocument final {
  std::string defaultOutputProfile = "preview";
  std::unordered_map<std::string, OutputProfile> outputProfiles;
  OfflineRenderSettings offline;

  [[nodiscard]] bool empty() const { return outputProfiles.empty(); }
};

struct RenderProfileCliOverrides final {
  std::optional<std::string> profileName;
  std::optional<u32> width;
  std::optional<u32> height;
  std::optional<u32> samples;
  std::optional<u32> maxBounce;
  std::optional<u32> seed;
  std::optional<std::filesystem::path> outputPath;
};

struct ResolvedRenderProfile final {
  std::string profileName;
  OutputProfile output;
  OfflineRenderSettings offline;
  std::optional<std::filesystem::path> outputPath;
};

[[nodiscard]] OutputProfile makeDefaultOutputProfile();
[[nodiscard]] OfflineRenderSettings makeDefaultOfflineRenderSettings();
[[nodiscard]] RenderProfileDocument makeDefaultRenderProfileDocument();

[[nodiscard]] ResolvedRenderProfile resolveRenderProfileDocument(
    const RenderProfileDocument &document,
    const RenderProfileCliOverrides &overrides);

} // namespace LX_core::offline
