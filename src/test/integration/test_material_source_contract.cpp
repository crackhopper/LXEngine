#include "core/asset/material_contract.hpp"
#include "infra/material_loader/material_contract_reflector.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

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
             result.reflection->findParameter("metallic").has_value(),
         "contract should reflect metallic parameter");
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

} // namespace

int main() {
  testSourceSignatureIgnoresInstanceValues();
  testFindParameterHitAndMiss();
  testDefaultAccessorAbi();
  testSourceSignatureIncludesStorageAndAccessorAbi();
  testMaterialSignatureIncludesPassAndRenderState();
  testReflectsContractMetadataBlock();
  testReflectRejectsMissingAccessor();
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
