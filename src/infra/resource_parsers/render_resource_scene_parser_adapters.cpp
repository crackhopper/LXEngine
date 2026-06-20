#include "infra/resource_parsers/render_resource_scene_parser_adapters.hpp"

#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/render_path_feature_validation.hpp"
#include "infra/resource_parsers/render_feature_resource_parser.hpp"
#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"
#include "infra/resource_parsers/render_path_shader_resolver.hpp"
#include "infra/resource_parsers/texture_resource_parser.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
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

[[nodiscard]] LX_core::FrameGraph
makeFeatureValidationFrameGraph(const LX_core::RenderPathGraph &graph) {
  LX_core::FrameGraph frameGraph;
  for (const LX_core::RenderPassNode &node : graph.passes) {
    LX_core::FramePass pass;
    pass.name = LX_core::StringID(node.id);
    pass.attachments = node.attachments;
    for (const std::string &target : node.targets) {
      const auto attachment = std::find_if(
          node.attachments.begin(), node.attachments.end(),
          [&](const LX_core::RenderPathAttachmentContract &candidate) {
            return candidate.target == target;
          });
      const bool isDepthTarget =
          attachment != node.attachments.end() && attachment->depth;
      pass.writes.push_back(LX_core::FrameGraphWrite{
          isDepthTarget
              ? LX_core::FrameGraphResourceRef::depthAttachment(
                    LX_core::StringID(target))
              : LX_core::FrameGraphResourceRef::colorAttachment(
                    LX_core::StringID(target)),
          node.writeMode,
      });
    }
    frameGraph.addPass(std::move(pass));
  }
  return frameGraph;
}

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

