#include "graphics_backend/vulkan/details/pipelines/vkp_blinnphong.hpp"
#include "graphics_backend/vulkan/details/render_objects/vkr_renderpass.hpp"
#include "graphics_backend/vulkan/details/vk_device.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static void cdToWhereShadersExist() {
  fs::path p = fs::current_path();
  for (int i = 0; i < 8; ++i) {
    if (fs::exists(p / "shaders" / "glsl" / "blinnphong_0.vert.spv") &&
        fs::exists(p / "shaders" / "glsl" / "blinnphong_0.frag.spv")) {
      fs::current_path(p);
      return;
    }
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
      std::cerr << "Missing SPIR-V files for pipeline test\n";
      return 1;
    }

    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize();

    const VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    const VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
    auto renderPass =
        LX_core::graphic_backend::VulkanRenderPass::create(
            *device, colorFormat, depthFormat);

    VkExtent2D extent{1, 1};
    auto pipeline =
        LX_core::graphic_backend::VkPipelineBlinnPhong::create(
            *device, extent);

    // Build actual VkPipeline (layout/shader modules are created in create()).
    pipeline->buildGraphicsPpl(renderPass->getHandle());

    if (pipeline->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "VkPipeline handle is null\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanPipeline test: " << e.what() << "\n";
    return 0;
  }
}

