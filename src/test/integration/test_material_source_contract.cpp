#include "core/asset/material_contract.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/material_loader/material_contract_reflector.hpp"
#include "infra/material_loader/material_resource_parser.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

void testSourceSignatureIgnoresInstanceValues() {
  LX_core::MaterialContractReflection a;
  a.sourceUri = LX_core::ResourceUri(
      "assets/shaders/glsl/common/materials/matte.contract.glsl");
  a.declaredType = "matte";
  a.reflectionHash = "hash-a";
  a.storageAbiHash = "storage-a";
  a.accessorAbiHash = "accessor-v1";

  LX_core::MaterialContractReflection b = a;

  EXPECT(a.sourceSignature() == b.sourceSignature(),
         "same reflected source should produce same source signature");
  EXPECT(a.materialSignature(LX_core::StringID("ForwardPbr"),
                             LX_core::StringID("RenderStateOpaque")) ==
             b.materialSignature(LX_core::StringID("ForwardPbr"),
                                 LX_core::StringID("RenderStateOpaque")),
         "same reflected source should produce same material signature");

  b.reflectionHash = "hash-b";
  EXPECT(a.sourceSignature() != b.sourceSignature(),
         "reflection hash must participate in source signature");
}

bool diagnosticsContain(const std::vector<std::string> &diagnostics,
                        const std::string &needle) {
  for (const std::string &diagnostic : diagnostics) {
    if (diagnostic.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void testMaterialParserRequiresBsdfSource() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/no-source.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.instance == nullptr, "missing bsdf.source should fail");
  EXPECT(!parsed.diagnostics.empty(), "missing bsdf.source should diagnose");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.source"),
         "diagnostic should name bsdf.source");
}

void testMaterialParserRejectsNonScalarBsdfSource() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const auto parsed = parser.parse(table,
                                   LX_core::ResourceUri("memory://materials/"
                                                        "non-scalar-source.material"),
                                   R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: [materials/matte.contract.glsl]
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.instance == nullptr, "non-scalar bsdf.source should fail");
  EXPECT(!parsed.diagnostics.empty(),
         "non-scalar bsdf.source should diagnose");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.source"),
         "non-scalar diagnostic should name bsdf.source");
}

void testMaterialParserStoresSourceIdentityAndCloneCopiesIt() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/with-source.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: shaders/materials/matte.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.diagnostics.empty(), "explicit bsdf.source should parse");
  EXPECT(parsed.instance != nullptr,
         "explicit bsdf.source should create instance");
  if (!parsed.instance) {
    return;
  }

  EXPECT(parsed.instance->getMaterialSourceUri().string() ==
             "memory://materials/shaders/materials/matte.contract.glsl",
         "MaterialInstance should store canonical source URI");
  EXPECT(parsed.instance->getMaterialSourceSignature() ==
             LX_core::StringID(
                 "memory://materials/shaders/materials/matte.contract.glsl"),
         "MaterialInstance should store source signature identity");
  EXPECT(parsed.instance->getMaterialSourceReflectionHash() ==
             "unreflected-contract",
         "MaterialInstance should store placeholder reflection hash until "
         "source loading is wired");

  const auto clone = parsed.instance->cloneInstanceDataUnique();
  EXPECT(clone->getMaterialSourceUri() ==
             parsed.instance->getMaterialSourceUri(),
         "clone should copy material source URI");
  EXPECT(clone->getMaterialSourceSignature() ==
             parsed.instance->getMaterialSourceSignature(),
         "clone should copy material source signature");
  EXPECT(clone->getMaterialSourceReflectionHash() ==
             parsed.instance->getMaterialSourceReflectionHash(),
         "clone should copy material source reflection hash");
}

void testFindParameterHitAndMiss() {
  LX_core::MaterialContractReflection reflection;
  reflection.parameters.push_back(LX_core::MaterialContractParameter{
      "Kd", true, {LX_core::MaterialContractParameterKind::Rgb}});
  reflection.parameters.push_back(LX_core::MaterialContractParameter{
      "roughness", false, {LX_core::MaterialContractParameterKind::Float}});

  auto hit = reflection.findParameter("roughness");
  EXPECT(hit.has_value(), "findParameter should find an existing parameter");
  EXPECT(hit.has_value() && hit->get().name == "roughness",
         "findParameter should return the matching parameter");
  EXPECT(!reflection.findParameter("metallic").has_value(),
         "findParameter should return empty for a missing parameter");
}

