#include "core/asset/material_contract.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/resource/resource_uri.hpp"
#include "core/utils/env.hpp"
#include "infra/material_loader/material_contract_reflector.hpp"
#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"

#include <filesystem>
#include <iostream>
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

} // namespace

int main() {
  expSetEnvVK();

  testPipelineKeyIgnoresObjectAndTargetAxes();
  testRenderPassNodeRequiresRenderingAndGeometry();
  testUnknownRenderPathResourceVocabularyFails();
  testSameTypeDifferentSourceFails();
  testStandardPbrContractReflectsRequiredFields();
  testVariantOnlyShaderNakedCompileFailsWithDiagnostic();

  if (g_failures > 0) {
    std::cerr << "FAILED: " << g_failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: material source variant pipeline contract tests passed\n";
  return 0;
}
