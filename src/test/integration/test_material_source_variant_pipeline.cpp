#include "core/asset/material_contract.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/material_template.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/resource/resource_uri.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "core/utils/env.hpp"
#include "infra/material_loader/material_contract_reflector.hpp"
#include "infra/resource_parsers/material_source_variant_resolver.hpp"
#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"
#include "infra/resource_parsers/render_resource_scene_parser_adapters.hpp"
#include "infra/resource_parsers/scene_resource_parser_registry.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifndef LXE_SOURCE_DIR
#define LXE_SOURCE_DIR ""
#endif

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " "      \
                << msg << " (" #cond ")\n";                                  \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

bool diagnosticsContain(const std::vector<std::string> &diagnostics,
                        const std::string &needle) {
  for (const std::string &diagnostic : diagnostics) {
    if (diagnostic.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

LX_core::MaterialContractReflection makeReflection(std::string type,
                                                   std::string sourceUri,
                                                   std::string hash) {
  LX_core::MaterialContractReflection reflection;
  reflection.declaredType = std::move(type);
  reflection.sourceUri = LX_core::ResourceUri(std::move(sourceUri));
  reflection.reflectionHash = std::move(hash);
  reflection.storageAbiHash = "storage-v1";
  reflection.accessorAbiHash = "accessor-v1";
  reflection.parameters.push_back(LX_core::MaterialContractParameter{
      "Kd",
      true,
      {LX_core::MaterialContractParameterKind::Rgb,
       LX_core::MaterialContractParameterKind::Texture}});
  reflection.storageFields.push_back(LX_core::MaterialContractStorageField{
      .name = "baseColor",
      .type = LX_core::MaterialContractStorageFieldType::Vec4,
      .inputKind = LX_core::MaterialContractStorageInputKind::ParameterValue,
      .parameterName = "Kd",
      .defaultValue = LX_core::Vec4f{1.0f, 1.0f, 1.0f, 1.0f},
  });
  return reflection;
}

LX_infra::ParsedRenderPathGraphResource parseRenderPath(std::string yamlText) {
  LX_infra::RenderPathGraphResourceParser parser;
  return parser.parse(LX_core::ResourceUri("memory://073-c-test.render-path"),
                      yamlText);
}

LX_core::ResourceUri writeTempRenderPathGraph(const std::string &fileName,
                                              const std::string &contents) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / fileName;
  std::ofstream file(path);
  file << contents;
  return LX_core::ResourceUri("file://" + path.generic_string());
}

LX_core::MaterialInstanceUniquePtr makeStandardPbrSourceMaterial(
    std::string name = "standard-pbr-test-material") {
  const LX_core::ResourceUri sourceUri(
      "assets://shaders/glsl/common/materials/standard_pbr.contract.glsl");
  const auto reflected =
      LX_infra::loadAndReflectMaterialContractSource(sourceUri);
  EXPECT(reflected.reflection.has_value(),
         "test standard-pbr source reflection should succeed");

  auto material =
      LX_core::MaterialInstance::createUnique(LX_core::MaterialTemplate::create(
          std::move(name)));
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

LX_core::MaterialInstanceUniquePtr
makeManualSourceMaterial(std::string type, std::string sourceUri,
                         std::string reflectionHash) {
  auto reflection =
      makeReflection(type, sourceUri, std::move(reflectionHash));
  auto material =
      LX_core::MaterialInstance::createUnique(LX_core::MaterialTemplate::create(
          type + "-manual-test-material"));
  material->setBsdfType(type);
  material->setMaterialSourceUri(reflection.sourceUri);
  material->setMaterialSourceReflectionHash(reflection.reflectionHash);
  material->setMaterialSourceSignature(reflection.sourceSignature());
  material->setMaterialContractReflection(std::move(reflection));
  return material;
}

LX_core::RenderPathGraph makeForwardStandardPbrGraph() {
  const auto parsed = parseRenderPath(R"yaml(
schema: lxe.render-path-graph.v1
name: ForwardStandardPbr
renderPath: Forward
passes:
  - id: ForwardOpaque
    stage: raster
    dispatch: draw
    shader: techniques/Forward/pbr
    filters:
      bsdf: [standard-pbr]
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
    sources: [geometry.vertex, geometry.index, material.bsdf, scene.camera, scene.lights]
    targets: [hdr.color, depth.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)yaml");

  EXPECT(parsed.renderPathGraph.has_value(),
         "test forward standard-pbr graph should parse");
  return parsed.renderPathGraph.value_or(LX_core::RenderPathGraph{});
}

void testPipelineKeyIgnoresObjectAndTargetAxes() {
  const LX_core::StringID materialTypeVariant(
      "MaterialTypeVariant(type=standard-pbr,source=standard_pbr)");
  const LX_core::StringID renderPathNodeSignature(
      "RenderPathNodeSignature(pass=ForwardOpaque)");

  const LX_core::PipelineKey key =
      LX_core::PipelineKey::build(materialTypeVariant, renderPathNodeSignature);
  const std::string debug =
      LX_core::GlobalStringTable::get().toDebugString(key.id);
  EXPECT(debug.find("MaterialTypeVariant") != std::string::npos,
         "debug key should include material type variant");
  EXPECT(debug.find("RenderPathNodeSignature") != std::string::npos,
         "debug key should include RenderPathNodeSignature");
  EXPECT(debug.find("ObjectRender(") == std::string::npos,
         "ObjectRender must not be a PipelineKey axis");
  EXPECT(debug.find("TargetRender(") == std::string::npos,
         "TargetRender must not be a separate PipelineKey axis");
}

void testRenderPassNodeRequiresRenderingAndGeometry() {
  const auto parsed = parseRenderPath(R"yaml(
schema: lxe.render-path-graph.v1
name: MissingExplicitContracts
renderPath: Forward
passes:
  - id: ForwardOpaque
    stage: raster
    dispatch: draw
    shader: techniques/Forward/pbr
    sources: [geometry.vertex, geometry.index, material.bsdf, scene.camera]
    targets: [hdr.color, depth.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)yaml");

  EXPECT(!parsed.renderPathGraph.has_value(),
         "raster draw passes must declare rendering and geometry contracts");
  EXPECT(diagnosticsContain(parsed.diagnostics, "rendering"),
         "diagnostic should name missing rendering contract");
  EXPECT(diagnosticsContain(parsed.diagnostics, "geometry"),
         "diagnostic should name missing geometry contract");
}

void testUnknownRenderPathResourceVocabularyFails() {
  const auto parsed = parseRenderPath(R"yaml(
schema: lxe.render-path-graph.v1
name: UnknownResourceVocabulary
renderPath: Forward
passes:
  - id: ForwardOpaque
    stage: raster
    dispatch: draw
    shader: techniques/Forward/pbr
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
    sources: [geometry.vertex, geometry.index, material.bsdf, unknown.resource]
    targets: [hdr.color, depth.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)yaml");

  EXPECT(!parsed.renderPathGraph.has_value(),
         "unknown RenderPath source/target vocabulary must fail parse");
  EXPECT(diagnosticsContain(parsed.diagnostics, "unknown.resource"),
         "diagnostic should name the unknown resource");
}

void testSameTypeDifferentSourceFails() {
  const LX_core::MaterialContractReflection matteA = makeReflection(
      "matte", "assets://shaders/glsl/common/materials/matte.contract.glsl",
      "matte-a");
  const LX_core::MaterialContractReflection matteB = makeReflection(
      "matte", "assets://shaders/glsl/common/materials/alt_matte.contract.glsl",
      "matte-b");

  const auto validated =
      LX_infra::validateMaterialContractReflectionSet({matteA, matteB});

  EXPECT(!validated.diagnostics.empty(),
         "one material type must not resolve to multiple source signatures");
  EXPECT(diagnosticsContain(validated.diagnostics, "matte"),
         "diagnostic should name the conflicted material type");
}

void testStandardPbrContractReflectsRequiredFields() {
  const auto reflected = LX_infra::loadAndReflectMaterialContractSource(
      LX_core::ResourceUri(
          "assets://shaders/glsl/common/materials/standard_pbr.contract.glsl"));

  EXPECT(reflected.reflection.has_value(),
         "standard-pbr contract source should reflect successfully");
  EXPECT(reflected.reflection.has_value() &&
             reflected.reflection->declaredType == "standard-pbr",
         "standard-pbr contract should declare type standard-pbr");
  if (!reflected.reflection.has_value()) {
    return;
  }

  const std::vector<std::string> requiredFields = {
      "baseColor",        "baseColorTexture", "metallic",
      "metallicRoughnessTexture",             "roughness",
      "normalTexture",    "occlusionTexture", "emissive",
      "emissiveTexture",  "alphaMode",        "alphaCutoff"};
  for (const std::string &field : requiredFields) {
    bool found = false;
    for (const LX_core::MaterialContractStorageField &storageField :
         reflected.reflection->storageFields) {
      if (storageField.name == field) {
        found = true;
        break;
      }
    }
    EXPECT(found, "standard-pbr storage should include " + field);
  }
}

void testVariantOnlyShaderNakedCompileFailsWithDiagnostic() {
  const std::filesystem::path shaderPath =
      std::filesystem::path(LXE_SOURCE_DIR) / "assets" / "shaders" / "glsl" /
      "techniques" / "Forward" / "pbr.frag";

  const auto compiled = LX_infra::ShaderCompiler::compileFile(shaderPath);
  EXPECT(!compiled.success,
         "variant-only material shader must fail when compiled without "
         "LX_MATERIAL_CONTRACT_SOURCE");
  EXPECT(compiled.errorMessage.find("LX_MATERIAL_CONTRACT_SOURCE") !=
             std::string::npos,
         "diagnostic should name LX_MATERIAL_CONTRACT_SOURCE");
}

void testResolverAttachesFinalShaderVariantToSourceMaterialPass() {
  LX_core::SceneResourceTable table;
  const LX_core::MaterialHandle materialHandle = table.registerMaterialInstance(
      LX_core::ResourceUri("memory://standard-pbr-test.material"),
      makeStandardPbrSourceMaterial());
  EXPECT(materialHandle.isValid(), "source material should register");

  LX_core::RenderPathGraph graph = makeForwardStandardPbrGraph();
  const auto resolved = LX_infra::resolveMaterialSourceVariants(
      table, graph, LX_core::ResourceUri("memory://forward-test.render-path"));

  EXPECT(resolved.success, "resolver should compile and reflect final variant");
  EXPECT(resolved.resolvedVariantCount == 1,
         "one standard-pbr material type should create one final variant");
  EXPECT(diagnosticsContain(resolved.diagnostics, "standard-pbr"),
         "diagnostics should name material type");
  EXPECT(diagnosticsContain(resolved.diagnostics, "RenderPathNodeSignature"),
         "diagnostics should expose RenderPathNodeSignature");

  const auto materialRef = table.resolve(materialHandle);
  EXPECT(materialRef.has_value(), "registered material should resolve");
  if (!materialRef.has_value()) {
    return;
  }

  const LX_core::StringID pass("ForwardOpaque");
  const LX_core::MaterialInstance &material = materialRef->get();
  EXPECT(material.isPassEnabled(pass),
         "resolver should enable the graph pass on the source material");
  const auto shaderProgram = material.getPassShaderProgram(pass);
  EXPECT(shaderProgram.has_value(),
         "resolver should attach a shader program to the material pass");
  if (!shaderProgram.has_value()) {
    return;
  }

  EXPECT(shaderProgram->get().hasEnabledVariant("LX_MATERIAL_CONTRACT_SOURCE"),
         "final shader program should carry the material source include "
         "variant");
  EXPECT(shaderProgram->get().getShader() != nullptr,
         "final shader program should contain compiled/reflected payload");
  EXPECT(shaderProgram->get().getShader() != nullptr &&
             !shaderProgram->get().getShader()->getAllStages().empty(),
         "final shader payload should contain compiled stages");
  EXPECT(shaderProgram->get().getShader() != nullptr &&
             !shaderProgram->get().getShader()->getVertexInputs().empty(),
         "final shader reflection should contain vertex inputs");

  const std::string variantDebug =
      LX_core::GlobalStringTable::get().toDebugString(
          material.getMaterialTypeVariantSignature(shaderProgram->get()));
  EXPECT(variantDebug.find("standard-pbr") != std::string::npos,
         "material type variant should include material type");
  EXPECT(variantDebug.find("standard_pbr.contract.glsl") != std::string::npos,
         "material type variant should include material source URI");

  const LX_core::SceneResourceTableUploadView uploadView =
      table.buildUploadView();
  bool foundResolvedShaderVariant = false;
  for (const auto &shaderRef : uploadView.shaderResources) {
    const LX_core::ShaderResourceMetadata &shader = shaderRef.get();
    if (shader.uri == LX_core::ResourceUri("techniques/Forward/pbr")) {
      foundResolvedShaderVariant =
          shader.requiresMaterialSourceVariant &&
          shader.materialSourceVariants.size() == 1 &&
          shader.materialSourceVariants.front().shaderProgram.getShader() !=
              nullptr;
    }
  }
  EXPECT(foundResolvedShaderVariant,
         "upload view should expose final resolved material source shader "
         "variant metadata");
}

void testResolverNoOpsBeforeMaterialsAreLoaded() {
  LX_core::SceneResourceTable table;
  LX_core::RenderPathGraph graph = makeForwardStandardPbrGraph();
  const auto resolved = LX_infra::resolveMaterialSourceVariants(
      table, graph, LX_core::ResourceUri("memory://forward-empty.render-path"));

  EXPECT(resolved.success,
         "resolver should allow graph setup before scene materials load");
  EXPECT(resolved.resolvedVariantCount == 0,
         "empty scenes should not create unresolved material source variants");
  try {
    (void)table.buildUploadView();
  } catch (const std::exception &error) {
    EXPECT(false, std::string("empty scene upload view should not fail: ") +
                      error.what());
  }
}

void testResolverKeepsNonContractShadowShaderPlain() {
  constexpr std::string_view graphYaml = R"yaml(
schema: lxe.render-path-graph.v1
name: ShadowAndForwardStandardPbr
renderPath: Forward
passes:
  - id: Shadow
    stage: raster
    dispatch: draw
    shader: techniques/Forward/shadow_depth_only
    filters:
      bsdf: [standard-pbr]
    rendering:
      mode: dynamic
      attachments:
        - target: shadow.main
          format: D32Float
          samples: 1
          layers: 1
          depth: true
    geometry:
      vertex: position-only
      topology: triangle-list
    sources: [geometry.vertex, geometry.index, scene.camera, scene.lights]
    targets: [shadow.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
  - id: ForwardOpaque
    stage: raster
    dispatch: draw
    shader: techniques/Forward/pbr
    filters:
      bsdf: [standard-pbr]
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
    sources: [geometry.vertex, geometry.index, material.bsdf, scene.camera, scene.lights]
    targets: [hdr.color, depth.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)yaml";

  LX_core::SceneResourceTable table;
  const LX_core::MaterialHandle materialHandle = table.registerMaterialInstance(
      LX_core::ResourceUri("memory://standard-pbr-shadow-test.material"),
      makeStandardPbrSourceMaterial("standard-pbr-shadow-test-material"));
  EXPECT(materialHandle.isValid(), "source material should register");

  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  const LX_core::ResourceUri graphUri = writeTempRenderPathGraph(
      "lxe_shadow_plain_forward_variant.render-path.yaml",
      std::string(graphYaml));
  const auto parsedResource = registry.parse(
      table, LX_core::SceneResourceType::RenderPathGraph, graphUri,
      LX_infra::SceneResourceParseContext{});
  EXPECT(parsedResource.diagnostics.empty(),
         "shadow + forward test graph should parse without diagnostics");
  EXPECT(parsedResource.identity.isValid(),
         "shadow + forward test graph should register");

  LX_infra::RenderPathGraphResourceParser graphParser;
  const auto parsedGraph = graphParser.parse(graphUri, std::string(graphYaml));
  EXPECT(parsedGraph.renderPathGraph.has_value(),
         "shadow + forward graph should parse for resolver");
  if (!parsedGraph.renderPathGraph.has_value()) {
    return;
  }

  const auto resolved = LX_infra::resolveMaterialSourceVariants(
      table, *parsedGraph.renderPathGraph, graphUri);
  EXPECT(resolved.success, "resolver should succeed");
  EXPECT(resolved.resolvedVariantCount == 1,
         "only the contract shader should create a material source variant");

  const LX_core::SceneResourceTableUploadView uploadView =
      table.buildUploadView();
  bool foundPlainShadow = false;
  bool foundForwardVariant = false;
  for (const auto &shaderRef : uploadView.shaderResources) {
    const LX_core::ShaderResourceMetadata &shader = shaderRef.get();
    if (shader.uri ==
        LX_core::ResourceUri("techniques/Forward/shadow_depth_only")) {
      foundPlainShadow =
          !shader.requiresMaterialSourceVariant &&
          shader.materialSourceVariants.empty() && shader.payload != nullptr &&
          !shader.payload->getAllStages().empty();
    }
    if (shader.uri == LX_core::ResourceUri("techniques/Forward/pbr")) {
      foundForwardVariant =
          shader.requiresMaterialSourceVariant &&
          shader.materialSourceVariants.size() == 1 &&
          shader.materialSourceVariants.front().shaderProgram.getShader() !=
              nullptr;
    }
  }
  EXPECT(foundPlainShadow,
         "Shadow depth-only shader must remain a plain compiled payload");
  EXPECT(foundForwardVariant,
         "Forward PBR shader must expose one final material source variant");
}

void testResolverFailsSameTypeDifferentSceneSources() {
  LX_core::SceneResourceTable table;
  const LX_core::MaterialHandle first = table.registerMaterialInstance(
      LX_core::ResourceUri("memory://standard-pbr-a.material"),
      makeManualSourceMaterial(
          "standard-pbr",
          "assets://shaders/glsl/common/materials/standard_pbr.contract.glsl",
          "standard-pbr-a"));
  const LX_core::MaterialHandle second = table.registerMaterialInstance(
      LX_core::ResourceUri("memory://standard-pbr-b.material"),
      makeManualSourceMaterial(
          "standard-pbr",
          "assets://shaders/glsl/common/materials/alt_standard_pbr.contract.glsl",
          "standard-pbr-b"));
  EXPECT(first.isValid() && second.isValid(),
         "conflict materials should register");

  LX_core::RenderPathGraph graph = makeForwardStandardPbrGraph();
  const auto resolved = LX_infra::resolveMaterialSourceVariants(
      table, graph, LX_core::ResourceUri("memory://forward-test.render-path"));

  EXPECT(!resolved.success,
         "same material type cannot map to multiple source signatures");
  EXPECT(diagnosticsContain(resolved.diagnostics, "standard-pbr"),
         "diagnostic should name conflicted material type");
  EXPECT(diagnosticsContain(resolved.diagnostics, "standard-pbr-a.material"),
         "diagnostic should name first material URI");
  EXPECT(diagnosticsContain(resolved.diagnostics, "standard-pbr-b.material"),
         "diagnostic should name second material URI");
}

} // namespace

int main() {
  expSetEnvVK();
  if (std::filesystem::path sourceRoot{LXE_SOURCE_DIR}; !sourceRoot.empty()) {
    std::filesystem::current_path(sourceRoot);
  }

  testPipelineKeyIgnoresObjectAndTargetAxes();
  testRenderPassNodeRequiresRenderingAndGeometry();
  testUnknownRenderPathResourceVocabularyFails();
  testSameTypeDifferentSourceFails();
  testStandardPbrContractReflectsRequiredFields();
  testVariantOnlyShaderNakedCompileFailsWithDiagnostic();
  testResolverAttachesFinalShaderVariantToSourceMaterialPass();
  testResolverNoOpsBeforeMaterialsAreLoaded();
  testResolverKeepsNonContractShadowShaderPlain();
  testResolverFailsSameTypeDifferentSceneSources();

  if (g_failures > 0) {
    std::cerr << "FAILED: " << g_failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: material source variant pipeline contract tests passed\n";
  return 0;
}
