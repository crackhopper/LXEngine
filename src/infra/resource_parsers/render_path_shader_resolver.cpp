#include "infra/resource_parsers/render_path_shader_resolver.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <sstream>
#include <string_view>

namespace LX_infra {
namespace {

constexpr std::string_view kAssetsPrefix = "assets://";
constexpr std::string_view kFilePrefix = "file://";
constexpr std::string_view kShaderRoot = "assets/shaders/glsl/";
constexpr std::string_view kRenderPathPrefix = "render_paths/";
constexpr std::string_view kLegacyTechniquePrefix = "techniques/";
constexpr std::array<std::string_view, 6> kSupportedStageExtensions{
    ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese",
};

[[nodiscard]] bool startsWith(std::string_view text, std::string_view prefix) {
  return text.rfind(prefix, 0) == 0;
}

[[nodiscard]] std::filesystem::path
pathFromUri(const LX_core::ResourceUri &uri) {
  const std::string &text = uri.string();
  if (startsWith(text, kAssetsPrefix)) {
    return std::filesystem::path("assets") / text.substr(kAssetsPrefix.size());
  }
  if (startsWith(text, kFilePrefix)) {
    return std::filesystem::path(text.substr(kFilePrefix.size()));
  }
  return std::filesystem::path(text);
}

[[nodiscard]] bool sourceFileExists(const LX_core::ResourceUri &uri) {
  return std::filesystem::is_regular_file(pathFromUri(uri));
}

[[nodiscard]] LX_core::ResourceUri makeShaderRootUri(std::string_view path) {
  return LX_core::ResourceUri(std::string(kShaderRoot) + std::string(path));
}

[[nodiscard]] std::string diagnosticPrefix(const LX_core::ResourceUri &graphUri,
                                           const std::string &passId,
                                           const LX_core::ResourceUri &shaderUri) {
  return "RenderPathGraph '" + graphUri.string() + "' pass '" + passId +
         "' shader '" + shaderUri.string() + "'";
}

[[nodiscard]] std::string legacyRealtimeTechniqueRoot() {
  return std::string(kShaderRoot) + "techniques" + '/';
}

[[nodiscard]] std::string legacyRealtimeAssetsUriRoot() {
  return std::string(kAssetsPrefix) + "shaders/glsl/" + "techniques" + '/';
}

[[nodiscard]] bool containsLegacyRealtimePath(std::string_view shader,
                                              std::string_view pathRoot) {
  const std::string root(pathRoot);
  return shader.find(root + "Forward") != std::string_view::npos ||
         shader.find(root + "Deferred") != std::string_view::npos;
}

[[nodiscard]] bool isLegacyRealtimeDirectPath(std::string_view shader) {
  return containsLegacyRealtimePath(shader, legacyRealtimeTechniqueRoot()) ||
         containsLegacyRealtimePath(shader, legacyRealtimeAssetsUriRoot());
}

[[nodiscard]] bool isAllowedRootUtilityShader(std::string_view shader) {
  static constexpr std::array<std::string_view, 9> kAllowedRootUtilities{
      "post_process",
      "debug_overlay",
      "bloom_threshold",
      "bloom_blur_h",
      "bloom_blur_v",
      "skybox",
      "debug_color_transfer_tonemap",
      "debug_color_transfer_copy",
      "debug_color_transfer_ramp",
  };
  for (const std::string_view allowed : kAllowedRootUtilities) {
    if (shader == allowed) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::string_view supportedStageExtensionsText() {
  return ".vert, .frag, .comp, .geom, .tesc, .tese";
}

[[nodiscard]] std::string pathExtension(std::string_view shader) {
  return std::filesystem::path(std::string(shader)).extension().string();
}

[[nodiscard]] bool hasCompilerSupportedStageExtension(std::string_view shader) {
  const std::string extension = pathExtension(shader);
  for (const std::string_view supported : kSupportedStageExtensions) {
    if (extension == supported) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool hasPathExtension(std::string_view shader) {
  return !pathExtension(shader).empty();
}

[[nodiscard]] bool isDirectShaderLocation(std::string_view shader) {
  return startsWith(shader, kShaderRoot) || startsWith(shader, kAssetsPrefix) ||
         startsWith(shader, kFilePrefix) ||
         std::filesystem::path(std::string(shader)).is_absolute();
}

[[nodiscard]] bool isDirectShaderSourceCandidate(std::string_view shader) {
  return isDirectShaderLocation(shader) || hasPathExtension(shader);
}

[[nodiscard]] std::string formatUriList(
    const std::vector<LX_core::ResourceUri> &uris) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < uris.size(); ++i) {
    if (i != 0) {
      oss << ", ";
    }
    oss << '\'' << uris[i].string() << '\'';
  }
  return oss.str();
}

void diagnoseMissingStageShape(RenderPathShaderSourceResolveResult &result,
                               const LX_core::ResourceUri &graphUri,
                               const std::string &passId,
                               const LX_core::ResourceUri &shaderUri,
                               std::string_view base,
                               const LX_core::ResourceUri &vertUri,
                               const LX_core::ResourceUri &fragUri,
                               const LX_core::ResourceUri &compUri) {
  std::vector<LX_core::ResourceUri> missing;
  const bool vertExists = sourceFileExists(vertUri);
  const bool fragExists = sourceFileExists(fragUri);
  const bool compExists = sourceFileExists(compUri);
  if (!vertExists) {
    missing.push_back(vertUri);
  }
  if (!fragExists) {
    missing.push_back(fragUri);
  }
  if (!compExists) {
    missing.push_back(compUri);
  }

  const bool partialRasterPair = vertExists != fragExists;
  result.diagnostics.push_back(
      diagnosticPrefix(graphUri, passId, shaderUri) +
      (partialRasterPair ? " has unsupported shader source shape"
                         : " is missing shader source files") +
      "; expected complete raster pair '" + std::string(kShaderRoot) +
      std::string(base) + ".vert' + '" + std::string(kShaderRoot) +
      std::string(base) + ".frag' or compute source '" +
      std::string(kShaderRoot) + std::string(base) +
      ".comp'; missing expected files: " + formatUriList(missing) +
      "; use " + std::string(kRenderPathPrefix) +
      "... for RenderPath pass shaders");
}

[[nodiscard]] RenderPathShaderSourceResolveResult
resolveStageShape(const LX_core::ResourceUri &graphUri,
                  const std::string &passId,
                  const LX_core::ResourceUri &shaderUri,
                  std::string_view base) {
  RenderPathShaderSourceResolveResult result;
  const LX_core::ResourceUri vertUri = makeShaderRootUri(
      std::string(base) + ".vert");
  const LX_core::ResourceUri fragUri = makeShaderRootUri(
      std::string(base) + ".frag");
  const LX_core::ResourceUri compUri = makeShaderRootUri(
      std::string(base) + ".comp");

  if (sourceFileExists(vertUri) && sourceFileExists(fragUri)) {
    result.sourceUris = {vertUri, fragUri};
    return result;
  }
  if (sourceFileExists(compUri)) {
    result.sourceUris = {compUri};
    return result;
  }

  diagnoseMissingStageShape(result, graphUri, passId, shaderUri, base, vertUri,
                            fragUri, compUri);
  return result;
}

[[nodiscard]] LX_core::ResourceUri
directSourceUri(std::string_view shader) {
  if (startsWith(shader, kShaderRoot) || startsWith(shader, kAssetsPrefix) ||
      startsWith(shader, kFilePrefix) ||
      std::filesystem::path(std::string(shader)).is_absolute()) {
    return LX_core::ResourceUri(shader);
  }
  return makeShaderRootUri(shader);
}

} // namespace

RenderPathShaderSourceResolveResult
resolveRenderPathShaderSourceUris(const LX_core::ResourceUri &graphUri,
                                  const std::string &passId,
                                  const LX_core::ResourceUri &shaderUri) {
  RenderPathShaderSourceResolveResult result;
  const std::string &shader = shaderUri.string();

  if (shader.empty()) {
    result.diagnostics.push_back(diagnosticPrefix(graphUri, passId, shaderUri) +
                                 " is empty; use " +
                                 std::string(kRenderPathPrefix) +
                                 "... for RenderPath pass shaders");
    return result;
  }

  if (startsWith(shader, kLegacyTechniquePrefix)) {
    result.diagnostics.push_back(
        diagnosticPrefix(graphUri, passId, shaderUri) +
        " rejected legacy shader URI '" + shader + "'; use " +
        std::string(kRenderPathPrefix) +
        "... for RenderPath pass shaders; resolver search path is '" +
        std::string(kShaderRoot) + std::string(kRenderPathPrefix) + "...'");
    return result;
  }

  if (isLegacyRealtimeDirectPath(shader)) {
    result.diagnostics.push_back(
        diagnosticPrefix(graphUri, passId, shaderUri) +
        " rejected legacy realtime shader source URI '" + shader + "'; use " +
        std::string(kRenderPathPrefix) +
        "... pass shader URIs instead of direct old realtime source paths");
    return result;
  }

  if (startsWith(shader, kRenderPathPrefix)) {
    return resolveStageShape(graphUri, passId, shaderUri, shader);
  }

  if (isAllowedRootUtilityShader(shader)) {
    return resolveStageShape(graphUri, passId, shaderUri, shader);
  }

  if (isDirectShaderSourceCandidate(shader) &&
      !hasCompilerSupportedStageExtension(shader)) {
    result.diagnostics.push_back(
        diagnosticPrefix(graphUri, passId, shaderUri) +
        " has unsupported direct shader source extension; use " +
        std::string(kRenderPathPrefix) +
        "... for RenderPath pass shaders or a direct shader source file with "
        "one of these valid compiler-supported stage extensions: " +
        std::string(supportedStageExtensionsText()));
    return result;
  }

  if (hasCompilerSupportedStageExtension(shader)) {
    const LX_core::ResourceUri directUri = directSourceUri(shader);
    if (sourceFileExists(directUri)) {
      result.sourceUris = {directUri};
      return result;
    }
    result.diagnostics.push_back(
        diagnosticPrefix(graphUri, passId, shaderUri) +
        " is missing direct shader source file '" + directUri.string() +
        "'; use " + std::string(kRenderPathPrefix) +
        "... for RenderPath pass shaders or an existing direct shader source "
        "file with one of these valid compiler-supported stage extensions: " +
        std::string(supportedStageExtensionsText()));
    return result;
  }

  result.diagnostics.push_back(
      diagnosticPrefix(graphUri, passId, shaderUri) +
      " has unsupported shader URI; use " + std::string(kRenderPathPrefix) +
      "... for RenderPath pass shaders, an allowed root utility shader, or an "
      "existing direct shader source file with one of these "
      "valid compiler-supported stage extensions: " +
      std::string(supportedStageExtensionsText()));
  return result;
}

} // namespace LX_infra