void testDefaultAccessorAbi() {
  LX_core::MaterialContractAccessorAbi abi;
  constexpr std::array expectedFields{"baseColor", "alpha",  "metallic",
                                      "roughness", "normal", "ao",
                                      "emissive"};

  EXPECT(abi.entryPoint == "lxLoadMaterialSurface",
         "default accessor entry point should match Material Accessor ABI");
  EXPECT(abi.requiredFields.size() == expectedFields.size(),
         "default accessor required field count should match ABI");
  for (std::size_t i = 0;
       i < abi.requiredFields.size() && i < expectedFields.size(); ++i) {
    EXPECT(abi.requiredFields[i] == expectedFields[i],
           "default accessor required fields should match ABI order");
  }
}

void testSourceSignatureIncludesStorageAndAccessorAbi() {
  LX_core::MaterialContractReflection a;
  a.sourceUri = LX_core::ResourceUri(
      "assets/shaders/glsl/common/materials/matte.contract.glsl");
  a.declaredType = "matte";
  a.reflectionHash = "hash-a";
  a.storageAbiHash = "storage-a";
  a.accessorAbiHash = "accessor-v1";

  LX_core::MaterialContractReflection b = a;
  b.storageAbiHash = "storage-b";
  EXPECT(a.sourceSignature() != b.sourceSignature(),
         "storage ABI hash must participate in source signature");

  b = a;
  b.accessorAbiHash = "accessor-v2";
  EXPECT(a.sourceSignature() != b.sourceSignature(),
         "accessor ABI hash must participate in source signature");
}

void testMaterialSignatureIncludesPassAndRenderState() {
  LX_core::MaterialContractReflection reflection;
  reflection.sourceUri = LX_core::ResourceUri(
      "assets/shaders/glsl/common/materials/matte.contract.glsl");
  reflection.declaredType = "matte";
  reflection.reflectionHash = "hash-a";
  reflection.storageAbiHash = "storage-a";
  reflection.accessorAbiHash = "accessor-v1";

  const LX_core::StringID forwardPbr("ForwardPbr");
  const LX_core::StringID deferredPbr("DeferredPbr");
  const LX_core::StringID opaque("RenderStateOpaque");
  const LX_core::StringID alphaBlend("RenderStateAlphaBlend");

  EXPECT(reflection.materialSignature(forwardPbr, opaque) !=
             reflection.materialSignature(deferredPbr, opaque),
         "pass shader signature must participate in material signature");
  EXPECT(reflection.materialSignature(forwardPbr, opaque) !=
             reflection.materialSignature(forwardPbr, alphaBlend),
         "render state signature must participate in material signature");
}

