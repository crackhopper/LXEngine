#include "backend/vulkan/details/device_resources/texture.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/asset/texture.hpp"

#include <cstdlib>
#include <iostream>

using namespace LX_core;
using namespace LX_core::backend;

namespace {

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
  }
}

void testRg16FloatMapsToVulkanUploadFormat() {
  expect(vulkanTextureFormat(TextureFormat::RG16Float) ==
             VK_FORMAT_R16G16_SFLOAT,
         "TextureFormat::RG16Float should map to VK_FORMAT_R16G16_SFLOAT");
  expect(vulkanTextureFormatBytesPerPixel(VK_FORMAT_R16G16_SFLOAT) == 4,
         "VK_FORMAT_R16G16_SFLOAT upload copy should use 4 bytes per pixel");
}

} // namespace

int main() {
  testRg16FloatMapsToVulkanUploadFormat();
  return 0;
}
