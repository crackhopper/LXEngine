#include "vulkan_renderer.hpp"
#include "core/rhi/render_resource.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/pass.hpp"
#include "infra/window/window.hpp"
#include "details/commands/command_buffer_manager.hpp"
#include "details/descriptors/descriptor_manager.hpp"
#include "details/render_objects/framebuffer.hpp"
#include "details/render_objects/render_pass.hpp"
#include "details/render_objects/swapchain.hpp"
#include "details/device.hpp"
#include "details/resource_manager.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
bool rendererDebugEnabled() {
  static const bool enabled = [] {
    const char *value = std::getenv("LX_RENDER_DEBUG");
    return value != nullptr && std::strcmp(value, "0") != 0;
  }();
  return enabled;
}

bool envEnabled(const char *name) {
  const char *value = std::getenv(name);
  return value != nullptr && std::strcmp(value, "0") != 0;
}

const char *vkResultToString(VkResult result) {
  switch (result) {
  case VK_SUCCESS:
    return "VK_SUCCESS";
  case VK_NOT_READY:
    return "VK_NOT_READY";
  case VK_TIMEOUT:
    return "VK_TIMEOUT";
  case VK_EVENT_SET:
    return "VK_EVENT_SET";
  case VK_EVENT_RESET:
    return "VK_EVENT_RESET";
  case VK_INCOMPLETE:
    return "VK_INCOMPLETE";
  case VK_ERROR_OUT_OF_DATE_KHR:
    return "VK_ERROR_OUT_OF_DATE_KHR";
  case VK_SUBOPTIMAL_KHR:
    return "VK_SUBOPTIMAL_KHR";
  case VK_ERROR_DEVICE_LOST:
    return "VK_ERROR_DEVICE_LOST";
  case VK_ERROR_SURFACE_LOST_KHR:
    return "VK_ERROR_SURFACE_LOST_KHR";
  default:
    return "VK_RESULT_UNKNOWN";
  }
}

void debugLog(const char *message) {
  if (!rendererDebugEnabled()) {
    return;
  }
  std::cerr << "[RendererDebug] " << message << std::endl;
}

/// REQ-009: reverse of resource_manager.cpp's toVkFormat(ImageFormat).
/// Only covers the swapchain-relevant VkFormats. Unknown inputs fall back to
/// RGBA8 and log a debug warning rather than throwing — initScene must be
/// robust against whatever surface format the Vulkan driver exposes.
LX_core::ImageFormat toImageFormat(VkFormat format) {
  switch (format) {
  case VK_FORMAT_B8G8R8A8_SRGB:
  case VK_FORMAT_B8G8R8A8_UNORM:
    return LX_core::ImageFormat::BGRA8;
  case VK_FORMAT_R8G8B8A8_SRGB:
  case VK_FORMAT_R8G8B8A8_UNORM:
    return LX_core::ImageFormat::RGBA8;
  case VK_FORMAT_R8_UNORM:
    return LX_core::ImageFormat::R8;
  case VK_FORMAT_D32_SFLOAT:
    return LX_core::ImageFormat::D32Float;
  case VK_FORMAT_D24_UNORM_S8_UINT:
    return LX_core::ImageFormat::D24UnormS8;
  case VK_FORMAT_D32_SFLOAT_S8_UINT:
    return LX_core::ImageFormat::D32FloatS8;
  default:
    if (rendererDebugEnabled()) {
      std::cerr << "[RendererDebug] toImageFormat: unknown VkFormat "
                << static_cast<int>(format) << ", falling back to RGBA8"
                << std::endl;
    }
    return LX_core::ImageFormat::RGBA8;
  }
}
} // namespace

namespace LX_core::backend {

class VulkanRendererImpl : public gpu::Renderer {
public:
  VulkanRendererImpl() {}
  ~VulkanRendererImpl() override { destroy(); }

