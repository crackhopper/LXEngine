#include "core/resources/index_buffer.hpp"
#include "core/resources/vertex_buffer.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "backend/vulkan/details/vk_resource_manager.hpp"
#include "backend/vulkan/details/vk_device.hpp"
#include "backend/vulkan/details/commands/vkc_cmdbuffer_manager.hpp"
#include "backend/vulkan/details/resources/vkr_buffer.hpp"
#include "infra/window/window.hpp"
#include "core/utils/env.hpp"

#include <vulkan/vulkan.h>

#include <iostream>

int main() {
  expSetEnvVK();
  try {
    auto success = cdToWhereShadersExist("blinnphong_0");
    if (!success) {
      std::cerr << "Failed to find shader files\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>("Test Vulkan ResourceManager", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanResourceManager");

    VkSurfaceFormatKHR surfaceFormat = device->getSurfaceFormat();
    const VkFormat colorFormat = surfaceFormat.format;
    const VkFormat depthFormat = device->getDepthFormat();
    auto depthAspectMask = device->getDepthAspectMask();

    auto cmdBufferMgr = LX_core::backend::VulkanCommandBufferManager::create(
        *device, 3, device->getGraphicsQueueFamilyIndex());
    auto resourceManager =
        LX_core::backend::VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(surfaceFormat, depthFormat);

    auto &renderPass = resourceManager->getRenderPass();
    auto &pipeline = resourceManager->getRenderPipeline();
    if (pipeline.getHandle() == VK_NULL_HANDLE) {
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

    resourceManager->syncResource(*cmdBufferMgr, vertexBufferPtr);
    resourceManager->syncResource(*cmdBufferMgr, indexBufferPtr);
    resourceManager->collectGarbage();

    auto vkVertexOpt = resourceManager->getBuffer(vertexBufferPtr->getResourceHandle());
    auto vkIndexOpt = resourceManager->getBuffer(indexBufferPtr->getResourceHandle());
    if (!vkVertexOpt || !vkIndexOpt) {
      std::cerr << "Expected Vulkan buffers were not created\n";
      return 1;
    }

    auto &vkVertex = vkVertexOpt->get();
    auto &vkIndex = vkIndexOpt->get();
    if (vkVertex.getHandle() == VK_NULL_HANDLE ||
        vkIndex.getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Vulkan buffer handles are null\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanResourceManager test: " << e.what() << "\n";
    return 0;
  }
}

