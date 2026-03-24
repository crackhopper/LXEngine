#include "vk_renderer.hpp"
#include "details/commands/vkc_cmdbuffer_manager.hpp"
#include "details/descriptors/vkd_descriptor_manager.hpp"
#include "details/render_objects/vkr_framebuffer.hpp"
#include "details/render_objects/vkr_renderpass.hpp"
#include "details/render_objects/vkr_swapchain.hpp"
#include "details/vk_device.hpp"
#include "details/vk_resource_manager.hpp"
#include "infra/window/window.hpp"
#include <cstring>
#include <stdexcept>
namespace {
// 这种函数通常只需要执行一次，逻辑相对固定

} // namespace

namespace LX_core::graphic_backend {

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

    resourceManager = VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(device->getSurfaceFormat(),
                                                     device->getDepthFormat());

    auto graphicsIdx = device->getGraphicsQueueFamilyIndex();
    auto presentIdx = device->getPresentQueueFamilyIndex();
    swapchain = VulkanSwapchain::create(*device, device->getSurface(), device->getExtent(), graphicsIdx,
                                        presentIdx, maxFramesInFlight);
    swapchain->initialize(*resourceManager->getRenderPass());

    cmdBufferMgr = VulkanCommandBufferManager::create(
        *device, maxFramesInFlight, device->getGraphicsQueueFamilyIndex());

    // Needed for GPU-side uploads (textures).
    resourceManager->setCommandBufferManager(*cmdBufferMgr);
  }
  void shutdown() override { destroy(); }

  void initScene(ScenePtr _scene) override {
    scene = _scene;
    renderItem = scene->buildRenderItem();

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
    resourceManager->syncResource(renderItem.vertexBuffer);
    resourceManager->syncResource(renderItem.indexBuffer);
    for (auto &cpuRes : renderItem.descriptorResources) {
      resourceManager->syncResource(cpuRes);
    }
    resourceManager->collectGarbage();
  }

  void uploadData() override {
    // Sync only dirty resources; the manager handles create/update.
    resourceManager->syncResource(renderItem.vertexBuffer);
    resourceManager->syncResource(renderItem.indexBuffer);
    for (auto &cpuRes : renderItem.descriptorResources) {
      resourceManager->syncResource(cpuRes);
    }
    resourceManager->collectGarbage();
  }

  void draw() override {
    const uint32_t maxFramesInFlight = 3;
    const VkExtent2D extent = swapchain->getExtent();

    const uint32_t currentFrameIndex = frameIndex % maxFramesInFlight;
    uint32_t imageIndex = 0;

    if (swapchain->acquireNextImage(currentFrameIndex, imageIndex) !=
        VK_SUCCESS) {
      return;
    }

    cmdBufferMgr->beginFrame(currentFrameIndex);
    device->getDescriptorManager().beginFrame(currentFrameIndex);

    VulkanCommandBuffer cmd = cmdBufferMgr->allocateBuffer();
    cmd.setResourceManager(*resourceManager);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    vkBeginCommandBuffer(cmd.getHandle(), &beginInfo);

    auto *renderPass = resourceManager->getRenderPass();
    cmd.beginRenderPass(renderPass->getHandle(),
                        swapchain->getFramebuffer(imageIndex).getHandle(),
                        extent, renderPass->getClearValues());

    cmd.setViewport(extent.width, extent.height);
    cmd.setScissor(extent.width, extent.height);

    auto *pipeline = resourceManager->getRenderPipeline();
    cmd.bindPipeline(*pipeline);
    cmd.bindResources(*pipeline, renderItem);
    cmd.drawItem(renderItem);

    cmd.endRenderPass();
    vkEndCommandBuffer(cmd.getHandle());

    VkSemaphore waitSemaphores[] = {
        swapchain->getImageAvailableSemaphore(currentFrameIndex)};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {
        swapchain->getRenderFinishedSemaphore(currentFrameIndex)};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    VkCommandBuffer handle = cmd.getHandle();
    submitInfo.pCommandBuffers = &handle;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkFence fence = swapchain->getInFlightFence(currentFrameIndex);
    vkResetFences(device->getLogicalDevice(), 1, &fence);
    if (vkQueueSubmit(device->getGraphicsQueue(), 1, &submitInfo, fence) !=
        VK_SUCCESS) {
      return;
    }

    swapchain->present(currentFrameIndex, imageIndex);
    frameIndex++;
  }

  VulkanDevicePtr device = nullptr;
  VulkanResourceManagerPtr resourceManager = nullptr;
  VulkanSwapchainPtr swapchain = nullptr;
  VulkanCommandBufferManagerPtr cmdBufferMgr = nullptr;

  ScenePtr scene = nullptr;
  RenderItem renderItem{};
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

} // namespace LX_core::graphic_backend