void testReflectsContractMetadataBlock() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture spectrum
// parameter: metallic optional float texture
// parameter: roughness optional float texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/matte.contract.glsl"), source);

  EXPECT(result.diagnostics.empty(),
         "valid contract source should not emit diagnostics");
  EXPECT(result.reflection.has_value(),
         "valid contract source should reflect a contract");
  EXPECT(result.reflection.has_value() &&
             result.reflection->declaredType == "matte",
         "contract should reflect declared type");
  EXPECT(result.reflection.has_value() &&
             result.reflection->supportStatus ==
                 LX_core::MaterialContractSupportStatus::Supported,
         "contract should reflect support status");
  EXPECT(result.reflection.has_value() &&
             result.reflection->reflectionHash == "matte-reflect-v1",
         "contract should reflect reflection hash");
  EXPECT(result.reflection.has_value() &&
             result.reflection->storageAbiHash == "matte-storage-v1",
         "contract should reflect storage ABI hash");
  EXPECT(result.reflection.has_value() &&
             result.reflection->accessorAbiHash == "material-surface-v1",
         "contract should reflect accessor ABI hash");

  const auto kd = result.reflection.has_value()
                      ? result.reflection->findParameter("Kd")
                      : std::nullopt;
  EXPECT(kd.has_value(), "contract should reflect Kd parameter");
  EXPECT(kd.has_value() && kd->get().required,
         "contract should reflect Kd required flag");
  EXPECT(kd.has_value() && kd->get().allowedKinds.size() == 3,
         "contract should reflect Kd kind count");
  EXPECT(kd.has_value() && kd->get().allowedKinds.size() == 3 &&
             kd->get().allowedKinds[0] ==
                 LX_core::MaterialContractParameterKind::Rgb &&
             kd->get().allowedKinds[1] ==
                 LX_core::MaterialContractParameterKind::Texture &&
             kd->get().allowedKinds[2] ==
                 LX_core::MaterialContractParameterKind::Spectrum,
         "contract should reflect Kd allowed kind order");

  const auto metallic = result.reflection.has_value()
                            ? result.reflection->findParameter("metallic")
                            : std::nullopt;
  EXPECT(metallic.has_value(), "contract should reflect metallic parameter");
  EXPECT(metallic.has_value() && !metallic->get().required,
         "contract should reflect metallic optional flag");
  EXPECT(metallic.has_value() && metallic->get().allowedKinds.size() == 2,
         "contract should reflect metallic kind count");
  EXPECT(metallic.has_value() && metallic->get().allowedKinds.size() == 2 &&
             metallic->get().allowedKinds[0] ==
                 LX_core::MaterialContractParameterKind::Float &&
             metallic->get().allowedKinds[1] ==
                 LX_core::MaterialContractParameterKind::Texture,
         "contract should reflect metallic allowed kind order");
}

void testReflectsAccessorWithBodyComment() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) {
  /* body comment */
  LxMaterialSurface surface;
  return surface;
}
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/body-comment.contract.glsl"),
      source);
  EXPECT(result.diagnostics.empty(),
         "function body comments should not affect accessor detection");
  EXPECT(result.reflection.has_value(),
         "function body comments should still reflect a contract");
}

void testReflectRejectsMissingAccessor() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/bad.contract.glsl"), source);
  EXPECT(!result.diagnostics.empty(),
         "missing Material Accessor ABI should be diagnostic");
  EXPECT(!result.reflection.has_value(),
         "missing accessor should reject reflection");
}

void testReflectRejectsMissingStatus() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/missing-status.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "missing status should emit a diagnostic");
  EXPECT(!result.reflection.has_value(),
         "missing status should reject reflection");
}

void testReflectRejectsDuplicateTypeMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// type: glossy
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/duplicate-type.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(), "duplicate type should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate type should reject reflection");
}

void testReflectRejectsDuplicateStatusMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// status: unsupported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/duplicate-status.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "duplicate status should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate status should reject reflection");
}

void testReflectRejectsDuplicateReflectionHashMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// reflectionHash: matte-reflect-v2
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/duplicate-reflection-hash.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "duplicate reflection hash should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate reflection hash should reject reflection");
}

void testReflectRejectsDuplicateStorageAbiHashMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// storageAbiHash: matte-storage-v2
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/duplicate-storage-hash.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "duplicate storage ABI hash should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate storage ABI hash should reject reflection");
}

void testReflectRejectsDuplicateAccessorAbiHashMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// accessorAbiHash: material-surface-v2
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/duplicate-accessor-hash.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "duplicate accessor ABI hash should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate accessor ABI hash should reject reflection");
}

void testReflectRejectsDuplicateParameterMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// parameter: Kd optional float
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/duplicate-parameter.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "duplicate parameter should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate parameter should reject reflection");
}

void testReflectRejectsBareMetadataLines() {
  const std::string source = R"glsl(
LX_MATERIAL_CONTRACT_BEGIN
type: matte
status: supported
reflectionHash: matte-reflect-v1
storageAbiHash: matte-storage-v1
accessorAbiHash: material-surface-v1
parameter: Kd required rgb texture
LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/bare.contract.glsl"), source);
  EXPECT(!result.diagnostics.empty(),
         "bare metadata lines should not be accepted as contract metadata");
  EXPECT(!result.reflection.has_value(),
         "bare metadata lines should reject reflection");
}

