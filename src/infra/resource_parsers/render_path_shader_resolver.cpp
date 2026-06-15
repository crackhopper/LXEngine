#include "infra/resource_parsers/render_path_shader_resolver.hpp"

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

  result.diagnostics.push_back(
      diagnosticPrefix(graphUri, passId, shaderUri) +
      " has unsupported shader URI; use " + std::string(kRenderPathPrefix) +
      "... for RenderPath pass shaders");
  return result;
}

} // namespace LX_infra
