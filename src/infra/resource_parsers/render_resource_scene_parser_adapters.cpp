#include "infra/resource_parsers/render_resource_scene_parser_adapters.hpp"

#include "infra/resource_parsers/render_feature_resource_parser.hpp"
#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace LX_infra {
namespace {

constexpr const char *kRenderFeatureParserName = "RenderFeatureResourceParser";
constexpr const char *kRenderPathGraphParserName =
    "RenderPathGraphResourceParser";

[[nodiscard]] std::filesystem::path
pathFromUri(const LX_core::ResourceUri &uri) {
  const std::string &text = uri.string();
  constexpr std::string_view assetsPrefix = "assets://";
  constexpr std::string_view filePrefix = "file://";
  if (text.rfind(assetsPrefix, 0) == 0) {
    return std::filesystem::path("assets") / text.substr(assetsPrefix.size());
  }
  if (text.rfind(filePrefix, 0) == 0) {
    return std::filesystem::path(text.substr(filePrefix.size()));
  }
  return std::filesystem::path(text);
}

[[nodiscard]] LX_core::ResourceUri
canonicalResourceUri(const LX_core::ResourceUri &ownerUri,
                     const LX_core::ResourceUri &uri) {
  const std::string &text = uri.string();
  if (text.find("://") != std::string::npos ||
      text.rfind("assets/", 0) == 0 ||
      std::filesystem::path(text).is_absolute()) {
    return uri;
  }
  return LX_core::ResourceUri::canonicalize(ownerUri.string(), text);
}

[[nodiscard]] std::optional<std::string>
readTextFile(const LX_core::ResourceUri &uri, std::string &diagnostic) {
  std::ifstream file(pathFromUri(uri));
  if (!file) {
    diagnostic = "failed to open resource file";
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

[[nodiscard]] bool resourceFileExists(const LX_core::ResourceUri &uri) {
  return std::filesystem::exists(pathFromUri(uri));
}

[[nodiscard]] std::optional<std::vector<LX_core::ResourceUri>>
resolveShaderSourceUris(const LX_core::ResourceUri &shaderUri) {
  const std::string &shader = shaderUri.string();
  const auto exists = [](const LX_core::ResourceUri &uri) {
    return resourceFileExists(uri);
  };
  const auto makeShaderUri = [](const std::string &path) {
    return LX_core::ResourceUri("assets/shaders/glsl/" + path);
  };
  const auto stagePair = [&](const std::string &base)
      -> std::optional<std::vector<LX_core::ResourceUri>> {
    std::vector<LX_core::ResourceUri> sources{
        makeShaderUri(base + ".vert"),
        makeShaderUri(base + ".frag"),
    };
    if (exists(sources[0]) && exists(sources[1])) {
      return sources;
    }
    return std::nullopt;
  };
  const auto computeStage = [&](const std::string &base)
      -> std::optional<std::vector<LX_core::ResourceUri>> {
    std::vector<LX_core::ResourceUri> sources{makeShaderUri(base + ".comp")};
    if (exists(sources[0])) {
      return sources;
    }
    return std::nullopt;
  };

  if (shader == "techniques/Deferred/gbuffer") {
    return stagePair("techniques/Deferred/pbr_gbuffer");
  }
  if (shader == "deferred_lighting") {
    return stagePair("techniques/Deferred/deferred_lighting");
  }

  if (auto pairSources = stagePair(shader); pairSources.has_value()) {
    return pairSources;
  }
  if (auto computeSources = computeStage(shader); computeSources.has_value()) {
    return computeSources;
  }

  const LX_core::ResourceUri directUri =
      shader.rfind("assets/shaders/glsl/", 0) == 0
          ? LX_core::ResourceUri(shader)
          : makeShaderUri(shader);
  if (exists(directUri)) {
    return std::vector<LX_core::ResourceUri>{directUri};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<LX_core::IShaderSharedPtr> compileShaderPayload(
    const LX_core::ResourceUri &graphUri, const LX_core::ResourceUri &shaderUri,
    const std::vector<LX_core::ResourceUri> &sourceUris,
    std::vector<std::string> &diagnostics) {
  std::vector<LX_core::ShaderStageCode> stages;
  for (const LX_core::ResourceUri &sourceUri : sourceUris) {
    const std::filesystem::path sourcePath = pathFromUri(sourceUri);
    LX_infra::CompileResult compiled =
        LX_infra::ShaderCompiler::compileFile(sourcePath);
    if (!compiled.success) {
      diagnostics.push_back(
          "RenderPathGraph '" + graphUri.string() + "' failed to compile "
          "Shader '" + shaderUri.string() + "' source '" +
          sourceUri.string() + "': " + compiled.errorMessage);
      return std::nullopt;
    }
    stages.insert(stages.end(),
                  std::make_move_iterator(compiled.stages.begin()),
                  std::make_move_iterator(compiled.stages.end()));
  }
  if (stages.empty()) {
    diagnostics.push_back("RenderPathGraph '" + graphUri.string() +
                          "' Shader '" + shaderUri.string() +
                          "' produced no compiled stages");
    return std::nullopt;
  }

  std::vector<LX_core::ShaderResourceBinding> bindings;
  std::vector<LX_core::VertexInputAttribute> vertexInputs;
  try {
    bindings = LX_infra::ShaderReflector::reflect(stages);
    vertexInputs = LX_infra::ShaderReflector::reflectVertexInputs(stages);
  } catch (const std::exception &error) {
    diagnostics.push_back(
        "RenderPathGraph '" + graphUri.string() + "' failed to reflect "
        "Shader '" + shaderUri.string() + "': " + error.what());
    return std::nullopt;
  }
  if (bindings.empty() && vertexInputs.empty()) {
    diagnostics.push_back("RenderPathGraph '" + graphUri.string() +
                          "' Shader '" + shaderUri.string() +
                          "' produced no reflection payload");
    return std::nullopt;
  }

  return std::make_shared<LX_infra::CompiledShader>(
      std::move(stages), std::move(bindings), std::move(vertexInputs),
      shaderUri.string());
}

[[nodiscard]] LX_core::ResourceUri
resolveAssetDependencyUri(const LX_core::ResourceUri &ownerUri,
                          const LX_core::ResourceUri &dependencyUri) {
  const LX_core::ResourceUri graphRelative =
      LX_core::ResourceUri::canonicalize(ownerUri.string(),
                                         dependencyUri.string());
  if (resourceFileExists(graphRelative)) {
    return graphRelative;
  }

  const std::string &owner = ownerUri.string();
  const std::string &dependency = dependencyUri.string();
  if (owner.rfind("assets/", 0) == 0 &&
      dependency.find("://") == std::string::npos) {
    const LX_core::ResourceUri assetRootRelative("assets/" + dependency);
    if (resourceFileExists(assetRootRelative)) {
      return assetRootRelative;
    }
  }

  return graphRelative;
}

[[nodiscard]] ParsedSceneResource makeFailedParse(
    LX_core::SceneResourceTable &table, LX_core::SceneResourceType type,
    const LX_core::ResourceUri &ownerUri, const LX_core::ResourceUri &uri,
    const char *parserName, std::vector<std::string> diagnostics) {
  ParsedSceneResource parsed;
  parsed.metadata.type = type;
  parsed.metadata.uri = uri;
  parsed.metadata.state = LX_core::ResourceState::Failed;
  for (const std::string &diagnostic : diagnostics) {
    parsed.metadata.diagnostics.push_back(LX_core::ResourceDiagnostic{
        .ownerUri = ownerUri,
        .resourceUri = uri,
        .parserName = parserName,
        .message = diagnostic,
    });
    parsed.diagnostics.push_back(diagnostic);
  }
  parsed.identity = table.internResourceMetadata(parsed.metadata);
  if (const auto *metadata = table.findResourceMetadata(parsed.identity)) {
    parsed.metadata = *metadata;
  }
  return parsed;
}

[[nodiscard]] ParsedSceneResource parseRenderFeatureIntoTable(
    LX_core::SceneResourceTable &table, const LX_core::ResourceUri &uri,
    const SceneResourceParseContext &context) {
  const LX_core::ResourceUri canonicalUri =
      canonicalResourceUri(context.ownerUri, uri);

  std::string readDiagnostic;
  const auto yamlText = readTextFile(canonicalUri, readDiagnostic);
  if (!yamlText.has_value()) {
    return makeFailedParse(table, LX_core::SceneResourceType::RenderFeature,
                           context.ownerUri, canonicalUri,
                           kRenderFeatureParserName, {readDiagnostic});
  }

  RenderFeatureResourceParser parser;
  auto parsedFeature = parser.parse(canonicalUri, *yamlText);
  if (!parsedFeature.renderFeature.has_value()) {
    return makeFailedParse(table, LX_core::SceneResourceType::RenderFeature,
                           context.ownerUri, canonicalUri,
                           kRenderFeatureParserName,
                           std::move(parsedFeature.diagnostics));
  }

  const LX_core::RenderFeatureHandle handle = table.registerRenderFeature(
      canonicalUri, std::move(*parsedFeature.renderFeature));
  if (!handle.isValid()) {
    return makeFailedParse(
        table, LX_core::SceneResourceType::RenderFeature, context.ownerUri,
        canonicalUri, kRenderFeatureParserName,
        {"render feature payload registration failed"});
  }

  ParsedSceneResource parsed;
  parsed.identity = table.metadataHandle(handle);
  if (const auto *metadata = table.findResourceMetadata(parsed.identity)) {
    parsed.metadata = *metadata;
  }
  return parsed;
}

[[nodiscard]] ParsedSceneResource parseRenderPathGraphIntoTable(
    LX_core::SceneResourceTable &table, const LX_core::ResourceUri &uri,
    const SceneResourceParseContext &context) {
  const LX_core::ResourceUri canonicalUri =
      canonicalResourceUri(context.ownerUri, uri);

  std::string readDiagnostic;
  const auto yamlText = readTextFile(canonicalUri, readDiagnostic);
  if (!yamlText.has_value()) {
    return makeFailedParse(table, LX_core::SceneResourceType::RenderPathGraph,
                           context.ownerUri, canonicalUri,
                           kRenderPathGraphParserName, {readDiagnostic});
  }

  RenderPathGraphResourceParser parser;
  auto parsedGraph = parser.parse(canonicalUri, *yamlText);
  if (!parsedGraph.renderPathGraph.has_value()) {
    return makeFailedParse(table, LX_core::SceneResourceType::RenderPathGraph,
                           context.ownerUri, canonicalUri,
                           kRenderPathGraphParserName,
                           std::move(parsedGraph.diagnostics));
  }

  LX_core::RenderPathGraph graph = std::move(*parsedGraph.renderPathGraph);
  for (auto &featureDependency : graph.features) {
    const LX_core::ResourceUri featureUri =
        resolveAssetDependencyUri(canonicalUri, featureDependency.uri);
    ParsedSceneResource feature = parseRenderFeatureIntoTable(
        table, featureUri, SceneResourceParseContext{.ownerUri = canonicalUri});
    if (!feature.identity.isValid() ||
        feature.metadata.state == LX_core::ResourceState::Failed) {
      return makeFailedParse(
          table, LX_core::SceneResourceType::RenderPathGraph, context.ownerUri,
          canonicalUri, kRenderPathGraphParserName,
          {"failed to load RenderFeature dependency '" + featureUri.string() +
           "'"});
    }
    featureDependency.uri = featureUri;
  }

  for (const auto &pass : graph.passes) {
    if (!pass.shaderUri.empty()) {
      const auto shaderSourceUris = resolveShaderSourceUris(pass.shaderUri);
      if (!shaderSourceUris.has_value()) {
        LX_core::ResourceMetadata failedShader;
        failedShader.type = LX_core::SceneResourceType::Shader;
        failedShader.uri = pass.shaderUri;
        failedShader.state = LX_core::ResourceState::Failed;
        failedShader.diagnostics.push_back(LX_core::ResourceDiagnostic{
            .ownerUri = canonicalUri,
            .resourceUri = pass.shaderUri,
            .parserName = kRenderPathGraphParserName,
            .message = "failed to resolve shader source descriptors",
        });
        const LX_core::ResourceIdentityHandle failedShaderIdentity =
            table.internResourceMetadata(std::move(failedShader));
        (void)failedShaderIdentity;
        return makeFailedParse(
            table, LX_core::SceneResourceType::RenderPathGraph,
            context.ownerUri, canonicalUri, kRenderPathGraphParserName,
            {"failed to resolve Shader dependency '" +
             pass.shaderUri.string() + "' for RenderPathGraph '" +
             canonicalUri.string() + "'"});
      }
      std::vector<std::string> shaderDiagnostics;
      std::optional<LX_core::IShaderSharedPtr> shaderPayload =
          compileShaderPayload(canonicalUri, pass.shaderUri, *shaderSourceUris,
                               shaderDiagnostics);
      if (!shaderPayload.has_value()) {
        LX_core::ResourceMetadata failedShader;
        failedShader.type = LX_core::SceneResourceType::Shader;
        failedShader.uri = pass.shaderUri;
        failedShader.state = LX_core::ResourceState::Failed;
        for (const std::string &diagnostic : shaderDiagnostics) {
          failedShader.diagnostics.push_back(LX_core::ResourceDiagnostic{
              .ownerUri = canonicalUri,
              .resourceUri = pass.shaderUri,
              .parserName = kRenderPathGraphParserName,
              .message = diagnostic,
          });
        }
        const LX_core::ResourceIdentityHandle failedShaderIdentity =
            table.internResourceMetadata(std::move(failedShader));
        (void)failedShaderIdentity;
        return makeFailedParse(
            table, LX_core::SceneResourceType::RenderPathGraph,
            context.ownerUri, canonicalUri, kRenderPathGraphParserName,
            shaderDiagnostics);
      }
      const LX_core::ShaderHandle shaderHandle =
          table.registerShaderResource(pass.shaderUri, *shaderSourceUris,
                                       std::move(*shaderPayload));
      if (!shaderHandle.isValid()) {
        return makeFailedParse(
            table, LX_core::SceneResourceType::RenderPathGraph,
            context.ownerUri, canonicalUri, kRenderPathGraphParserName,
            {"failed to register Shader dependency '" +
             pass.shaderUri.string() + "' for RenderPathGraph '" +
             canonicalUri.string() + "'"});
      }
    }
  }

  LX_core::RenderPathGraphHandle handle;
  try {
    handle = table.registerRenderPathGraph(canonicalUri, std::move(graph));
  } catch (const std::exception &error) {
    return makeFailedParse(table, LX_core::SceneResourceType::RenderPathGraph,
                           context.ownerUri, canonicalUri,
                           kRenderPathGraphParserName, {error.what()});
  }
  if (!handle.isValid()) {
    return makeFailedParse(
        table, LX_core::SceneResourceType::RenderPathGraph, context.ownerUri,
        canonicalUri, kRenderPathGraphParserName,
        {"render path graph payload registration failed"});
  }

  ParsedSceneResource parsed;
  parsed.identity = table.metadataHandle(handle);
  if (const auto *metadata = table.findResourceMetadata(parsed.identity)) {
    parsed.metadata = *metadata;
  }
  return parsed;
}

} // namespace

void registerRenderResourceParsers(SceneResourceParserRegistry &registry) {
  registry.registerParser(
      LX_core::SceneResourceType::RenderFeature, ".render-feature.yaml",
      kRenderFeatureParserName, parseRenderFeatureIntoTable);
  registry.registerParser(
      LX_core::SceneResourceType::RenderPathGraph, ".render-path.yaml",
      kRenderPathGraphParserName, parseRenderPathGraphIntoTable);
}

} // namespace LX_infra
