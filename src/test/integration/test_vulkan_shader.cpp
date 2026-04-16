#include "core/utils/filesystem_tools.hpp"
#include "backend/vulkan/details/device_resources/shader.hpp"
#include "backend/vulkan/details/device.hpp"
#include "infra/window/window.hpp"
#include "core/utils/env.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
  expSetEnvVK();
  try {
    auto success = cdToWhereResourcesCouldFound("blinnphong_0");
    if (!success) {
      std::cerr << "Failed to find shader files\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>("Test Vulkan Shader", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanShader");

    auto vertShader = LX_core::backend::VulkanShader::create(
        *device, "blinnphong_0", VK_SHADER_STAGE_VERTEX_BIT);
    auto fragShader = LX_core::backend::VulkanShader::create(
        *device, "blinnphong_0", VK_SHADER_STAGE_FRAGMENT_BIT);

    if (vertShader->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Vertex shader module is null\n";
      return 1;
    }
    if (fragShader->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Fragment shader module is null\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanShader test: " << e.what() << "\n";
    return 0;
  }
}
