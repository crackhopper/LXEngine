#include "core/asset/material_instance.hpp"
#include "core/asset/material_template.hpp"
#include "core/asset/render_effect.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "core/utils/env.hpp"
#include "infra/material_loader/material_contract_reflector.hpp"
#include "infra/resource_parsers/material_source_variant_resolver.hpp"
#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"
#include "infra/resource_parsers/render_resource_scene_parser_adapters.hpp"
#include "infra/resource_parsers/scene_resource_parser_registry.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

#ifndef LXE_SOURCE_DIR
#define LXE_SOURCE_DIR ""
#endif

int g_failures = 0;
constexpr std::string_view kRenderPathNamespace = "render_paths/";

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

bool isRepoRoot(const fs::path &path) {
  return fs::exists(path / "assets/render_paths/forward_main.render-path.yaml") &&
         fs::exists(path / "src/demos/lxe_editor/scene_runtime.cpp");
}

fs::path findRepoRoot() {
  const fs::path configured{LXE_SOURCE_DIR};
  if (!configured.empty() && isRepoRoot(configured)) {
    return fs::canonical(configured);
  }

  const std::vector<fs::path> starts{
      fs::absolute(__FILE__),
      fs::current_path(),
  };
  for (const fs::path &start : starts) {
    fs::path probe = fs::is_directory(start) ? start : start.parent_path();
    for (fs::path current = probe; !current.empty();
         current = current.parent_path()) {
      if (isRepoRoot(current)) {
        return fs::canonical(current);
      }
      if (current == current.root_path()) {
        break;
      }
    }
  }
  return fs::current_path();
}

