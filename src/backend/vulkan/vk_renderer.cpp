#include "vk_renderer.hpp"
#include "details/commands/vkc_cmdbuffer_manager.hpp"
#include "details/descriptors/vkd_descriptor_manager.hpp"
#include "details/render_objects/vkr_framebuffer.hpp"
#include "details/render_objects/vkr_renderpass.hpp"
#include "details/render_objects/vkr_swapchain.hpp"
#include "details/vk_device.hpp"
#include "details/vk_resource_manager.hpp"
#include "infra/window/window.hpp"
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

  void initScene(ScenePtr _scene) override {
    scene = _scene;
    renderItem = scene->buildRenderingItem();

    // Inject camera/light UBOs required by the blinn-phong pipeline.
    if (scene->camera) {
      auto camRes = scene->camera->getRenderResources();
      renderItem.descriptorResources.insert(
          renderItem.descriptorResources.end(), camRes.begin(), camRes.end());
    }
    if (scene->directionalLight) {
      auto lightRes = scene->directionalLight->getRenderResources();
      renderItem.descriptorResources.insert(
          renderItem.descriptorResources.end(), lightRes.begin(),
          lightRes.end());
    }

    // Initialize push-constants with sane defaults.
    if (renderItem.objectInfo) {
      PC_BlinnPhong pc{};
      pc.model = Mat4f::identity();
      pc.enableLighting = 1;
      pc.enableSkinning = 0;
      renderItem.objectInfo->update(pc);
    }

    // Create GPU resources immediately.
    resourceManager->syncResource(*cmdBufferMgr, renderItem.vertexBuffer);
    resourceManager->syncResource(*cmdBufferMgr, renderItem.indexBuffer);
    for (auto &cpuRes : renderItem.descriptorResources) {
      resourceManager->syncResource(*cmdBufferMgr, cpuRes);
    }
    resourceManager->collectGarbage();
    if (rendererDebugEnabled()) {
      std::cerr << "[RendererDebug] initScene: vertexBytes="
                << renderItem.vertexBuffer->getByteSize()
                << ", indexBytes=" << renderItem.indexBuffer->getByteSize()
                << ", descriptorCount="
                << renderItem.descriptorResources.size() << std::endl;
    }
  }

  void uploadData() override {
    // Sync only dirty resources; the manager handles create/update.
    resourceManager->syncResource(*cmdBufferMgr, renderItem.vertexBuffer);
    resourceManager->syncResource(*cmdBufferMgr, renderItem.indexBuffer);
    for (auto &cpuRes : renderItem.descriptorResources) {
      resourceManager->syncResource(*cmdBufferMgr, cpuRes);
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
    const std::string pipelineKey =
        renderItem.shaderInfo ? renderItem.shaderInfo->getShaderName()
                              : std::string("blinnphong_0");
    auto &pipeline = resourceManager->getRenderPipeline(pipelineKey);

    cmdBufferMgr->beginFrame(currentFrameIndex);
    device->getDescriptorManager().beginFrame(currentFrameIndex);

    auto cmd = cmdBufferMgr->allocateBuffer();
    cmd->begin();
    cmd->beginRenderPass(renderPass.getHandle(),
                         swapchain->getFramebuffer(imageIndex).getHandle(),
                         extent, renderPass.getClearValues());

    cmd->bindPipeline(pipeline);
    cmd->setViewport(extent.width, extent.height);
    cmd->setScissor(extent.width, extent.height);
    cmd->bindResources(*resourceManager, pipeline, renderItem);
    cmd->drawItem(renderItem);

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
  RenderingItem renderItem{};
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
