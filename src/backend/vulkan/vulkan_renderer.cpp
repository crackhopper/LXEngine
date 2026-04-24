#include "vulkan_renderer.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/pass.hpp"
#include "infra/gui/gui.hpp"
#include "infra/window/window.hpp"
#include "details/commands/command_buffer_manager.hpp"
#include "details/descriptors/descriptor_manager.hpp"
#include "details/render_objects/framebuffer.hpp"
#include "details/render_objects/render_pass.hpp"
#include "details/render_objects/swapchain.hpp"
#include "details/device.hpp"
#include "details/resource_manager.hpp"
#include "core/utils/env.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
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
    if (expRendererDebugEnabled()) {
      std::cerr << "[RendererDebug] toImageFormat: unknown VkFormat "
                << static_cast<int>(format) << ", falling back to RGBA8"
                << std::endl;
    }
    return LX_core::ImageFormat::RGBA8;
  }
}
} // namespace

namespace LX_core::backend {

namespace {

constexpr FrameIndex32 kMaxFramesInFlight = 3;

} // namespace

class VulkanRendererImpl {
public:
  VulkanRendererImpl() = default;
  ~VulkanRendererImpl() { destroy(); }

  void initialize(WindowSharedPtr _window, const char *appName) {
    m_window = _window;

    m_device = VulkanDevice::create();
    m_device->initialize(_window, appName);
    // Window backends return an allocated handle pointer (void*) for Vulkan.

    // Create command buffer manager first (needed for resource manager)
    m_cmdBufferMgr = VulkanCommandBufferManager::create(
        *m_device, kMaxFramesInFlight, m_device->getGraphicsQueueFamilyIndex());

    // Create resource manager
    m_resourceManager = VulkanResourceManager::create(*m_device);
    m_resourceManager->initializeRenderPassAndPipeline(
        m_device->getSurfaceFormat(), m_device->getDepthFormat());
    if (expEnvEnabled("LX_RENDER_DEBUG_CLEAR")) {
      m_resourceManager->getRenderPass().setClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    }

    m_swapchain =
        VulkanSwapchain::create(*m_device, _window, kMaxFramesInFlight);
    m_swapchain->initialize(m_resourceManager->getRenderPass());

    // REQ-017: bring up ImGui overlay inside the swapchain render pass.
    infra::Gui::InitParams guiParams{};
    guiParams.instance = m_device->getInstance();
    guiParams.physicalDevice = m_device->getPhysicalDevice();
    guiParams.device = m_device->getLogicalDevice();
    guiParams.graphicsQueueFamilyIndex = m_device->getGraphicsQueueFamilyIndex();
    guiParams.presentQueueFamilyIndex = m_device->getPresentQueueFamilyIndex();
    guiParams.graphicsQueue = m_device->getGraphicsQueue();
    guiParams.presentQueue = m_device->getPresentQueue();
    guiParams.surface = m_device->getSurface();
    guiParams.nativeWindowHandle = _window->getNativeHandle();
    guiParams.renderPass = m_resourceManager->getRenderPass().getHandle();
    guiParams.swapchainImageCount = m_swapchain->getImageCount();
    m_gui.init(guiParams);

    if (expRendererDebugEnabled()) {
      const VkExtent2D extent = m_swapchain->getExtent();
      std::cerr << "[RendererDebug] initialize: extent=" << extent.width << "x"
                << extent.height << ", maxFramesInFlight=" << kMaxFramesInFlight
                << std::endl;
      if (expEnvEnabled("LX_RENDER_DEBUG_CLEAR")) {
        std::cerr << "[RendererDebug] debug clear color enabled" << std::endl;
      }
      if (expEnvEnabled("LX_RENDER_DISABLE_CULL")) {
        std::cerr << "[RendererDebug] cull disabled" << std::endl;
      }
      if (expEnvEnabled("LX_RENDER_DISABLE_DEPTH")) {
        std::cerr << "[RendererDebug] depth disabled" << std::endl;
      }
      if (expEnvEnabled("LX_RENDER_FLIP_VIEWPORT_Y")) {
        std::cerr << "[RendererDebug] viewport Y flipped" << std::endl;
      }
    }
  }
  void shutdown() { destroy(); }