[[nodiscard]] std::optional<LX_core::IShaderSharedPtr> compileShaderPayload(
    const LX_core::ResourceUri &graphUri, const LX_core::ResourceUri &shaderUri,
    const std::vector<LX_core::ResourceUri> &sourceUris,
    std::vector<std::string> &diagnostics,
    const std::vector<LX_core::ShaderVariant> &variants = {}) {
  std::vector<LX_core::ShaderStageCode> stages;
  for (const LX_core::ResourceUri &sourceUri : sourceUris) {
    const std::filesystem::path sourcePath = pathFromUri(sourceUri);
    LX_infra::CompileResult compiled =
        LX_infra::ShaderCompiler::compileFile(sourcePath, variants);
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
  std::vector<LX_core::ShaderSpecializationConstantInfo>
      specializationConstants;
  try {
    bindings = LX_infra::ShaderReflector::reflect(stages);
    vertexInputs = LX_infra::ShaderReflector::reflectVertexInputs(stages);
    specializationConstants =
        LX_infra::ShaderReflector::reflectSpecializationConstants(stages);
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
      std::move(specializationConstants), shaderUri.string());
}

[[nodiscard]] std::optional<LX_core::ResourceUri>
representativeMaterialContractSource(const LX_core::RenderPassNode &pass) {
  const auto allowsType = [&](std::string_view type) {
    return pass.input.material.types.empty() ||
           std::find(pass.input.material.types.begin(),
                     pass.input.material.types.end(), std::string(type)) !=
               pass.input.material.types.end();
  };
  struct ContractCandidate final {
    const char *type;
    const char *uri;
  };
  constexpr ContractCandidate kCandidates[] = {
      {"standard-pbr",
       "assets://shaders/glsl/common/materials/standard_pbr.contract.glsl"},
      {"matte", "assets://shaders/glsl/common/materials/matte.contract.glsl"},
      {"uber", "assets://shaders/glsl/common/materials/uber.contract.glsl"},
      {"metal", "assets://shaders/glsl/common/materials/metal.contract.glsl"},
      {"substrate",
       "assets://shaders/glsl/common/materials/substrate.contract.glsl"},
      {"unlit-texture",
       "assets://shaders/glsl/common/materials/unlit_texture.contract.glsl"},
  };
  for (const ContractCandidate &candidate : kCandidates) {
    if (allowsType(candidate.type)) {
      return LX_core::ResourceUri(candidate.uri);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<LX_core::IShaderSharedPtr>
compileRepresentativeMaterialVariantShaderPayload(
    const LX_core::ResourceUri &graphUri, const LX_core::RenderPassNode &pass,
    const std::vector<LX_core::ResourceUri> &sourceUris,
    std::vector<std::string> &diagnostics) {
  const auto materialContract = representativeMaterialContractSource(pass);
  if (!materialContract.has_value()) {
    diagnostics.push_back("RenderPathGraph '" + graphUri.string() + "' pass '" +
                          pass.id + "' shader '" + pass.shaderUri.string() +
                          "' requires LX_MATERIAL_CONTRACT_SOURCE but no "
                          "representative material contract is available");
    return std::nullopt;
  }

  return compileShaderPayload(
      graphUri, pass.shaderUri, sourceUris, diagnostics,
      {LX_core::ShaderVariant{
          .macroName = "LX_MATERIAL_CONTRACT_SOURCE",
          .enabled = true,
          .materialContractSource = *materialContract,
      }});
}

[[nodiscard]] std::optional<bool> shaderRequiresMaterialSourceVariant(
    const LX_core::ResourceUri &graphUri, const LX_core::ResourceUri &shaderUri,
    const std::vector<LX_core::ResourceUri> &sourceUris,
    std::vector<std::string> &diagnostics) {
  for (const LX_core::ResourceUri &sourceUri : sourceUris) {
    std::string readDiagnostic;
    const auto text = readTextFile(sourceUri, readDiagnostic);
    if (!text.has_value()) {
      diagnostics.push_back(
          "RenderPathGraph '" + graphUri.string() +
          "' failed to inspect Shader '" + shaderUri.string() + "' source '" +
          sourceUri.string() + "': " + readDiagnostic);
      return std::nullopt;
    }
    if (text->find("LX_MATERIAL_CONTRACT_SOURCE") != std::string::npos) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::optional<bool> shaderProvidesRayHitDispatch(
    const LX_core::ResourceUri &graphUri, const LX_core::ResourceUri &shaderUri,
    const std::vector<LX_core::ResourceUri> &sourceUris,
    std::vector<std::string> &diagnostics) {
  for (const LX_core::ResourceUri &sourceUri : sourceUris) {
    std::string readDiagnostic;
    const auto text = readTextFile(sourceUri, readDiagnostic);
    if (!text.has_value()) {
      diagnostics.push_back(
          "RenderPathGraph '" + graphUri.string() +
          "' failed to inspect Shader '" + shaderUri.string() + "' source '" +
          sourceUri.string() + "': " + readDiagnostic);
      return std::nullopt;
    }
    if (text->find("lxDispatchRadianceHit") != std::string::npos) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool passDeclaresMaterialBsdf(
    const LX_core::RenderPassNode &pass) {
  return std::find(pass.sources.begin(), pass.sources.end(), "material.bsdf") !=
         pass.sources.end();
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

  for (auto &[parameterName, parameter] :
       parsedFeature.renderFeature->parameters) {
    if (parameter.uri.empty() ||
        parameter.uri.string().rfind("builtin:", 0) == 0) {
      continue;
    }
    if (parameter.kind != "textureCube" && parameter.kind != "texture2D" &&
        parameter.kind != "texture3D") {
      continue;
    }

    const LX_core::ResourceUri textureUri =
        canonicalResourceUri(canonicalUri, parameter.uri);
    TextureResourceParser textureParser;
    const auto texture = textureParser.parse(
        table, textureUri,
        SceneResourceParseContext{
            .ownerUri = {},
            .textureContent = LX_core::TextureContent::Environment,
        });
    if (!texture.identity.isValid() ||
        texture.metadata.state == LX_core::ResourceState::Failed) {
      std::vector<std::string> diagnostics;
      diagnostics.push_back("failed to load RenderFeature texture parameter '" +
                            parameterName + "' resource '" +
                            textureUri.string() + "'");
      diagnostics.insert(diagnostics.end(), texture.diagnostics.begin(),
                         texture.diagnostics.end());
      return makeFailedParse(
          table, LX_core::SceneResourceType::RenderFeature, context.ownerUri,
          canonicalUri, kRenderFeatureParserName, std::move(diagnostics));
    }
    parameter.uri = texture.metadata.uri;
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
      const auto shaderSourceUris = resolveRenderPathShaderSourceUris(
          canonicalUri, pass.id, pass.shaderUri);
      if (!shaderSourceUris.success()) {
        LX_core::ResourceMetadata failedShader;
        failedShader.type = LX_core::SceneResourceType::Shader;
        failedShader.uri = pass.shaderUri;
        failedShader.state = LX_core::ResourceState::Failed;
        for (const std::string &diagnostic : shaderSourceUris.diagnostics) {
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
            shaderSourceUris.diagnostics);
      }
      std::vector<std::string> shaderDiagnostics;
      const std::optional<bool> requiresMaterialSourceVariant =
          shaderRequiresMaterialSourceVariant(canonicalUri, pass.shaderUri,
                                              shaderSourceUris.sourceUris,
                                              shaderDiagnostics);
      if (!requiresMaterialSourceVariant.has_value()) {
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
      const std::optional<bool> providesRayHitDispatch =
          shaderProvidesRayHitDispatch(canonicalUri, pass.shaderUri,
                                       shaderSourceUris.sourceUris,
                                       shaderDiagnostics);
      if (!providesRayHitDispatch.has_value()) {
        return makeFailedParse(
            table, LX_core::SceneResourceType::RenderPathGraph,
            context.ownerUri, canonicalUri, kRenderPathGraphParserName,
            shaderDiagnostics);
      }
      const bool declaresMaterialBsdf = passDeclaresMaterialBsdf(pass);
      if (*requiresMaterialSourceVariant && !declaresMaterialBsdf) {
        return makeFailedParse(
            table, LX_core::SceneResourceType::RenderPathGraph,
            context.ownerUri, canonicalUri, kRenderPathGraphParserName,
            {"RenderPathGraph '" + canonicalUri.string() + "' pass '" +
             pass.id + "' shader '" + pass.shaderUri.string() +
             "' requires LX_MATERIAL_CONTRACT_SOURCE but the pass does not "
             "declare material.bsdf in sources"});
      }
      if (!*requiresMaterialSourceVariant && declaresMaterialBsdf &&
          !*providesRayHitDispatch) {
        return makeFailedParse(
            table, LX_core::SceneResourceType::RenderPathGraph,
            context.ownerUri, canonicalUri, kRenderPathGraphParserName,
            {"RenderPathGraph '" + canonicalUri.string() + "' pass '" +
             pass.id + "' declares material.bsdf but shader '" +
             pass.shaderUri.string() +
             "' does not include LX_MATERIAL_CONTRACT_SOURCE"});
      }
      std::optional<LX_core::IShaderSharedPtr> shaderPayload;
      if (*requiresMaterialSourceVariant) {
        shaderPayload = compileRepresentativeMaterialVariantShaderPayload(
            canonicalUri, pass, shaderSourceUris.sourceUris, shaderDiagnostics);
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
      } else {
        shaderPayload = compileShaderPayload(canonicalUri, pass.shaderUri,
                                             shaderSourceUris.sourceUris,
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
      }
      const LX_core::ShaderHandle shaderHandle =
          table.registerShaderResource(
              pass.shaderUri, shaderSourceUris.sourceUris,
              shaderPayload.has_value() ? std::move(*shaderPayload) : nullptr,
              *requiresMaterialSourceVariant);
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

  try {
    const LX_core::FrameGraph frameGraph = makeFeatureValidationFrameGraph(graph);
    const auto diagnostics = LX_core::validateRenderPathFeatureCombination(
        graph, frameGraph, table);
    std::vector<std::string> fatalDiagnostics;
    for (const auto &diagnostic : diagnostics) {
      if (!diagnostic.fatal) {
        continue;
      }
      std::cerr << diagnostic.message << '\n';
      fatalDiagnostics.push_back(diagnostic.message);
    }
    if (!fatalDiagnostics.empty()) {
      return makeFailedParse(
          table, LX_core::SceneResourceType::RenderPathGraph, context.ownerUri,
          canonicalUri, kRenderPathGraphParserName,
          std::move(fatalDiagnostics));
    }
  } catch (const std::exception &error) {
    return makeFailedParse(table, LX_core::SceneResourceType::RenderPathGraph,
                           context.ownerUri, canonicalUri,
                           kRenderPathGraphParserName, {error.what()});
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