  void initialize(WindowPtr _window, const char *appName) override {
    const int maxFramesInFlight = 3;

    device = VulkanDevice::create();
    device->initialize(_window, appName);
    // Window backends return an allocated handle pointer (void*) for Vulkan.
    VkInstance instance = device->getInstance();

    // Create command buffer manager first (needed for resource manager)
    cmdBufferMgr = VulkanCommandBufferManager::create(
        *device, maxFramesInFlight, device->getGraphicsQueueFamilyIndex());

    // Create resource manager
    resourceManager = VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(device->getSurfaceFormat(),
                                                     device->getDepthFormat());
    if (envEnabled("LX_RENDER_DEBUG_CLEAR")) {
      resourceManager->getRenderPass().setClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    }

    swapchain = VulkanSwapchain::create(*device, _window, maxFramesInFlight);
    swapchain->initialize(resourceManager->getRenderPass());
    if (rendererDebugEnabled()) {
      const VkExtent2D extent = swapchain->getExtent();
      std::cerr << "[RendererDebug] initialize: extent=" << extent.width << "x"
                << extent.height << ", maxFramesInFlight=" << maxFramesInFlight
                << std::endl;
      if (envEnabled("LX_RENDER_DEBUG_CLEAR")) {
        std::cerr << "[RendererDebug] debug clear color enabled" << std::endl;
      }
      if (envEnabled("LX_RENDER_DISABLE_CULL")) {
        std::cerr << "[RendererDebug] cull disabled" << std::endl;
      }
      if (envEnabled("LX_RENDER_DISABLE_DEPTH")) {
        std::cerr << "[RendererDebug] depth disabled" << std::endl;
      }
      if (envEnabled("LX_RENDER_FLIP_VIEWPORT_Y")) {
        std::cerr << "[RendererDebug] viewport Y flipped" << std::endl;
      }
    }
  }
  void shutdown() override { destroy(); }

  /// REQ-009: derive the real swapchain RenderTarget from the Vulkan device's
  /// chosen surface format + depth format. This is the value that gets plugged
  /// into FramePass.target and also backfilled into any Camera whose m_target
  /// is nullopt at initScene time.
  LX_core::RenderTarget makeSwapchainTarget() const {
    LX_core::RenderTarget t{};
    t.colorFormat = toImageFormat(device->getSurfaceFormat().format);
    t.depthFormat = toImageFormat(device->getDepthFormat());
    t.sampleCount = 1;
    return t;
  }

  void initScene(ScenePtr _scene) override {
    scene = _scene;

    // REQ-009: compute the swapchain target once, use it for both:
    //   1. Backfilling any nullopt camera's m_target (before buildFromScene).
    //   2. Wiring up FramePass.target so getSceneLevelResources(pass, target)
    //      can match the camera on the filter side.
    const LX_core::RenderTarget swapchainTarget = makeSwapchainTarget();
    for (const auto &cam : scene->getCameras()) {
      if (cam && !cam->getTarget().has_value()) {
        cam->setTarget(swapchainTarget);
      }
    }

    // Configure the FrameGraph. REQ-008 only wires up Pass_Forward; future
    // changes may add Pass_Shadow / Pass_Deferred with real targets.
    m_frameGraph = LX_core::FrameGraph{}; // Fresh graph on every initScene.
    m_frameGraph.addPass(
        LX_core::FramePass{LX_core::Pass_Forward, swapchainTarget, {}});

    // RenderQueue::buildFromScene (invoked per pass below) internally:
    //   - filters renderables by supportsPass(pass)
    //   - merges scene.getSceneLevelResources(pass, target) (camera UBO filtered by
    //     target, light UBO filtered by pass mask)
    //   - sorts by PipelineKey
    // There is no more side-channel camera/light UBO injection here.
    m_frameGraph.buildFromScene(*scene);

    // Initial resource sync + push-constant seed for every item across every
    // pass in the FrameGraph.
    for (auto &pass : m_frameGraph.getPasses()) {
      for (auto &item : pass.queue.getItems()) {
        resourceManager->syncResource(*cmdBufferMgr, item.vertexBuffer);
        resourceManager->syncResource(*cmdBufferMgr, item.indexBuffer);
        for (auto &cpuRes : item.descriptorResources) {
          resourceManager->syncResource(*cmdBufferMgr, cpuRes);
        }
        if (item.objectInfo) {
          PC_Draw pc{};
          pc.model = Mat4f::identity();
          pc.enableLighting = 1;
          pc.enableSkinning = 0;
          item.objectInfo->update(pc);
        }
      }
    }
    resourceManager->collectGarbage();

    // Pre-build every pipeline the scene needs. Runtime cache misses still
    // work via getOrCreateRenderPipeline(item) but emit a warning log.
    auto infos = m_frameGraph.collectAllPipelineBuildDescs();
    resourceManager->preloadPipelines(infos);

    if (rendererDebugEnabled()) {
      size_t itemCount = 0;
      for (const auto &pass : m_frameGraph.getPasses()) {
        itemCount += pass.queue.getItems().size();
      }
      std::cerr << "[RendererDebug] initScene: passes="
                << m_frameGraph.getPasses().size()
                << ", totalItems=" << itemCount
                << ", preloadedPipelines=" << infos.size() << std::endl;
    }
  }

