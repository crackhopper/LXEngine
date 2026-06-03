#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/descriptors/descriptor_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/texture.hpp"
#include "backend/vulkan/details/render_objects/framebuffer.hpp"
#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/skeleton.hpp"
#include "core/frame_graph/render_upload_plan.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/window/window.hpp"

#include "scene_test_helpers.hpp"

#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace {

int failures = 0;
int skipped = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

[[nodiscard]] bool shouldRunProbe() {
  const char *value = std::getenv("LX_RUN_MEMORY_PROBE");
  return value && *value && std::string(value) != "0";
}

[[nodiscard]] usize requestedFrameCount(const usize fallback) {
  const char *value = std::getenv("LX_MEMORY_PROBE_FRAMES");
  if (!value || !*value) {
    return fallback;
  }
  try {
    const auto parsed = static_cast<usize>(std::stoul(value));
    return parsed == 0 ? fallback : parsed;
  } catch (...) {
    return fallback;
  }
}

[[nodiscard]] bool submitEnabled() {
  const char *value = std::getenv("LX_OFFSCREEN_PROBE_MODE");
  return !(value && std::string_view(value) == "record_only");
}

[[nodiscard]] std::optional<usize> currentRssKb() {
#if defined(__linux__)
  std::ifstream status("/proc/self/status");
  std::string key;
  while (status >> key) {
    if (key == "VmRSS:") {
      usize rssKb = 0;
      status >> rssKb;
      return rssKb;
    }
    std::string discard;
    std::getline(status, discard);
  }
#endif
  return std::nullopt;
}