void testReflectRejectsCommentedOutAccessor() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
// LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/commented-accessor.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "commented-out accessor should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "commented-out accessor should reject reflection");
}

void testReflectRejectsAccessorCallWithoutDefinition() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
void probe() { lxLoadMaterialSurface(0, uv, n, tbn); }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/accessor-call.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor call should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor call should reject reflection");
}

void testReflectRejectsAccessorPrototypeWithoutBody() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn);
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-prototype.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor prototype should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor prototype should reject reflection");
}

void testReflectRejectsWrongAccessorReturnType() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
float lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-wrong-return.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "wrong accessor return type should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "wrong accessor return type should reject reflection");
}

void testReflectRejectsQualifiedAccessorReturnType() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
const LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-qualified-return.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "qualified accessor return type should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "qualified accessor return type should reject reflection");
}

void testReflectRejectsPrefixedAccessorReturnType() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
inline LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-prefixed-return.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "prefixed accessor return type should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "prefixed accessor return type should reject reflection");
}

void testReflectRejectsCommentBetweenAccessorReturnAndName() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface /* comment */ lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-comment-before-name.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "comment between accessor return type and name should not satisfy "
         "Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "comment between accessor return type and name should reject "
         "reflection");
}

void testReflectRejectsWrongAccessorFunctionName() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadNotMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-wrong-name.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "wrong accessor function name should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "wrong accessor function name should reject reflection");
}

void testReflectRejectsWrongAccessorParameterList() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-wrong-params.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "wrong accessor parameter list should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "wrong accessor parameter list should reject reflection");
}

void testReflectRejectsExtraAccessorParameter() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn, float extra) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-extra-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "extra accessor parameter should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "extra accessor parameter should reject reflection");
}

void testReflectRejectsWrongAccessorParameterType() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(int materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-wrong-param-type.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "wrong accessor parameter type should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "wrong accessor parameter type should reject reflection");
}

void testReflectRejectsAccessorArrayDeclarator() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex[2], vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-array-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor array declarator should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor array declarator should reject reflection");
}

void testReflectRejectsAccessorNumericParameterName() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint 2bad, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-numeric-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor numeric parameter name should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor numeric parameter name should reject reflection");
}

void testReflectRejectsCommentInsideAccessorParameter() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex /* comment */, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-comment-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "comment inside accessor parameter should not satisfy Material "
         "Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "comment inside accessor parameter should reject reflection");
}

void testReflectRejectsCommentBetweenAccessorParametersAndBody() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) /* comment */ { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-comment-before-body.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "comment between accessor parameter list and body should not satisfy "
         "Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "comment between accessor parameter list and body should reject "
         "reflection");
}

void testReflectRejectsAccessorPointerDeclarator() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint *materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-pointer-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor pointer declarator should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor pointer declarator should reject reflection");
}

void testReflectRejectsAccessorReferenceDeclarator() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint &materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-reference-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor reference declarator should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor reference declarator should reject reflection");
}

void testReflectRejectsAccessorQualifierDeclarator() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(const uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-qualifier-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor qualifier declarator should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor qualifier declarator should reject reflection");
}

void testReflectRejectsWrongAccessorParameterNames() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint i, vec2 tex, vec3 normal, mat3 basis) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-wrong-names.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "wrong accessor parameter names should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "wrong accessor parameter names should reject reflection");
}

void testReflectRejectsSingleWrongAccessorParameterName() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 tex, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-single-wrong-name.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "single wrong accessor parameter name should not satisfy Material "
         "Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "single wrong accessor parameter name should reject reflection");
}

void testReflectRejectsAccessorMacroWithoutFunctionDefinition() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#define lxLoadMaterialSurface(materialIndex, uv, n, tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/accessor-macro.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor macro should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor macro should reject reflection");
}

