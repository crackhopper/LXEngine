#include "core/asset/material_contract.hpp"
#include "infra/material_loader/material_contract_reflector.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>

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

} // namespace

int main() {
  testSourceSignatureIgnoresInstanceValues();
  testFindParameterHitAndMiss();
  testDefaultAccessorAbi();
  testSourceSignatureIncludesStorageAndAccessorAbi();
  testMaterialSignatureIncludesPassAndRenderState();
  testReflectsContractMetadataBlock();
  testReflectRejectsMissingAccessor();
  testReflectRejectsMissingStatus();
  testReflectRejectsBareMetadataLines();
  testReflectRejectsCommentedOutAccessor();
  testReflectRejectsAccessorCallWithoutDefinition();
  testReflectRejectsAccessorPrototypeWithoutBody();
  testReflectRejectsWrongAccessorReturnType();
  testReflectRejectsWrongAccessorParameterList();
  testReflectRejectsAccessorMacroWithoutFunctionDefinition();
  testReflectRejectsMultilineAccessorMacro();
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