  void uploadData() override {
    for (auto &pass : m_frameGraph.getPasses()) {
      for (auto &item : pass.queue.getItems()) {
        resourceManager->syncResource(*cmdBufferMgr, item.vertexBuffer);
        resourceManager->syncResource(*cmdBufferMgr, item.indexBuffer);
        for (auto &cpuRes : item.descriptorResources) {
          resourceManager->syncResource(*cmdBufferMgr, cpuRes);
        }
      }
    }
    resourceManager->collectGarbage();
  }

  void draw() override {
    const uint32_t maxFramesInFlight = 3;
    const VkExtent2D extent = swapchain->getExtent();

    const uint32_t currentFrameIndex = frameIndex % maxFramesInFlight;
    uint32_t imageIndex = 0;

    VkResult acquireResult =
        swapchain->acquireNextImage(currentFrameIndex, imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR ||
        acquireResult == VK_SUBOPTIMAL_KHR) {
      VkFence fence = swapchain->getInFlightFence(currentFrameIndex);
      vkResetFences(device->getLogicalDevice(), 1, &fence);
      swapchain->waitIdle();
      swapchain->rebuild(resourceManager->getRenderPass());
      return;
    }
    if (acquireResult != VK_SUCCESS) {
      VkFence fence = swapchain->getInFlightFence(currentFrameIndex);
      vkResetFences(device->getLogicalDevice(), 1, &fence);
      return;
    }

    auto &renderPass = resourceManager->getRenderPass();

    cmdBufferMgr->beginFrame(currentFrameIndex);
    device->getDescriptorManager().beginFrame(currentFrameIndex);

    auto cmd = cmdBufferMgr->allocateBuffer();
    cmd->begin();
    cmd->beginRenderPass(renderPass.getHandle(),
                         swapchain->getFramebuffer(imageIndex).getHandle(),
                         extent, renderPass.getClearValues());

    cmd->setViewport(extent.width, extent.height);
    cmd->setScissor(extent.width, extent.height);

    // Iterate every pass × every item in the FrameGraph. Each item may use a
    // different pipeline; bindPipeline / bindResources / drawItem per item.
    for (auto &pass : m_frameGraph.getPasses()) {
      for (auto &item : pass.queue.getItems()) {
        auto &pipeline = resourceManager->getOrCreateRenderPipeline(item);
        cmd->bindPipeline(pipeline);
        cmd->bindResources(*resourceManager, pipeline, item);
        cmd->drawItem(item);
      }
    }

    cmd->endRenderPass();
    cmd->end();

    VkSemaphore waitSemaphores[] = {
        swapchain->getImageAvailableSemaphore(currentFrameIndex)};
    VkSemaphore signalSemaphores[] = {
        swapchain->getRenderFinishedSemaphore(currentFrameIndex)};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    VkCommandBuffer handle = cmd->getHandle();
    submitInfo.pCommandBuffers = &handle;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkFence fence = swapchain->getInFlightFence(currentFrameIndex);
    vkResetFences(device->getLogicalDevice(), 1, &fence);
    if (vkQueueSubmit(device->getGraphicsQueue(), 1, &submitInfo, fence) !=
        VK_SUCCESS) {
      return;
    }

    VkResult presentResult = swapchain->present(currentFrameIndex, imageIndex);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR) {
      swapchain->waitIdle();
      swapchain->rebuild(resourceManager->getRenderPass());
      return;
    }

    frameIndex++;
  }

  VulkanDevicePtr device = nullptr;
  VulkanResourceManagerPtr resourceManager = nullptr;
  VulkanSwapchainPtr swapchain = nullptr;
  VulkanCommandBufferManagerPtr cmdBufferMgr = nullptr;

  ScenePtr scene = nullptr;
  LX_core::FrameGraph m_frameGraph{};
  uint32_t frameIndex = 0;

private:
  void destroy() {
    if (device) {
      // 关键：等 GPU 干完活再删东西
      vkDeviceWaitIdle(device->getLogicalDevice());
    }
    // 1. 销毁 Command Buffer Manager
    cmdBufferMgr.reset();
    // 2. 销毁 Swapchain
    swapchain.reset();
    // 3. 销毁 Resource Manager
    resourceManager.reset();
    // 4. 销毁 Device
    device.reset();
  }

  VkSurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<VkSurfaceFormatKHR> &availableFormats) {
    // 优先选择 SRGB 非线性格式
    for (const auto &availableFormat : availableFormats) {
      if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
          availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return availableFormat;
      }
    }
    return availableFormats[0];
  }
};

VulkanRenderer::VulkanRenderer(Token token) : p_impl(nullptr) {
  p_impl = new VulkanRendererImpl();
}

VulkanRenderer::~VulkanRenderer() { delete p_impl; }

} // namespace LX_core::backend
