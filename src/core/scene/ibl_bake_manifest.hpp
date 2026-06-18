#pragma once

#include "core/math/vec.hpp"
#include "core/platform/types.hpp"
#include "core/resource/resource_uri.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace LX_core {

struct IblBakeValidationResult final {
  bool ok = true;
  std::vector<std::string> diagnostics;
};

struct Sh9IrradiancePayload final {
  std::string space = "world";
  std::string basis = "real-sh";
  u32 order = 2;
  std::string layout = "rgb-interleaved";
  std::array<Vec3f, 9> coefficients{};
};

struct EnvironmentIblBakeManifest final {
  ResourceUri sourceUri;
  std::string sourceHash;
  std::string diffuseBasis = "sh9";
  u32 specularResolution = 256;
  u32 specularMips = 9;
  std::string specularFormat = "RGBA16Float";
  std::string specularRoughness = "alpha-squared";
  std::string specularLayout = "cubemap";
  u32 specularFaces = 6;
  std::filesystem::path diffuseFile;
  std::filesystem::path specularFile;
};

struct MaterialIblBakeManifest final {
  std::string materialType = "standard-pbr";
  ResourceUri materialSourceUri;
  std::string materialSourceHash;
  std::string brdfModel = "ggx-smith";
  std::string brdfFormat = "RG16Float";
  u32 brdfSize = 256;
  std::filesystem::path brdfFile;
};

[[nodiscard]] u32 deriveIblBakeMipCount(u32 resolution);
[[nodiscard]] IblBakeValidationResult
validateIblBakeManifest(const EnvironmentIblBakeManifest &manifest);
[[nodiscard]] IblBakeValidationResult validateIblBakeManifestSource(
    const EnvironmentIblBakeManifest &manifest,
    const ResourceUri &expectedSourceUri, std::string_view expectedSourceHash);
[[nodiscard]] IblBakeValidationResult
validateIblBakeManifest(const MaterialIblBakeManifest &manifest);
[[nodiscard]] IblBakeValidationResult validateIblBakeManifestSource(
    const MaterialIblBakeManifest &manifest, const ResourceUri &expectedUri,
    std::string_view expectedHash);
[[nodiscard]] IblBakeValidationResult
validateIblBakePayload(const Sh9IrradiancePayload &payload);

} // namespace LX_core