  /// REQ-009: derive the real swapchain RenderTarget from the Vulkan device's
  /// chosen surface format + depth format. This is the value that gets plugged
  /// into FramePass.target and also backfilled into any Camera whose m_target
  /// is nullopt at initScene time.
  LX_core::RenderTarget makeSwapchainTarget() const {
    LX_core::RenderTarget t{};
    t.colorFormat = toImageFormat(m_device->getSurfaceFormat().format);
    t.depthFormat = toImageFormat(m_device->getDepthFormat());
    t.sampleCount = 1;
    return t;
  }

  void initScene(SceneSharedPtr _scene) {
    m_scene = _scene;

    // REQ-009: compute the swapchain target once, use it for both:
    //   1. Backfilling any nullopt camera's m_target (before buildFromScene).
    //   2. Wiring up FramePass.target so getSceneLevelResources(pass, target)
    //      can match the camera on the filter side.
    const LX_core::RenderTarget swapchainTarget = makeSwapchainTarget();
    for (const auto &cam : m_scene->getCameras()) {
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
    m_frameGraph.buildFromScene(*m_scene);

    // Initial resource sync + per-draw payload seed for every item across
    // every pass in the FrameGraph.
    for (auto &pass : m_frameGraph.getPasses()) {
      for (auto &item : pass.queue.getItems()) {
        m_resourceManager->syncResource(*m_cmdBufferMgr, item.vertexBuffer);
        m_resourceManager->syncResource(*m_cmdBufferMgr, item.indexBuffer);
        for (auto &cpuRes : item.descriptorResources) {
          m_resourceManager->syncResource(*m_cmdBufferMgr, cpuRes);
        }
        if (item.drawData) {
          PerDrawLayoutBase pc{};
          pc.model = Mat4f::identity();
          item.drawData->update(pc);
        }
      }
    }
    m_resourceManager->collectGarbage();

    // Pre-build every pipeline the scene needs. Runtime cache misses still
    // work via getOrCreateRenderPipeline(item) but emit a warning log.
    auto infos = m_frameGraph.collectAllPipelineBuildDescs();
    m_resourceManager->preloadPipelines(infos);

    if (expRendererDebugEnabled()) {
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

  void uploadData() {
    for (auto &pass : m_frameGraph.getPasses()) {
      for (auto &item : pass.queue.getItems()) {
        m_resourceManager->syncResource(*m_cmdBufferMgr, item.vertexBuffer);
        m_resourceManager->syncResource(*m_cmdBufferMgr, item.indexBuffer);
        for (auto &cpuRes : item.descriptorResources) {
          m_resourceManager->syncResource(*m_cmdBufferMgr, cpuRes);
        }
      }
    }
    m_resourceManager->collectGarbage();
  }

  void draw() {
    // If the window has zero client area (minimized or in the middle of a
    // drag-resize on Windows), rebuilding or acquiring would either fail or
    // produce an invalid swapchain. Skip this frame cleanly; the next call
    // will retry once the window has non-zero size again.
    if (m_window && (m_window->getWidth() <= 0 || m_window->getHeight() <= 0)) {
      return;
    }

    const VkExtent2D extent = m_swapchain->getExtent();

    const FrameIndex32 currentFrameIndex =
        m_frameIndex % kMaxFramesInFlight;
    SwapchainImageIndex32 imageIndex = 0;

    VkResult acquireResult =
        m_swapchain->acquireNextImage(currentFrameIndex, imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR ||
        acquireResult == VK_SUBOPTIMAL_KHR) {
      // No queue submission will happen on this path, so keep the frame fence
      // signaled. Resetting it here would leave the next acquire blocked if
      // swapchain rebuild is deferred while the window is zero-sized.
      rebuildSwapchain();
      return;
    }
    if (acquireResult != VK_SUCCESS) {
      return;
    }

    auto &renderPass = m_resourceManager->getRenderPass();

    m_cmdBufferMgr->beginFrame(currentFrameIndex);
    m_device->getDescriptorManager().beginFrame(currentFrameIndex);

    auto cmd = m_cmdBufferMgr->allocateBuffer();
    cmd->begin();
    cmd->beginRenderPass(renderPass.getHandle(),
                         m_swapchain->getFramebuffer(imageIndex).getHandle(),
                         extent, renderPass.getClearValues());

    cmd->setViewport(extent.width, extent.height);
    cmd->setScissor(extent.width, extent.height);

    // REQ-017: overlay path. Kick off an ImGui frame *inside* the swapchain
    // render pass so the UI callback can emit widgets before scene draws,
    // and the final ImGui draw data is merged via endFrame(cmd) right before
    // endRenderPass.
    m_gui.beginFrame();
    if (m_drawUiCallback) {
      m_drawUiCallback();
    }

    // Iterate every pass × every item in the FrameGraph. Each item may use a
    // different pipeline; bindPipeline / bindResources / drawItem per item.
    for (auto &pass : m_frameGraph.getPasses()) {
      for (auto &item : pass.queue.getItems()) {
        auto &pipeline = m_resourceManager->getOrCreateRenderPipeline(item);
        cmd->bindPipeline(pipeline);
        cmd->bindResources(*m_resourceManager, pipeline, item);
        cmd->drawItem(item);
      }
    }

    m_gui.endFrame(cmd->getHandle());

    cmd->endRenderPass();
    cmd->end();

    VkSemaphore waitSemaphores[] = {
        m_swapchain->getImageAvailableSemaphore(currentFrameIndex)};
    VkSemaphore signalSemaphores[] = {
        m_swapchain->getRenderFinishedSemaphore(currentFrameIndex)};
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

    VkFence fence = m_swapchain->getInFlightFence(currentFrameIndex);
    vkResetFences(m_device->getLogicalDevice(), 1, &fence);
    if (vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, fence) !=
        VK_SUCCESS) {
      return;
    }

    VkResult presentResult =
        m_swapchain->present(currentFrameIndex, imageIndex);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR) {
      rebuildSwapchain();
      return;
    }

    m_frameIndex++;
  }

  void setDrawUiCallback(std::function<void()> cb) {
    m_drawUiCallback = std::move(cb);
  }

private:
  void rebuildSwapchain() {
    // A zero-sized window (minimized, or mid-drag) produces an invalid
    // swapchain. Let draw() retry later when the window has real size.
    if (m_window && (m_window->getWidth() <= 0 || m_window->getHeight() <= 0)) {
      return;
    }
    m_swapchain->waitIdle();
    m_swapchain->rebuild(m_resourceManager->getRenderPass());
    m_gui.updateSwapchainImageCount(m_swapchain->getImageCount());
  }

  void destroy() {
    if (m_device) {
      // 关键：等 GPU 干完活再删东西
      vkDeviceWaitIdle(m_device->getLogicalDevice());
    }
    // REQ-017: tear down ImGui before releasing Vulkan device so that
    // ImGui's descriptor pool / backend objects still see a live VkDevice.
    if (m_gui.isInitialized()) {
      m_gui.shutdown();
    }
    // 1. 销毁 Command Buffer Manager
    m_cmdBufferMgr.reset();
    // 2. 销毁 Swapchain
    m_swapchain.reset();
    // 3. 销毁 Resource Manager
    m_resourceManager.reset();
    // 4. 销毁 Device
    m_device.reset();
  }

  WindowSharedPtr m_window;
  VulkanDeviceUniquePtr m_device = nullptr;
  VulkanResourceManagerUniquePtr m_resourceManager = nullptr;
  VulkanSwapchainUniquePtr m_swapchain = nullptr;
  VulkanCommandBufferManagerUniquePtr m_cmdBufferMgr = nullptr;
  SceneSharedPtr m_scene = nullptr;
  LX_core::FrameGraph m_frameGraph{};
  FrameIndex32 m_frameIndex = 0;
  infra::Gui m_gui{};
  std::function<void()> m_drawUiCallback{};
};

VulkanRenderer::VulkanRenderer(Token)
    : p_impl(std::make_unique<VulkanRendererImpl>()) {}

VulkanRenderer::~VulkanRenderer() = default;

void VulkanRenderer::initialize(WindowSharedPtr window, const char *appName) {
  p_impl->initialize(window, appName);
}

void VulkanRenderer::shutdown() { p_impl->shutdown(); }

void VulkanRenderer::initScene(SceneSharedPtr scene) { p_impl->initScene(scene); }

void VulkanRenderer::uploadData() { p_impl->uploadData(); }

void VulkanRenderer::draw() { p_impl->draw(); }

void VulkanRenderer::setDrawUiCallback(std::function<void()> cb) {
  p_impl->setDrawUiCallback(std::move(cb));
}

} // namespace LX_core::backend