void testOffscreenSubmitProbe() {
  if (!shouldRunProbe()) {
    std::cout << "[SKIP] vulkan_offscreen_submit_memory_probe"
                 " (set LX_RUN_MEMORY_PROBE=1)\n";
    ++skipped;
    return;
  }

  try {
    constexpr usize kDefaultFrameCount = 1000;
    const usize frameCount = requestedFrameCount(kDefaultFrameCount);
    const bool doSubmit = submitEnabled();

    auto success = initializeRuntimeAssetRoot();
    if (!success) {
      std::cerr << "Failed to find shader files\n";
      ++failures;
      return;
    }

    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>(
        "vulkan-offscreen-submit-memory-probe", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "VulkanOffscreenSubmitMemoryProbe");
    constexpr u32 maxFrameInFlight = 2;

    const VkFormat depthFormat = device->getDepthFormat();
    const VkImageAspectFlags depthAspectMask = device->getDepthAspectMask();
    const VkSurfaceFormatKHR surfaceFormat = device->getSurfaceFormat();

    auto cmdBufferMgr = LX_core::backend::VulkanCommandBufferManager::create(
        *device, maxFrameInFlight, device->getGraphicsQueueFamilyIndex());
    auto resourceManager =
        LX_core::backend::VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(surfaceFormat,
                                                     depthFormat);

    auto &renderPass = resourceManager->getRenderPass();
    const VkExtent2D extent{64, 64};
    auto colorTex = LX_core::backend::VulkanTexture::createForAttachment(
        *device, extent.width, extent.height, surfaceFormat.format,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    auto depthTex = LX_core::backend::VulkanTexture::createForAttachment(
        *device, extent.width, extent.height, depthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthAspectMask);
    std::vector<VkImageView> attachments = {colorTex->getImageView(),
                                            depthTex->getImageView()};
    auto framebuffer = LX_core::backend::VulkanFrameBuffer::create(
        *device, renderPass.getHandle(), attachments, extent);

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
    auto meshPtr = LX_core::Mesh::create(
        vertexBufferPtr, indexBufferPtr,
        LX_core::BoundingBox{{-5.0f, -5.0f, 0.0f}, {5.0f, 5.0f, 0.0f}});

    auto material = LX_infra::loadGenericMaterial(
        "assets/materials/blinnphong_default.material");
    material->setParameter(LX_core::StringID("MaterialUBO"),
                           LX_core::StringID("enableNormal"), 0);
    material->syncGpuData();

    auto node = LX_core::SceneNode::create("offscreen_probe_triangle");
    node->addComponent<LX_core::MeshComponent>(meshPtr);
    node->addComponent<LX_core::MaterialComponent>(material);
    node->addComponent<LX_core::SkeletonComponent>(
        LX_core::Skeleton::create(std::vector<LX_core::Bone>{}));
    auto scene = LX_core::Scene::create(node);
    auto cameraNode = LX_test::makeDefaultCameraNodeWithTarget();
    scene->addCamera(cameraNode);
    auto lightNode = LX_core::SceneNode::create("offscreen_probe_light");
    scene->addRenderable(lightNode);
    scene->attachLight(lightNode,
                       std::make_shared<LX_core::DirectionalLight>());

    const auto camera = cameraNode->getComponent<LX_core::CameraComponent>();
    const auto dirLight = std::dynamic_pointer_cast<LX_core::DirectionalLight>(
        scene->getLights().front());
    const auto lightUbo = dirLight ? dirLight->getDirectionalUBO()
                                   : LX_core::DirectionalLightDataSharedPtr{};
    if (lightUbo) {
      lightUbo->param.dir = LX_core::Vec4f{0.0f, -1.0f, 0.0f, 0.0f};
      lightUbo->param.color = LX_core::Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
      lightUbo->setDirty();
    }

    camera->get().lookAt({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f},
                         LX_core::Vec3f{0.0f, 1.0f, 0.0f});
    camera->get().updateMatrices();

    auto renderItem =
        LX_test::firstItemFromScene(*scene, LX_core::Pass_Forward);
    if (renderItem.raster.drawData) {
      LX_core::PerDrawLayout pc{};
      pc.model = LX_core::Mat4f::identity();
      renderItem.raster.drawData->update(pc);
    }

    LX_core::RenderWorkQueue uploadQueue;
    uploadQueue.addItem(renderItem);
    const LX_core::RenderUploadPlan uploadPlan =
        LX_core::buildRenderUploadPlan(uploadQueue);
    for (const auto &resource : uploadPlan.resources) {
      resourceManager->syncResource(*cmdBufferMgr, resource);
    }
    resourceManager->collectGarbage();

    auto pipeline = resourceManager->getOrCreatePipeline(renderItem);
    const VkPipeline pipelineHandle =
        std::visit([](auto ref) { return ref.get().getHandle(); }, pipeline);
    if (pipelineHandle == VK_NULL_HANDLE) {
      std::cerr << "Pipeline not created correctly\n";
      ++failures;
      return;
    }

    const auto startRss = currentRssKb();
    if (!startRss.has_value()) {
      std::cerr << "failed to read /proc/self/status VmRSS\n";
      ++failures;
      return;
    }

    usize peakRss = *startRss;
    for (usize frame = 0; frame < frameCount; ++frame) {
      const u32 frameIndex = static_cast<u32>(frame % maxFrameInFlight);
      cmdBufferMgr->beginFrame(frameIndex);
      device->getDescriptorManager().beginFrame(frameIndex);
      resourceManager->beginFrame(frameIndex);

      auto cmd = cmdBufferMgr->allocateBuffer();
      cmd->begin();
      cmd->beginRenderPass(renderPass.getHandle(), framebuffer->getHandle(),
                           extent, renderPass.getClearValues());
      cmd->setViewport(extent.width, extent.height);
      cmd->setScissor(extent.width, extent.height);
      cmd->bindPipeline(pipeline);
      cmd->bindResources(*resourceManager, pipeline, renderItem);
      cmd->executeWorkItem(renderItem);
      cmd->endRenderPass();
      cmd->end();

      VkSubmitInfo submitInfo{};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submitInfo.commandBufferCount = 1;
      const VkCommandBuffer handle = cmd->getHandle();
      submitInfo.pCommandBuffers = &handle;
      if (doSubmit) {
        if (vkQueueSubmit(device->getGraphicsQueue(), 1, &submitInfo,
                          VK_NULL_HANDLE) != VK_SUCCESS) {
          std::cerr << "vkQueueSubmit failed in offscreen probe\n";
          ++failures;
          return;
        }
        vkQueueWaitIdle(device->getGraphicsQueue());
      }

      if (const auto rss = currentRssKb()) {
        peakRss = std::max(peakRss, *rss);
      }
    }

    const auto endRss = currentRssKb();
    if (!endRss.has_value()) {
      std::cerr << "failed to sample end-of-probe VmRSS\n";
      ++failures;
      return;
    }

    const usize growthKb = peakRss - *startRss;
    std::cout << "[probe] offscreen_" << (doSubmit ? "submit" : "record_only")
              << " frames=" << frameCount << " start=" << *startRss
              << " peak=" << peakRss << " end=" << *endRss
              << " growth_kb=" << growthKb << "\n";
  } catch (const std::exception &e) {
    std::cout << "[SKIP] vulkan_offscreen_submit_memory_probe (exception: "
              << e.what() << ")\n";
    ++skipped;
  }
}

} // namespace

int main() {
  expSetEnvVK();
  testOffscreenSubmitProbe();

  if (failures == 0) {
    std::cout << "[PASS] vulkan offscreen submit memory probe: " << skipped
              << " skipped\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
