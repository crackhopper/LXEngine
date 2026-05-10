#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device_resources/texture.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/render_objects/framebuffer.hpp"
#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/debug_draw/debug_draw.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"

#include "scene_test_helpers.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <new>
#include <iostream>
#include <type_traits>

namespace {

struct TestUniformResource final : public LX_core::IGpuResource {
  explicit TestUniformResource(u32 value) : value(value) {}

  LX_core::ResourceType getType() const override {
    return LX_core::ResourceType::UniformBuffer;
  }
  const void *getRawData() const override { return &value; }
  u32 getByteSize() const override {
    return sizeof(value);
  }

  u32 value = 0;
};

template <typename T, typename... Args>
std::shared_ptr<T> makePlacementShared(void *storage, Args &&...args) {
  auto *ptr = new (storage) T(std::forward<Args>(args)...);
  return std::shared_ptr<T>(ptr, [](T *p) { p->~T(); });
}

LX_core::SceneSharedPtr makeOverlayScene() {
  auto scene = LX_core::Scene::create("vulkan_debugdraw_growth");
  auto cameraNode = LX_test::makeDefaultCameraNodeWithTarget();
  const auto camera = cameraNode->getComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    throw std::runtime_error("DebugDraw test camera component missing");
  }
  camera->get().setCullingMask(LX_core::Layer_All);
  camera->get().updateMatrices();
  scene->addCamera(cameraNode);
  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::attachScene(scene);
  return scene;
}

void syncRenderingItemResources(
    LX_core::backend::VulkanResourceManager &resourceManager,
    LX_core::backend::VulkanCommandBufferManager &cmdBufferMgr,
    const LX_core::RenderingItem &item) {
  resourceManager.syncResource(cmdBufferMgr, item.vertexBuffer);
  resourceManager.syncResource(cmdBufferMgr, item.indexBuffer);
  for (const auto &cpuRes : item.descriptorResources) {
    resourceManager.syncResource(cmdBufferMgr, cpuRes);
  }
  resourceManager.collectGarbage();
}

LX_core::RenderingItem syncDebugOverlayItem(
    LX_core::backend::VulkanResourceManager &resourceManager,
    LX_core::backend::VulkanCommandBufferManager &cmdBufferMgr,
    LX_core::Scene &scene) {
  auto item = LX_test::firstItemFromScene(scene, LX_core::Pass_DebugOverlay);
  syncRenderingItemResources(resourceManager, cmdBufferMgr, item);
  return item;
}

