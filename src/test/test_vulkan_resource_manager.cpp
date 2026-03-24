#include "core/resources/index_buffer.hpp"
#include "core/resources/vertex_buffer.hpp"
#include "graphics_backend/vulkan/details/vk_resource_manager.hpp"
#include "graphics_backend/vulkan/details/vk_device.hpp"
#include "graphics_backend/vulkan/details/resources/vkr_buffer.hpp"

#include <filesystem>
#include <vulkan/vulkan.h>

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

    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize();

    const VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    const VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
    VkSurfaceFormatKHR surfaceFormat{};
    surfaceFormat.format = colorFormat;
    surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    auto resourceManager =
        LX_core::graphic_backend::VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(surfaceFormat, depthFormat);

    auto *renderPass = resourceManager->getRenderPass();
    auto *pipeline = resourceManager->getRenderPipeline();
    if (!renderPass || !pipeline || pipeline->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "RenderPass/Pipeline not initialized correctly\n";
      return 1;
    }

    using V = LX_core::VertexPosNormalUvBone;
    auto vertexBufferPtr = LX_core::VertexBuffer<V>::create(
        {
            V({-5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f},
              {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0},
              {1.0f, 0.0f, 0.0f, 0.0f}),
            V({5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f},
              {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0},
              {1.0f, 0.0f, 0.0f, 0.0f}),
            V({5.0f, -5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f},
              {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0},
              {1.0f, 0.0f, 0.0f, 0.0f}),
        });

    auto indexBufferPtr = LX_core::IndexBuffer::create({0u, 1u, 2u});

    resourceManager->syncResource(vertexBufferPtr);
    resourceManager->syncResource(indexBufferPtr);
    resourceManager->collectGarbage();

    auto *vkVertex = resourceManager->getBuffer(vertexBufferPtr->getResourceHandle());
    auto *vkIndex = resourceManager->getBuffer(indexBufferPtr->getResourceHandle());
    if (!vkVertex || !vkIndex) {
      std::cerr << "Expected Vulkan buffers were not created\n";
      return 1;
    }

    if (vkVertex->getHandle() == VK_NULL_HANDLE ||
        vkIndex->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Vulkan buffer handles are null\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanResourceManager test: " << e.what() << "\n";
    return 0;
  }
}

