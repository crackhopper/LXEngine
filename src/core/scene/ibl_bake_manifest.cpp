#include "core/scene/ibl_bake_manifest.hpp"

#include <cmath>

namespace LX_core {
namespace {

void addDiagnostic(IblBakeValidationResult &result, std::string message) {
  result.ok = false;
  result.diagnostics.push_back(std::move(message));
}

} // namespace

u32 deriveIblBakeMipCount(u32 resolution) {
  if (resolution == 0) {
    return 0;
  }
  u32 mips = 1;
  while (resolution > 1) {
    resolution /= 2;
    ++mips;
  }
  return mips;
}

IblBakeValidationResult
validateIblBakeManifest(const EnvironmentIblBakeManifest &manifest) {
  IblBakeValidationResult result;
  if (manifest.sourceUri.empty()) {
    addDiagnostic(result, "source.uri is required");
  }
  if (manifest.sourceHash.empty()) {
    addDiagnostic(result, "source.hash is required");
  }
  if (manifest.diffuseBasis != "sh9") {
    addDiagnostic(result, "bake.diffuse.basis must be sh9");
  }
  if (manifest.specularFormat != "RGBA16Float") {
    addDiagnostic(result, "bake.specular.format must be RGBA16Float");
  }
  if (manifest.specularResolution == 0) {
    addDiagnostic(result, "bake.specular.resolution must be non-zero");
  }
  const u32 expectedMips = deriveIblBakeMipCount(manifest.specularResolution);
  if (manifest.specularMips != expectedMips) {
    addDiagnostic(result, "bake.specular.mips must equal derived mip count");
  }
  if (manifest.specularRoughness != "alpha-squared") {
    addDiagnostic(result,
                  "bake.specular.roughness must be alpha-squared");
  }
  if (manifest.specularLayout != "cubemap") {
    addDiagnostic(result, "bake.specular.layout must be cubemap");
  }
  if (manifest.specularFaces != 6) {
    addDiagnostic(result, "bake.specular.faces must be 6");
  }
  if (manifest.diffuseFile.empty()) {
    addDiagnostic(result, "outputs.diffuse.file is required");
  }
  if (manifest.specularFile.empty()) {
    addDiagnostic(result, "outputs.specular.file is required");
  }
  return result;
}

IblBakeValidationResult validateIblBakeManifestSource(
    const EnvironmentIblBakeManifest &manifest,
    const ResourceUri &expectedSourceUri, std::string_view expectedSourceHash) {
  return validateIblBakeManifestSource(
      manifest, expectedSourceUri, expectedSourceHash, manifest.sourceKind);
}

IblBakeValidationResult validateIblBakeManifestSource(
    const EnvironmentIblBakeManifest &manifest,
    const ResourceUri &expectedSourceUri, std::string_view expectedSourceHash,
    EnvironmentIblBakeSourceKind expectedSourceKind) {
  IblBakeValidationResult result;
  if (manifest.sourceUri != expectedSourceUri) {
    addDiagnostic(result, "source.uri does not match requested source");
  }
  if (manifest.sourceHash != expectedSourceHash) {
    addDiagnostic(result, "source.hash does not match requested source");
  }
  if (manifest.sourceKind != expectedSourceKind) {
    addDiagnostic(result, "source.kind does not match requested source");
  }
  return result;
}

IblBakeValidationResult
validateIblBakeManifest(const MaterialIblBakeManifest &manifest) {
  IblBakeValidationResult result;
  if (manifest.materialSourceUri.empty()) {
    addDiagnostic(result, "material.uri is required");
  }
  if (manifest.materialType != "standard-pbr") {
    addDiagnostic(result, "material.type must be standard-pbr");
  }
  if (manifest.materialSourceHash.empty()) {
    addDiagnostic(result, "material.hash is required");
  }
  if (manifest.brdfModel != "ggx-smith") {
    addDiagnostic(result, "bake.brdf.model must be ggx-smith");
  }
  if (manifest.brdfFormat != "RG16Float") {
    addDiagnostic(result, "bake.brdf.format must be RG16Float");
  }
  if (manifest.brdfSize != 256) {
    addDiagnostic(result, "brdf.size must be 256");
  }
  if (manifest.brdfFile.empty()) {
    addDiagnostic(result, "outputs.brdf.file is required");
  }
  return result;
}

IblBakeValidationResult validateIblBakeManifestSource(
    const MaterialIblBakeManifest &manifest, const ResourceUri &expectedUri,
    std::string_view expectedHash) {
  IblBakeValidationResult result;
  if (manifest.materialSourceUri != expectedUri) {
    addDiagnostic(result, "material.uri does not match requested material");
  }
  if (manifest.materialSourceHash != expectedHash) {
    addDiagnostic(result, "material.hash does not match requested material");
  }
  return result;
}

IblBakeValidationResult
validateIblBakePayload(const Sh9IrradiancePayload &payload) {
  IblBakeValidationResult result;
  if (payload.space != "world") {
    addDiagnostic(result, "space must be world");
  }
  if (payload.basis != "real-sh") {
    addDiagnostic(result, "basis must be real-sh");
  }
  if (payload.order != 2) {
    addDiagnostic(result, "order must be 2");
  }
  if (payload.layout != "rgb-interleaved") {
    addDiagnostic(result, "layout must be rgb-interleaved");
  }
  bool hasNonZeroCoefficient = false;
  for (const Vec3f &coefficient : payload.coefficients) {
    if (!std::isfinite(coefficient.x) || !std::isfinite(coefficient.y) ||
        !std::isfinite(coefficient.z)) {
      addDiagnostic(result, "coefficients must be finite RGB triples");
      break;
    }
    if (std::fabs(coefficient.x) > 0.000001f ||
        std::fabs(coefficient.y) > 0.000001f ||
        std::fabs(coefficient.z) > 0.000001f) {
      hasNonZeroCoefficient = true;
    }
  }
  if (!hasNonZeroCoefficient) {
    addDiagnostic(result, "coefficients must contain nonzero lighting");
  }
  return result;
}

} // namespace LX_core
