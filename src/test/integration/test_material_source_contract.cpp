#include "core/asset/material_contract.hpp"

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

} // namespace

int main() {
  testSourceSignatureIgnoresInstanceValues();
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
