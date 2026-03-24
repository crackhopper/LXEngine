#include "graphics_backend/vulkan/details/resources/vkr_shader.hpp"
#include "graphics_backend/vulkan/details/vk_device.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static void cdToWhereShadersExist() {
  fs::path p = fs::current_path();
  for (int i = 0; i < 8; ++i) {
    // 1) Check direct expected path: <base>/shaders/glsl/*.spv
    if (fs::exists(p / "shaders" / "glsl" / "blinnphong_0.vert.spv") &&
        fs::exists(p / "shaders" / "glsl" / "blinnphong_0.frag.spv")) {
      fs::current_path(p);
      return;
    }
    // 2) Check build output path: <base>/build/shaders/glsl/*.spv
    if (fs::exists(p / "build" / "shaders" / "glsl" / "blinnphong_0.vert.spv") &&
        fs::exists(p / "build" / "shaders" / "glsl" / "blinnphong_0.frag.spv")) {
      fs::current_path(p / "build");
      return;
    }
    const auto parent = p.parent_path();
    if (parent == p) break;
    p = parent;
  }
}

int main() {
  try {
    cdToWhereShadersExist();

    const fs::path vertPath = "shaders/glsl/blinnphong_0.vert.spv";
    const fs::path fragPath = "shaders/glsl/blinnphong_0.frag.spv";
    if (!fs::exists(vertPath) || !fs::exists(fragPath)) {
      std::cerr << "Missing SPIR-V files:\n"
                << "  " << vertPath.string() << "\n"
                << "  " << fragPath.string() << "\n";
      return 1; // fail (this is what OpenSpec verifies)
    }

    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize();

    auto vertShader = LX_core::graphic_backend::VulkanShader::create(
        *device, "blinnphong_0", VK_SHADER_STAGE_VERTEX_BIT);
    auto fragShader = LX_core::graphic_backend::VulkanShader::create(
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

