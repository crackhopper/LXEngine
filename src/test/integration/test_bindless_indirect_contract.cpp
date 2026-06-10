#include "backend/vulkan/vulkan_gpu_resource_table.hpp"
#include "core/scene/scene_gpu_records.hpp"

#include <iostream>

using namespace LX_core;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

void testSharedTextureIdentityProducesOneBindlessSlot() {
  VulkanGpuResourceTable table;
  const GpuImageHandle imageA{42};
  const GpuImageHandle imageB{42};
  const GpuSamplerHandle sampler{7};
  const GpuDescriptorTableHandle descriptors = table.createDescriptorTable();

  const GpuBindlessSlot first =
      table.updateBindlessSlot(descriptors, imageA, sampler);
  const GpuBindlessSlot second =
      table.updateBindlessSlot(descriptors, imageB, sampler);

  EXPECT(first.index == second.index,
         "same image/sampler identity should reuse bindless slot");

  SceneGpuMaterialRecord materialA;
  SceneGpuMaterialRecord materialB;
  materialA.baseColorTexture = first.index;
  materialB.baseColorTexture = second.index;
  EXPECT(materialA.baseColorTexture == materialB.baseColorTexture,
         "two material records should point at the same texture slot");
}

void testIndirectDrawBufferHandleIsOpaque() {
  VulkanGpuResourceTable table;
  const u8 commandBytes[16] = {};
  const auto handle =
      table.updateIndirectDrawBuffer(std::span<const u8>(commandBytes));
  EXPECT(handle.id != 0, "indirect draw buffer handle should be valid");
}

} // namespace

int main() {
  testSharedTextureIdentityProducesOneBindlessSlot();
  testIndirectDrawBufferHandleIsOpaque();
  if (g_failures != 0) {
    std::cerr << g_failures << " bindless/indirect checks failed\n";
    return 1;
  }
  return 0;
}