bool drawDebugOverlayItem(
    LX_core::backend::VulkanDevice &device,
    LX_core::backend::VulkanResourceManager &resourceManager,
    LX_core::backend::VulkanCommandBufferManager &cmdBufferMgr,
    const LX_core::RenderingItem &item) {
  auto &renderPass = resourceManager.getRenderPass();
  auto &pipeline = resourceManager.getOrCreateRenderPipeline(item);
  if (pipeline.getHandle() == VK_NULL_HANDLE) {
    std::cerr << "DebugDraw overlay pipeline was not created\n";
    return false;
  }

  const VkExtent2D extent{64, 64};
  auto colorTex = LX_core::backend::VulkanTexture::createForAttachment(
      device, extent.width, extent.height, device.getSurfaceFormat().format,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
  auto depthTex = LX_core::backend::VulkanTexture::createForAttachment(
      device, extent.width, extent.height, device.getDepthFormat(),
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      device.getDepthAspectMask());
  std::vector<VkImageView> attachments = {colorTex->getImageView(),
                                          depthTex->getImageView()};
  auto framebuffer = LX_core::backend::VulkanFrameBuffer::create(
      device, renderPass.getHandle(), attachments, extent);

  cmdBufferMgr.beginFrame(0);
  auto cmd = cmdBufferMgr.allocateBuffer();

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (vkBeginCommandBuffer(cmd->getHandle(), &beginInfo) != VK_SUCCESS) {
    std::cerr << "Failed to begin DebugDraw command buffer\n";
    return false;
  }

  cmd->beginRenderPass(renderPass.getHandle(), framebuffer->getHandle(), extent,
                       renderPass.getClearValues());
  cmd->setViewport(extent.width, extent.height);
  cmd->setScissor(extent.width, extent.height);
  cmd->bindPipeline(pipeline);
  cmd->bindResources(resourceManager, pipeline, item);
  cmd->drawItem(item);
  cmd->endRenderPass();

  if (vkEndCommandBuffer(cmd->getHandle()) != VK_SUCCESS) {
    std::cerr << "Failed to end DebugDraw command buffer\n";
    return false;
  }

  return true;
}

bool verifyDebugDrawGrowthSync(
    LX_core::backend::VulkanDevice &device,
    LX_core::backend::VulkanResourceManager &resourceManager,
    LX_core::backend::VulkanCommandBufferManager &cmdBufferMgr) {
  auto scene = makeOverlayScene();

  LX_core::DebugDraw::beginFrame();
  LX_core::DebugDraw::drawLine({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  const bool initialSceneDirty = LX_core::DebugDraw::endFrame();

  auto smallItem = syncDebugOverlayItem(resourceManager, cmdBufferMgr, *scene);

  const auto smallVertexIdentity =
      LX_core::DebugDraw::testing::vertexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  const auto smallIndexIdentity =
      LX_core::DebugDraw::testing::indexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  auto smallVkVertex = resourceManager.getBuffer(smallVertexIdentity);
  auto smallVkIndex = resourceManager.getBuffer(smallIndexIdentity);
  if (!smallVkVertex || !smallVkIndex) {
    std::cerr << "Initial DebugDraw Vulkan buffers were not created\n";
    return false;
  }
  if (smallVkVertex->get().getSize() != smallItem.vertexBuffer->getByteSize() ||
      smallVkIndex->get().getSize() != smallItem.indexBuffer->getByteSize()) {
    std::cerr << "Initial DebugDraw GPU buffer sizes do not match CPU resources\n";
    return false;
  }
  if (!initialSceneDirty) {
    std::cerr << "Initial DebugDraw frame should mark scene dirty\n";
    return false;
  }
  if (!drawDebugOverlayItem(device, resourceManager, cmdBufferMgr, smallItem)) {
    return false;
  }
  const auto smallVertexHandle = smallVkVertex->get().getHandle();
  const auto smallIndexHandle = smallVkIndex->get().getHandle();
  const auto initialReservedVertexCapacity =
      LX_core::DebugDraw::testing::reservedVertexCapacity(
          LX_core::Layer_EditorOverlay);
  const usize growthLineCount = (initialReservedVertexCapacity / 2) + 1;

  LX_core::DebugDraw::beginFrame();
  for (usize i = 0; i < growthLineCount; ++i) {
    LX_core::DebugDraw::drawLine({0.0f, 0.0f, 0.0f},
                                 {static_cast<float>(i), 1.0f, 0.0f});
  }
  const bool growthSceneDirty = LX_core::DebugDraw::endFrame();

  const auto grownVertexIdentity =
      LX_core::DebugDraw::testing::vertexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  const auto grownIndexIdentity =
      LX_core::DebugDraw::testing::indexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  if (!growthSceneDirty) {
    std::cerr << "Growth DebugDraw frame should mark scene dirty\n";
    return false;
  }
  if (grownVertexIdentity == smallVertexIdentity ||
      grownIndexIdentity == smallIndexIdentity) {
    std::cerr << "DebugDraw growth did not replace CPU buffer identities\n";
    return false;
  }

  auto grownItem = syncDebugOverlayItem(resourceManager, cmdBufferMgr, *scene);
  auto grownVkVertex = resourceManager.getBuffer(grownVertexIdentity);
  auto grownVkIndex = resourceManager.getBuffer(grownIndexIdentity);
  if (!grownVkVertex || !grownVkIndex) {
    std::cerr << "Grown DebugDraw Vulkan buffers were not created\n";
    return false;
  }
  if (grownVkVertex->get().getSize() != grownItem.vertexBuffer->getByteSize() ||
      grownVkIndex->get().getSize() != grownItem.indexBuffer->getByteSize()) {
    std::cerr << "Grown DebugDraw GPU buffer sizes do not match CPU resources\n";
    return false;
  }
  if (grownVkVertex->get().getHandle() == smallVertexHandle ||
      grownVkIndex->get().getHandle() == smallIndexHandle) {
    std::cerr << "Grown DebugDraw buffers reused stale undersized Vulkan handles\n";
    return false;
  }
  if (!drawDebugOverlayItem(device, resourceManager, cmdBufferMgr, grownItem)) {
    return false;
  }
  const auto grownReservedVertexCapacity =
      LX_core::DebugDraw::testing::reservedVertexCapacity(
          LX_core::Layer_EditorOverlay);
  const usize retainedLineCount =
      std::max<usize>(1, (grownReservedVertexCapacity / 2) - 1);

  LX_core::DebugDraw::beginFrame();
  for (usize i = 0; i < retainedLineCount; ++i) {
    LX_core::DebugDraw::drawLine({0.0f, 0.0f, 0.0f},
                                 {static_cast<float>(i), 2.0f, 0.0f});
  }
  const bool retainedSceneDirty = LX_core::DebugDraw::endFrame();

  const auto retainedVertexIdentity =
      LX_core::DebugDraw::testing::vertexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  const auto retainedIndexIdentity =
      LX_core::DebugDraw::testing::indexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  if (retainedVertexIdentity != grownVertexIdentity ||
      retainedIndexIdentity != grownIndexIdentity) {
    std::cerr << "Within-capacity DebugDraw frame unexpectedly replaced CPU identities\n";
    return false;
  }
  if (retainedSceneDirty) {
    std::cerr << "Within-capacity DebugDraw frame should not mark scene dirty\n";
    return false;
  }

  syncRenderingItemResources(resourceManager, cmdBufferMgr, grownItem);
  auto retainedVkVertex = resourceManager.getBuffer(retainedVertexIdentity);
  auto retainedVkIndex = resourceManager.getBuffer(retainedIndexIdentity);
  if (!retainedVkVertex || !retainedVkIndex) {
    std::cerr << "Retained DebugDraw Vulkan buffers were not found\n";
    return false;
  }
  if (retainedVkVertex->get().getSize() != grownItem.vertexBuffer->getByteSize() ||
      retainedVkIndex->get().getSize() != grownItem.indexBuffer->getByteSize()) {
    std::cerr << "Within-capacity DebugDraw GPU sizes no longer match retained CPU capacity\n";
    return false;
  }
  if (retainedVkVertex->get().getHandle() != grownVkVertex->get().getHandle() ||
      retainedVkIndex->get().getHandle() != grownVkIndex->get().getHandle()) {
    std::cerr << "Within-capacity DebugDraw frame should reuse grown Vulkan buffers\n";
    return false;
  }
  if (!drawDebugOverlayItem(device, resourceManager, cmdBufferMgr, grownItem)) {
    return false;
  }

  LX_core::DebugDraw::reset();
  return true;
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

    auto meshPtr = LX_core::Mesh::create(
        vertexBufferPtr, indexBufferPtr,
        LX_core::BoundingBox{{-5.0f, -5.0f, 0.0f}, {5.0f, 5.0f, 0.0f}});
    auto material = LX_infra::loadGenericMaterial("assets/materials/blinnphong_default.material");
    auto node = LX_core::SceneNode::create("vulkan_resource_node");
    node->addComponent<LX_core::MeshComponent>(meshPtr);
    node->addComponent<LX_core::MaterialComponent>(material);
    node->addComponent<LX_core::SkeletonComponent>(
        LX_core::Skeleton::create({}));
    auto scene = LX_core::Scene::create(node);
    scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());
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

    if (!verifyDebugDrawGrowthSync(*device, *resourceManager, *cmdBufferMgr)) {
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanResourceManager test: " << e.what() << "\n";
    return 0;
  }
}