void testReflectRejectsMultilineAccessorMacro() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#define MATERIAL_ACCESSOR \
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-multiline-macro.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "multiline accessor macro should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "multiline accessor macro should reject reflection");
}

void testReflectRejectsAccessorInDisabledPreprocessorBlock() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-disabled-block.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "disabled preprocessor accessor should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "disabled preprocessor accessor should reject reflection");
}

void testReflectRejectsAccessorInCommentedDisabledPreprocessorBlock() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0 // reason
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-disabled-comment.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "commented disabled preprocessor accessor should not satisfy Material "
         "Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "commented disabled preprocessor accessor should reject reflection");
}

void testReflectRejectsAccessorInBlockCommentedDisabledPreprocessorBlock() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0 /* reason */
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-disabled-block-comment.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "block-commented disabled preprocessor accessor should not satisfy "
         "Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "block-commented disabled preprocessor accessor should reject "
         "reflection");
}

void testReflectRejectsAccessorAfterNestedDirectiveInDisabledBlock() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0
#if 1
#endif
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-disabled-nested.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "nested disabled preprocessor accessor should not satisfy Material "
         "Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "nested disabled preprocessor accessor should reject reflection");
}

void testReflectsAccessorInElseBranchAfterDisabledIf() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0
LxMaterialSurface lxNotMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
#else
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/accessor-else.contract.glsl"),
      source);
  EXPECT(result.diagnostics.empty(),
         "active else branch accessor should satisfy Material Accessor ABI");
  EXPECT(result.reflection.has_value(),
         "active else branch accessor should reflect a contract");
}

void testReflectsAccessorInElifOneBranchAfterDisabledIf() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0
LxMaterialSurface lxNotMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
#elif 1
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/accessor-elif.contract.glsl"),
      source);
  EXPECT(result.diagnostics.empty(),
         "active elif branch accessor should satisfy Material Accessor ABI");
  EXPECT(result.reflection.has_value(),
         "active elif branch accessor should reflect a contract");
}

void testReflectRejectsAccessorInElifZeroBranch() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0
LxMaterialSurface lxNotMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
#elif 0
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-elif-zero.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "inactive elif zero branch should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "inactive elif zero branch should reject reflection");
}

void testReflectsAccessorInIfOneBranchIgnoringInactiveElse() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 1
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
#else
LxMaterialSurface lxLoadMaterialSurface(uint badIndex, vec2 tex, vec3 normal, mat3 basis) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-if-one-else.contract.glsl"),
      source);
  EXPECT(result.diagnostics.empty(),
         "inactive else branch should not interfere with active accessor");
  EXPECT(result.reflection.has_value(),
         "active if branch accessor should reflect a contract");
}

void testValidateReflectionSetRejectsSourceSignatureConflicts() {
  LX_core::MaterialContractReflection a;
  a.sourceUri = LX_core::ResourceUri("memory://materials/matte.contract.glsl");
  a.declaredType = "matte";
  a.reflectionHash = "shared-reflect-v1";
  a.storageAbiHash = "shared-storage-v1";
  a.accessorAbiHash = "shared-accessor-v1";
  a.parameters.push_back(LX_core::MaterialContractParameter{
      "Kd", true, {LX_core::MaterialContractParameterKind::Rgb}});

  LX_core::MaterialContractReflection b = a;
  b.parameters.clear();
  b.parameters.push_back(LX_core::MaterialContractParameter{
      "roughness", false, {LX_core::MaterialContractParameterKind::Float}});

  const auto result =
      LX_infra::validateMaterialContractReflectionSet(std::vector{a, b});
  EXPECT(!result.diagnostics.empty(),
         "same source signature with different schemas should be diagnostic");
}