std::string readTextFile(const fs::path &path) {
  std::ifstream in(path);
  EXPECT(in.is_open(), "failed to open " + path.generic_string());
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

bool textContains(std::string_view text, std::string_view needle) {
  return text.find(needle) != std::string_view::npos;
}

std::string legacyTechniqueNamespace() {
  return std::string("tech") + "niques";
}

std::string legacyForwardPbrUri() {
  return legacyTechniqueNamespace() + "/Forward/pbr";
}

std::string legacyForwardPattern() {
  return legacyTechniqueNamespace() + "/Forward";
}

std::string legacyDeferredPattern() {
  return legacyTechniqueNamespace() + "/Deferred";
}

std::string legacyDefaultTechniqueField() {
  return std::string("default") + "Technique";
}

std::string legacyTechniquesRootKey() {
  return legacyTechniqueNamespace() + ":";
}

std::optional<std::string_view> functionBody(std::string_view source,
                                             std::string_view signature) {
  const std::size_t signaturePos = source.find(signature);
  if (signaturePos == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t openBrace = source.find('{', signaturePos);
  if (openBrace == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t depth = 0;
  for (std::size_t i = openBrace; i < source.size(); ++i) {
    if (source[i] == '{') {
      ++depth;
      continue;
    }
    if (source[i] != '}') {
      continue;
    }
    --depth;
    if (depth == 0) {
      return source.substr(openBrace + 1, i - openBrace - 1);
    }
  }
  return std::nullopt;
}

bool textContainsLegacyUriMigration(std::string_view text) {
  return textContains(text, legacyForwardPbrUri()) &&
         textContains(text, kRenderPathNamespace);
}

bool diagnosticsContainLegacyUriMigration(
    const std::vector<std::string> &diagnostics) {
  for (const std::string &diagnostic : diagnostics) {
    if (textContainsLegacyUriMigration(diagnostic)) {
      return true;
    }
  }
  return false;
}

bool hasDiagnosticContainingLegacyUriMigration(
    const LX_infra::ParsedSceneResource &parsed) {
  if (diagnosticsContainLegacyUriMigration(parsed.diagnostics)) {
    return true;
  }
  for (const LX_core::ResourceDiagnostic &diagnostic :
       parsed.metadata.diagnostics) {
    if (textContainsLegacyUriMigration(diagnostic.message)) {
      return true;
    }
  }
  return false;
}

std::string normalizeAuditText(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (const unsigned char ch : text) {
    if (std::isspace(ch) || ch == '"' || ch == '\'' || ch == '(' ||
        ch == ')' || ch == '{' || ch == '}' || ch == '[' || ch == ']' ||
        ch == '+') {
      continue;
    }
    normalized.push_back(static_cast<char>(ch));
  }
  return normalized;
}

LX_core::ResourceUri writeTempGraph(const std::string &name,
                                    const std::string &contents) {
  const fs::path path = fs::temp_directory_path() / name;
  std::ofstream out(path);
  out << contents;
  return LX_core::ResourceUri("file://" + path.generic_string());
}

LX_core::MaterialInstanceUniquePtr makeStandardPbrSourceMaterial() {
  const LX_core::ResourceUri sourceUri(
      "assets://shaders/glsl/common/materials/standard_pbr.contract.glsl");
  const auto reflected =
      LX_infra::loadAndReflectMaterialContractSource(sourceUri);
  EXPECT(reflected.reflection.has_value(),
         "test standard-pbr material source reflection should succeed");

  auto material = LX_core::MaterialInstance::createUnique(
      LX_core::MaterialTemplate::create("073-d-standard-pbr-material"));
  material->setBsdfType("standard-pbr");
  material->setMaterialSourceUri(sourceUri);
  if (reflected.reflection.has_value()) {
    material->setMaterialSourceReflectionHash(
        reflected.reflection->reflectionHash);
    material->setMaterialSourceSignature(
        reflected.reflection->sourceSignature());
    material->setMaterialContractReflection(*reflected.reflection);
  }
  return material;
}

LX_core::RenderPathGraph makeLegacyTechniqueGraph() {
  LX_core::RenderPassNode forward;
  forward.id = "Forward";
  forward.shaderUri = LX_core::ResourceUri(legacyForwardPbrUri());
  forward.stage = LX_core::RenderPassStage::Raster;
  forward.dispatch = LX_core::RenderPassDispatch::Draw;
  forward.filters.bsdfTypes = {"standard-pbr"};
  forward.renderingMode = LX_core::RenderPathNodeRenderingMode::Dynamic;

  LX_core::RenderPathGeometryContract geometry;
  geometry.vertex = LX_core::RenderPathGeometryVertexContract::PositionOnly;
  geometry.topology = LX_core::PrimitiveTopology::TriangleList;
  forward.geometry = geometry;

  LX_core::RenderPathAttachmentContract colorAttachment;
  colorAttachment.target = "hdr.color";
  colorAttachment.format = LX_core::ImageFormat::RGBA16Float;
  colorAttachment.samples = 1;
  colorAttachment.layers = 1;

  LX_core::RenderPathAttachmentContract depthAttachment;
  depthAttachment.target = "depth.main";
  depthAttachment.format = LX_core::ImageFormat::D32Float;
  depthAttachment.samples = 1;
  depthAttachment.layers = 1;
  depthAttachment.depth = true;
  forward.attachments = {colorAttachment, depthAttachment};

  forward.sources = {"geometry.vertex", "geometry.index", "material.bsdf",
                     "scene.camera", "scene.lights"};
  forward.targets = {"hdr.color", "depth.main"};

  LX_core::RenderState renderState;
  renderState.cullMode = LX_core::CullMode::Back;
  renderState.depthTestEnable = true;
  renderState.depthWriteEnable = true;
  renderState.depthOp = LX_core::CompareOp::LessEqual;
  forward.renderState = renderState;

  LX_core::RenderPathGraph graph;
  graph.name = "LegacyTechniqueGraph";
  graph.renderPath = LX_core::RenderPath::Forward;
  graph.passes.push_back(forward);
  return graph;
}

void testDefaultGraphAssetsUseRenderPathShaderUris(const fs::path &repoRoot) {
  const fs::path assets[] = {
      repoRoot / "assets/render_paths/forward_main.render-path.yaml",
      repoRoot / "assets/render_paths/forward_bloom.render-path.yaml",
      repoRoot / "assets/render_paths/deferred_main.render-path.yaml",
      repoRoot / "assets/render_paths/deferred_bloom.render-path.yaml",
  };

  LX_infra::RenderPathGraphResourceParser parser;
  for (const fs::path &path : assets) {
    const std::string text = readTextFile(path);
    EXPECT(!textContains(text, "techniques/"),
           path.generic_string() + " must not contain techniques/ URIs");

    const auto parsed =
        parser.parse(LX_core::ResourceUri(path.generic_string()), text);
    EXPECT(parsed.renderPathGraph.has_value(),
           path.generic_string() + " should parse as RenderPathGraph");
    if (!parsed.renderPathGraph.has_value()) {
      continue;
    }

    for (const LX_core::RenderPassNode &pass :
         parsed.renderPathGraph->passes) {
      const bool usesMaterialSource =
          std::find(pass.sources.begin(), pass.sources.end(),
                    "material.bsdf") != pass.sources.end();
      if (usesMaterialSource || pass.id == "Shadow" ||
          pass.id == "DeferredLighting") {
        EXPECT(pass.shaderUri.string().rfind("render_paths/", 0) == 0,
               path.generic_string() + " pass " + pass.id +
                   " must use render_paths/... shader URI");
      }
    }
  }
}

void testLegacyTechniqueUriRejectedByResourceParser() {
  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  LX_core::SceneResourceTable table;
  const std::string rejectedUri = legacyForwardPbrUri();
  const std::string graphText = std::string(R"yaml(
schema: lxe.render-path-graph.v1
name: LegacyShaderUri
renderPath: Forward
passes:
  - id: Forward
    stage: raster
    dispatch: draw
    shader: )yaml") +
                                rejectedUri + R"yaml(
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16F
          samples: 1
          layers: 1
        - target: depth.main
          format: D32Float
          samples: 1
          layers: 1
          depth: true
    geometry:
      vertex: position-only
      topology: triangle-list
    sources: [geometry.vertex, geometry.index, material.bsdf, scene.camera]
    targets: [hdr.color, depth.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)yaml";
  const LX_core::ResourceUri graphUri = writeTempGraph(
      "lxe_073d_legacy_shader_uri.render-path.yaml", graphText);

  const auto parsed = registry.parse(
      table, LX_core::SceneResourceType::RenderPathGraph, graphUri,
      LX_infra::SceneResourceParseContext{});

  EXPECT(!parsed.identity.isValid() ||
             parsed.metadata.state == LX_core::ResourceState::Failed,
         "legacy techniques/... shader URI should fail graph resource parse");
  EXPECT(hasDiagnosticContainingLegacyUriMigration(parsed),
         "graph resource parse diagnostic should include rejected URI " +
             rejectedUri + " and replacement namespace " +
             std::string(kRenderPathNamespace));
}

void testLegacyTechniqueUriRejectedByMaterialSourceVariantResolver() {
  LX_core::SceneResourceTable table;
  const LX_core::MaterialHandle materialHandle = table.registerMaterialInstance(
      LX_core::ResourceUri("memory://073-d-standard-pbr.material"),
      makeStandardPbrSourceMaterial());
  EXPECT(materialHandle.isValid(), "source material should register");

  LX_core::RenderPathGraph graph = makeLegacyTechniqueGraph();
  const std::string rejectedUri = legacyForwardPbrUri();
  try {
    const auto resolved = LX_infra::resolveMaterialSourceVariants(
        table, graph,
        LX_core::ResourceUri("memory://073-d-legacy-technique.render-path"));
    EXPECT(!resolved.success,
           "legacy techniques/... shader URI should fail material source "
           "variant resolution");
    EXPECT(diagnosticsContainLegacyUriMigration(resolved.diagnostics),
           "material source variant diagnostic should include rejected URI " +
               rejectedUri + " and replacement namespace " +
               std::string(kRenderPathNamespace));
  } catch (const std::exception &error) {
    EXPECT(textContainsLegacyUriMigration(error.what()),
           std::string("material source variant exception should include "
                       "rejected URI ") +
               rejectedUri + " and replacement namespace " +
               std::string(kRenderPathNamespace) + ": " +
               error.what());
  }
}

void testRendererNamesLoadedScenePipelinePreparationPhase(
    const fs::path &repoRoot) {
  const fs::path rendererPath =
      repoRoot / "src/backend/vulkan/vulkan_realtime_renderer.cpp";
  const std::string rendererSource = readTextFile(rendererPath);
  const std::string rendererLabel = rendererPath.generic_string();
  const std::optional<std::string_view> initSceneBody =
      functionBody(rendererSource, "void initScene(SceneSharedPtr _scene)");
  EXPECT(initSceneBody.has_value(),
         rendererLabel + " must define initScene(SceneSharedPtr _scene)");

  if (initSceneBody.has_value()) {
    const std::string_view body = *initSceneBody;
    const auto compilePos = body.find("m_frameGraph.compile()");
    const auto attachResourcesPos =
        body.find("attachFrameGraphSampledResources()");
    const auto uploadSyncPos = body.find("syncRenderUploadPlan(pass.queue)");
    const auto collectGarbagePos =
        body.find("resourceManager().collectGarbage();");
    const auto prepareCallPos = body.find("preparePipelinesForLoadedScene();");
    EXPECT(compilePos != std::string_view::npos,
           rendererLabel + " initScene must compile the FrameGraph");
    EXPECT(attachResourcesPos != std::string_view::npos,
           rendererLabel +
               " initScene must attach sampled resources before preparation");
    EXPECT(uploadSyncPos != std::string_view::npos,
           rendererLabel +
               " initScene must sync upload plans before preparation");
    EXPECT(collectGarbagePos != std::string_view::npos,
           rendererLabel +
               " initScene must collect garbage before pipeline preparation");
    EXPECT(prepareCallPos != std::string_view::npos,
           rendererLabel +
               " initScene must call preparePipelinesForLoadedScene()");
    if (compilePos != std::string_view::npos &&
        attachResourcesPos != std::string_view::npos &&
        uploadSyncPos != std::string_view::npos &&
        collectGarbagePos != std::string_view::npos &&
        prepareCallPos != std::string_view::npos) {
      EXPECT(compilePos < attachResourcesPos &&
                 attachResourcesPos < uploadSyncPos &&
                 uploadSyncPos < collectGarbagePos &&
                 collectGarbagePos < prepareCallPos,
             rendererLabel +
                 " initScene must prepare pipelines after compile, sampled "
                 "resource attachment, upload sync, and garbage collection");
    }
  }

  const std::optional<std::string_view> prepareFunctionBody =
      functionBody(rendererSource, "void preparePipelinesForLoadedScene()");
  EXPECT(prepareFunctionBody.has_value(),
         rendererLabel + " must define preparePipelinesForLoadedScene()");
  if (prepareFunctionBody.has_value()) {
    const std::string_view body = *prepareFunctionBody;
    const auto collectDescsPos =
        body.find("m_frameGraph.collectAllPipelineBuildDescs()");
    const auto preloadPipelinesPos =
        body.find("resourceManager().preloadPipelines(pipelineDescs)");
    EXPECT(collectDescsPos != std::string_view::npos,
           rendererLabel +
               " preparePipelinesForLoadedScene() must collect "
               "PipelineBuildDesc values from the FrameGraph");
    EXPECT(preloadPipelinesPos != std::string_view::npos,
           rendererLabel +
               " preparePipelinesForLoadedScene() must preload collected "
               "pipeline descriptions");
    if (collectDescsPos != std::string_view::npos &&
        preloadPipelinesPos != std::string_view::npos) {
      EXPECT(collectDescsPos < preloadPipelinesPos,
             rendererLabel +
                 " preparePipelinesForLoadedScene() must collect FrameGraph "
                 "PipelineBuildDesc values before preloading pipelines");
    }
  }
}

bool shouldScanFile(const fs::path &path) {
  const std::string filename = path.filename().string();
  if (filename == "test_073d_render_path_hard_cut.cpp") {
    return false;
  }
  const std::string ext = path.extension().string();
  return ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" ||
         ext == ".cc" || ext == ".cxx" || ext == ".hh" || ext == ".inl" ||
         ext == ".yaml" || ext == ".yml" || ext == ".glsl" ||
         ext == ".frag" || ext == ".vert" || ext == ".comp";
}

void testProductionOldTokenAudit(const fs::path &repoRoot) {
  const fs::path roots[] = {
      repoRoot / "src",
      repoRoot / "assets",
  };
  struct ForbiddenPattern {
    std::string normalizedToken;
    std::string_view description;
  };
  // Generic techniques/ stays allowed until OfflineRT moves in REQ-073-g/h.
  // This audit only cuts realtime Forward/Deferred paths and material fields.
  const std::vector<ForbiddenPattern> forbiddenPatterns{
      {legacyForwardPattern(),
       "legacy Forward shader URI or split path construction"},
      {legacyDeferredPattern(),
       "legacy Deferred shader URI or split path construction"},
      {legacyDefaultTechniqueField(),
       "material-local old default technique field"},
      {legacyTechniquesRootKey(), "material-local old technique map key"},
  };

  for (const fs::path &root : roots) {
    if (!fs::exists(root)) {
      continue;
    }
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
      if (!it->is_regular_file() || !shouldScanFile(it->path())) {
        continue;
      }
      const std::string relativePath =
          fs::relative(it->path(), repoRoot).generic_string();
      const std::string text = readTextFile(it->path());
      const std::string normalizedText = normalizeAuditText(text);
      const std::string normalizedRelativePath = normalizeAuditText(relativePath);
      for (const ForbiddenPattern &pattern : forbiddenPatterns) {
        EXPECT(!textContains(normalizedRelativePath, pattern.normalizedToken),
               relativePath + " path contains forbidden pattern " +
                   std::string(pattern.normalizedToken) + " (" +
                   std::string(pattern.description) + ")");
        EXPECT(!textContains(normalizedText, pattern.normalizedToken),
               relativePath + " contains forbidden pattern " +
                   std::string(pattern.normalizedToken) + " (" +
                   std::string(pattern.description) + ")");
      }
    }
  }
}

} // namespace

int main() {
  expSetEnvVK();
  const fs::path repoRoot = findRepoRoot();
  std::error_code ec;
  fs::current_path(repoRoot, ec);
  EXPECT(!ec, "failed to set current path to repo root " +
                  repoRoot.generic_string());

  testDefaultGraphAssetsUseRenderPathShaderUris(repoRoot);
  testLegacyTechniqueUriRejectedByResourceParser();
  testLegacyTechniqueUriRejectedByMaterialSourceVariantResolver();
  testRendererNamesLoadedScenePipelinePreparationPhase(repoRoot);
  testProductionOldTokenAudit(repoRoot);

  if (g_failures != 0) {
    std::cerr << g_failures << " 073-d render path hard-cut checks failed\n";
    return 1;
  }
  std::cout << "OK: 073-d render path hard-cut checks passed\n";
  return 0;
}
