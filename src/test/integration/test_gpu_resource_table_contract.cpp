#include "core/rhi/gpu_resource_table.hpp"
#include "backend/vulkan/vulkan_gpu_resource_table.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

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

std::string readFile(const char *path) {
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

void testCoreHeaderHasNoVulkanTypes() {
  const std::string header = readFile(
      "/home/lixiang/proj/LXEngine/src/core/rhi/gpu_resource_table.hpp");
  EXPECT(header.find("Vk") == std::string::npos,
         "core gpu_resource_table header should not mention Vulkan Vk types");
  EXPECT(header.find("vulkan") == std::string::npos,
         "core gpu_resource_table header should not mention vulkan");
}

void testInterfaceSurface() {
  static_assert(std::is_abstract_v<IGpuResourceTable>);
  static_assert(std::is_base_of_v<IGpuResourceTable, VulkanGpuResourceTable>);

  EXPECT(sizeof(GpuBufferHandle) == sizeof(u64),
         "buffer handles should be lightweight opaque ids");
  EXPECT(sizeof(GpuImageHandle) == sizeof(u64),
         "image handles should be lightweight opaque ids");
  EXPECT(sizeof(GpuSamplerHandle) == sizeof(u64),
         "sampler handles should be lightweight opaque ids");
  EXPECT(sizeof(GpuDescriptorTableHandle) == sizeof(u64),
         "descriptor table handles should be lightweight opaque ids");
  EXPECT(sizeof(GpuPipelineHandle) == sizeof(u64),
         "pipeline handles should be lightweight opaque ids");
}

void testVulkanShellProgress() {
  VulkanGpuResourceTable table;
  const GpuProgress progress = table.queryProgress();
  EXPECT(progress.totalTasks == 0, "empty shell should report zero total tasks");
  EXPECT(progress.completedTasks == 0,
         "empty shell should report zero completed tasks");
}

} // namespace

int main() {
  testCoreHeaderHasNoVulkanTypes();
  testInterfaceSurface();
  testVulkanShellProgress();
  if (g_failures != 0) {
    std::cerr << g_failures << " gpu resource table checks failed\n";
    return 1;
  }
  return 0;
}