void testValidateReflectionSetRejectsAccessorAbiConflicts() {
  LX_core::MaterialContractReflection a;
  a.sourceUri = LX_core::ResourceUri("memory://materials/matte.contract.glsl");
  a.declaredType = "matte";
  a.reflectionHash = "shared-reflect-v1";
  a.storageAbiHash = "shared-storage-v1";
  a.accessorAbiHash = "shared-accessor-v1";
  a.parameters.push_back(LX_core::MaterialContractParameter{
      "Kd", true, {LX_core::MaterialContractParameterKind::Rgb}});

  LX_core::MaterialContractReflection b = a;
  b.accessorAbi.entryPoint = "lxLoadDifferentMaterialSurface";

  LX_core::MaterialContractReflection c = a;
  c.accessorAbi.requiredFields.push_back("clearcoat");

  const auto entryPointResult =
      LX_infra::validateMaterialContractReflectionSet(std::vector{a, b});
  EXPECT(!entryPointResult.diagnostics.empty(),
         "same source signature with different accessor entry point should be "
         "diagnostic");

  const auto requiredFieldsResult =
      LX_infra::validateMaterialContractReflectionSet(std::vector{a, c});
  EXPECT(!requiredFieldsResult.diagnostics.empty(),
         "same source signature with different accessor fields should be "
         "diagnostic");
}

} // namespace

int main() {
  testSourceSignatureIgnoresInstanceValues();
  testMaterialParserRequiresBsdfSource();
  testMaterialParserRejectsNonScalarBsdfSource();
  testMaterialParserStoresSourceIdentityAndCloneCopiesIt();
  testFindParameterHitAndMiss();
  testDefaultAccessorAbi();
  testSourceSignatureIncludesStorageAndAccessorAbi();
  testMaterialSignatureIncludesPassAndRenderState();
  testReflectsContractMetadataBlock();
  testReflectsAccessorWithBodyComment();
  testReflectRejectsMissingAccessor();
  testReflectRejectsMissingStatus();
  testReflectRejectsDuplicateTypeMetadata();
  testReflectRejectsDuplicateStatusMetadata();
  testReflectRejectsDuplicateReflectionHashMetadata();
  testReflectRejectsDuplicateStorageAbiHashMetadata();
  testReflectRejectsDuplicateAccessorAbiHashMetadata();
  testReflectRejectsDuplicateParameterMetadata();
  testReflectRejectsBareMetadataLines();
  testReflectRejectsCommentedOutAccessor();
  testReflectRejectsAccessorCallWithoutDefinition();
  testReflectRejectsAccessorPrototypeWithoutBody();
  testReflectRejectsWrongAccessorReturnType();
  testReflectRejectsQualifiedAccessorReturnType();
  testReflectRejectsPrefixedAccessorReturnType();
  testReflectRejectsCommentBetweenAccessorReturnAndName();
  testReflectRejectsWrongAccessorFunctionName();
  testReflectRejectsWrongAccessorParameterList();
  testReflectRejectsExtraAccessorParameter();
  testReflectRejectsWrongAccessorParameterType();
  testReflectRejectsAccessorArrayDeclarator();
  testReflectRejectsAccessorNumericParameterName();
  testReflectRejectsCommentInsideAccessorParameter();
  testReflectRejectsCommentBetweenAccessorParametersAndBody();
  testReflectRejectsAccessorPointerDeclarator();
  testReflectRejectsAccessorReferenceDeclarator();
  testReflectRejectsAccessorQualifierDeclarator();
  testReflectRejectsWrongAccessorParameterNames();
  testReflectRejectsSingleWrongAccessorParameterName();
  testReflectRejectsAccessorMacroWithoutFunctionDefinition();
  testReflectRejectsMultilineAccessorMacro();
  testReflectRejectsAccessorInDisabledPreprocessorBlock();
  testReflectRejectsAccessorInCommentedDisabledPreprocessorBlock();
  testReflectRejectsAccessorInBlockCommentedDisabledPreprocessorBlock();
  testReflectRejectsAccessorAfterNestedDirectiveInDisabledBlock();
  testReflectsAccessorInElseBranchAfterDisabledIf();
  testReflectsAccessorInElifOneBranchAfterDisabledIf();
  testReflectRejectsAccessorInElifZeroBranch();
  testReflectsAccessorInIfOneBranchIgnoringInactiveElse();
  testValidateReflectionSetRejectsSourceSignatureConflicts();
  testValidateReflectionSetRejectsAccessorAbiConflicts();
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
