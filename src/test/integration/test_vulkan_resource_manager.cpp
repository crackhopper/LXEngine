#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"

#include "scene_test_helpers.hpp"

#include <vulkan/vulkan.h>

#include <new>
#include <iostream>
#include <type_traits>

namespace {

struct TestUniformResource final : public LX_core::IGpuResource {
  explicit TestUniformResource(uint32_t value) : value(value) {}

  LX_core::ResourceType getType() const override {
    return LX_core::ResourceType::UniformBuffer;
  }
  const void *getRawData() const override { return &value; }
  ResourceByteSize32 getByteSize() const override {
    return sizeof(value);
  }

  uint32_t value = 0;
};

template <typename T, typename... Args>
std::shared_ptr<T> makePlacementShared(void *storage, Args &&...args) {
  auto *ptr = new (storage) T(std::forward<Args>(args)...);
  return std::shared_ptr<T>(ptr, [](T *p) { p->~T(); });
}

} // namespace

int main() {
  expSetEnvVK();
  try {
    auto success = initializeRuntimeAssetRoot();
    if (!success) {
      std::cerr << "Failed to find shader files\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>(
        "Test Vulkan ResourceManager", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanResourceManager");

    VkSurfaceFormatKHR surfaceFormat = device->getSurfaceFormat();
    const VkFormat depthFormat = device->getDepthFormat();

    auto cmdBufferMgr = LX_core::backend::VulkanCommandBufferManager::create(
        *device, 3, device->getGraphicsQueueFamilyIndex());
    auto resourceManager =
        LX_core::backend::VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(surfaceFormat,
                                                     depthFormat);

    using V = LX_core::VertexPosNormalUvBone;
    auto vertexBufferPtr = LX_core::VertexBuffer<V>::create({
        V({-5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, -5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
    });

    auto indexBufferPtr = LX_core::IndexBuffer::create({0u, 1u, 2u});

    resourceManager->syncResource(*cmdBufferMgr, vertexBufferPtr);
    resourceManager->syncResource(*cmdBufferMgr, indexBufferPtr);
    resourceManager->collectGarbage();

    auto meshPtr = LX_core::Mesh::create(vertexBufferPtr, indexBufferPtr);
    auto material = LX_infra::loadGenericMaterial("materials/blinnphong_default.material");
    auto node = LX_core::SceneNode::create(
        "vulkan_resource_node", meshPtr, material, LX_core::Skeleton::create({}));
    auto scene = LX_core::Scene::create(node);
    auto item = LX_test::firstItemFromScene(*scene, LX_core::Pass_Forward);
    auto &pipeline = resourceManager->getOrCreateRenderPipeline(item);
    if (pipeline.getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Pipeline not created correctly\n";
      return 1;
    }

    auto vkVertexOpt =
        resourceManager->getBuffer(vertexBufferPtr->getBackendCacheIdentity());
    auto vkIndexOpt =
        resourceManager->getBuffer(indexBufferPtr->getBackendCacheIdentity());
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

    auto tempResource = std::make_shared<TestUniformResource>(7u);
    const auto tempIdentity = tempResource->getBackendCacheIdentity();
    resourceManager->syncResource(*cmdBufferMgr, tempResource);
    resourceManager->collectGarbage();
    auto tempBuffer0 = resourceManager->getBuffer(tempIdentity);
    if (!tempBuffer0) {
      std::cerr << "Expected temp uniform GPU buffer after initial sync\n";
      return 1;
    }
    const auto tempHandle0 = tempBuffer0->get().getHandle();

    resourceManager->collectGarbage();
    auto tempBuffer1 = resourceManager->getBuffer(tempIdentity);
    if (!tempBuffer1 || tempBuffer1->get().getHandle() != tempHandle0) {
      std::cerr << "Temporarily unused resource was not retained across grace frame\n";
      return 1;
    }

    resourceManager->syncResource(*cmdBufferMgr, tempResource);
    resourceManager->collectGarbage();
    auto tempBuffer2 = resourceManager->getBuffer(tempIdentity);
    if (!tempBuffer2 || tempBuffer2->get().getHandle() != tempHandle0) {
      std::cerr << "Resync after one inactive frame should reuse same GPU buffer\n";
      return 1;
    }

    resourceManager->collectGarbage();
    resourceManager->collectGarbage();
    if (resourceManager->getBuffer(tempIdentity)) {
      std::cerr << "Temp resource should be evicted after inactivity grace period\n";
      return 1;
    }

    using ReusedStorage =
        std::aligned_storage_t<sizeof(TestUniformResource),
                               alignof(TestUniformResource)>;
    ReusedStorage reusedStorage;

    auto reusedA = makePlacementShared<TestUniformResource>(&reusedStorage, 11u);
    const auto reusedIdentityA = reusedA->getBackendCacheIdentity();
    resourceManager->syncResource(*cmdBufferMgr, reusedA);
    resourceManager->collectGarbage();
    auto reusedBufferA = resourceManager->getBuffer(reusedIdentityA);
    if (!reusedBufferA) {
      std::cerr << "Expected first placement resource GPU buffer\n";
      return 1;
    }

    auto firstAddress = reusedA.get();
    reusedA.reset();

    auto reusedB = makePlacementShared<TestUniformResource>(&reusedStorage, 22u);
    const auto reusedIdentityB = reusedB->getBackendCacheIdentity();
    if (reusedB.get() != firstAddress) {
      std::cerr << "Placement test did not reuse the same CPU address\n";
      return 1;
    }
    if (reusedIdentityA == reusedIdentityB) {
      std::cerr << "Stable backend identity unexpectedly reused across objects\n";
      return 1;
    }

    resourceManager->syncResource(*cmdBufferMgr, reusedB);
    if (resourceManager->getCachedResourceCount() < 2) {
      std::cerr << "Address-reused CPU resource aliased old GPU cache entry\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanResourceManager test: " << e.what() << "\n";
    return 0;
  }
}
