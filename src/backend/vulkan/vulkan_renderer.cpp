#include "vulkan_renderer.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"
#include "infra/gui/gui.hpp"
#include "infra/window/window.hpp"
#include "details/commands/command_buffer_manager.hpp"
#include "details/descriptors/descriptor_manager.hpp"
#include "details/device.hpp"
#include "details/device_resources/buffer.hpp"
#include "details/device_resources/texture.hpp"
#include "details/render_objects/framebuffer.hpp"
#include "details/render_objects/render_pass.hpp"
#include "details/render_objects/swapchain.hpp"
#include "details/resource_manager.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
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

VkFormat toVkFormat(LX_core::ImageFormat format) {
  switch (format) {
  case LX_core::ImageFormat::RGBA8:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case LX_core::ImageFormat::BGRA8:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case LX_core::ImageFormat::R8:
    return VK_FORMAT_R8_UNORM;
  case LX_core::ImageFormat::D32Float:
    return VK_FORMAT_D32_SFLOAT;
  case LX_core::ImageFormat::D24UnormS8:
    return VK_FORMAT_D24_UNORM_S8_UINT;
  case LX_core::ImageFormat::D32FloatS8:
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
  }
  throw std::runtime_error("Unsupported ImageFormat");
}

std::string vkFormatName(VkFormat format) {
  switch (format) {
  case VK_FORMAT_D32_SFLOAT:
    return "D32_SFLOAT";
  case VK_FORMAT_D24_UNORM_S8_UINT:
    return "D24_UNORM_S8_UINT";
  case VK_FORMAT_D32_SFLOAT_S8_UINT:
    return "D32_SFLOAT_S8_UINT";
  case VK_FORMAT_R8G8B8A8_UNORM:
    return "R8G8B8A8_UNORM";
  case VK_FORMAT_B8G8R8A8_UNORM:
    return "B8G8R8A8_UNORM";
  default:
    return "VkFormat(" + std::to_string(static_cast<int>(format)) + ")";
  }
}

std::string sanitizeAttachmentName(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  for (const char c : name) {
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
    out.push_back(safe ? c : '_');
  }
  return out.empty() ? "attachment" : out;
}

LX_core::StringID passIdFromDebugName(std::string_view passName) {
  if (passName == "Forward" || passName == "forward") {
    return LX_core::Pass_Forward;
  }
  if (passName == "DebugOverlay" || passName == "debugOverlay" ||
      passName == "debug_overlay") {
    return LX_core::Pass_DebugOverlay;
  }
  throw std::runtime_error("unsupported debug render target pass: " +
                           std::string(passName));
}

void writeLe16(std::ostream &out, u16 value) {
  out.put(static_cast<char>(value & 0xffu));
  out.put(static_cast<char>((value >> 8u) & 0xffu));
}

void writeLe32(std::ostream &out, u32 value) {
  out.put(static_cast<char>(value & 0xffu));
  out.put(static_cast<char>((value >> 8u) & 0xffu));
  out.put(static_cast<char>((value >> 16u) & 0xffu));
  out.put(static_cast<char>((value >> 24u) & 0xffu));
}

void writeBmp24Header(std::ostream &out, u32 width, u32 height,
                      u32 pixelBytes) {
  constexpr u32 fileHeaderBytes = 14;
  constexpr u32 dibHeaderBytes = 40;
  writeLe16(out, 0x4d42u);
  writeLe32(out, fileHeaderBytes + dibHeaderBytes + pixelBytes);
  writeLe16(out, 0);
  writeLe16(out, 0);
  writeLe32(out, fileHeaderBytes + dibHeaderBytes);

  writeLe32(out, dibHeaderBytes);
  writeLe32(out, width);
  writeLe32(out, height);
  writeLe16(out, 1);
  writeLe16(out, 24);
  writeLe32(out, 0);
  writeLe32(out, pixelBytes);
  writeLe32(out, 2835);
  writeLe32(out, 2835);
  writeLe32(out, 0);
  writeLe32(out, 0);
}

void writeBmp24File(const std::filesystem::path &path, u32 width, u32 height,
                    const std::vector<unsigned char> &bgrPixels) {
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  const u32 rowBytes = width * 3u;
  const u32 paddedRowBytes = (rowBytes + 3u) & ~3u;
  const u32 paddingBytes = paddedRowBytes - rowBytes;
  const u32 pixelBytes = paddedRowBytes * height;

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("failed to open render target dump file: " +
                             path.string());
  }
  writeBmp24Header(out, width, height, pixelBytes);
  for (u32 y = 0; y < height; ++y) {
    const u32 srcY = height - 1u - y;
    const usize rowStart = static_cast<usize>(srcY) * width * 3u;
    for (u32 x = 0; x < rowBytes; ++x) {
      out.put(static_cast<char>(bgrPixels[rowStart + x]));
    }
    for (u32 p = 0; p < paddingBytes; ++p) {
      out.put('\0');
    }
  }
  if (!out) {
    throw std::runtime_error("failed to write render target dump file: " +
                             path.string());
  }
}
} // namespace

namespace LX_core::backend {

namespace {

constexpr u32 kMaxFramesInFlight = 3;

bool isSharedHostBufferResource(const IGpuResourceSharedPtr &resource) {
  if (!resource || !resource->isDirty()) {
    return false;
  }

  switch (resource->getType()) {
  case ResourceType::VertexBuffer:
  case ResourceType::IndexBuffer:
  case ResourceType::UniformBuffer:
  case ResourceType::StorageBuffer:
    return true;
  default:
    return false;
  }
}

VkImageLayout attachmentWriteLayout(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Color
             ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
             : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
}

VkPipelineStageFlags attachmentWriteStage(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Color
             ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
             : (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
}

VkAccessFlags attachmentWriteAccess(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Color
             ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
             : (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}

VkImageAspectFlags attachmentAspect(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Color ? VK_IMAGE_ASPECT_COLOR_BIT
                                                 : VK_IMAGE_ASPECT_DEPTH_BIT;
}

std::optional<std::reference_wrapper<const LX_core::FrameGraphWrite>>
findWriteForKind(const LX_core::CompiledFrameGraphPass &pass,
                 LX_core::FrameGraphAttachmentKind kind) {
  std::optional<std::reference_wrapper<const LX_core::FrameGraphWrite>> found;
  for (const auto &write : pass.writes) {
    if (write.resource.kind != kind) {
      continue;
    }
    if (found.has_value()) {
      throw std::runtime_error(
          "Frame graph pass declares duplicate writes for one attachment kind");
    }
    found = std::cref(write);
  }
  return found;
}

void validateOffscreenWritesMatchTarget(
    const LX_core::CompiledFrameGraphPass &pass) {
  const auto colorWrite =
      findWriteForKind(pass, LX_core::FrameGraphAttachmentKind::Color);
  const auto depthWrite =
      findWriteForKind(pass, LX_core::FrameGraphAttachmentKind::Depth);

  if (pass.target.colorFormat.has_value() != colorWrite.has_value()) {
    throw std::runtime_error(
        "Frame graph offscreen pass color write does not match target");
  }
  if (pass.target.depthFormat.has_value() != depthWrite.has_value()) {
    throw std::runtime_error(
        "Frame graph offscreen pass depth write does not match target");
  }
}

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
    guiParams.graphicsQueueFamilyIndex =
        m_device->getGraphicsQueueFamilyIndex();
    guiParams.presentQueueFamilyIndex = m_device->getPresentQueueFamilyIndex();
    guiParams.graphicsQueue = m_device->getGraphicsQueue();
    guiParams.presentQueue = m_device->getPresentQueue();
    guiParams.surface = m_device->getSurface();
    guiParams.nativeWindowHandle = _window->getNativeHandle();
    guiParams.renderPass = m_resourceManager->getRenderPass().getHandle();
    guiParams.swapchainImageCount = m_swapchain->getImageCount();
    m_gui.init(guiParams);
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
    ++m_initSceneCallCount;
    if (m_swapchain) {
      m_swapchain->waitForAllFrames();
    }
    m_scene = _scene;

    // REQ-009: compute the swapchain target once, use it for both:
    //   1. Backfilling any nullopt camera's m_target (before buildFromScene).
    //   2. Wiring up FramePass.target so getSceneLevelResources(pass, target)
    //      can match the camera on the filter side.
    const LX_core::RenderTarget swapchainTarget = makeSwapchainTarget();
    for (const auto &cameraNode : m_scene->getCameras()) {
      if (!cameraNode) {
        continue;
      }
      const auto cameraComponent =
          cameraNode->getComponent<LX_core::CameraComponent>();
      if (cameraComponent && !cameraComponent->get().getTarget().has_value()) {
        cameraComponent->get().setTarget(swapchainTarget);
      }
    }

    const auto swapchainDesc = swapchainTarget.toDesc();
    const auto shadowTarget =
        LX_core::RenderTargetDesc::offscreenDepth(swapchainTarget.depthFormat);
    updateDirectionalLightCascades();
    const auto swapchainColor = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("swapchain.color"));
    const auto swapchainDepth = LX_core::FrameGraphResourceRef::depthAttachment(
        LX_core::StringID("swapchain.depth"));

    m_frameGraph = LX_core::FrameGraph{}; // Fresh graph on every initScene.
    std::vector<LX_core::FrameGraphRead> shadowReads;
    shadowReads.reserve(LX_core::MaxShadowCascades);
    for (u32 cascadeIndex = 0; cascadeIndex < LX_core::MaxShadowCascades;
         ++cascadeIndex) {
      const auto shadowDepth = LX_core::FrameGraphResourceRef::depthAttachment(
          LX_core::StringID("shadow.cascade" + std::to_string(cascadeIndex)));
      m_frameGraph.addPass(
          LX_core::FramePass{LX_core::Pass_Shadow,
                             shadowTarget,
                             {},
                             {},
                             {LX_core::FrameGraphWrite{shadowDepth}}});
      shadowReads.push_back(LX_core::FrameGraphRead::sampled(
          shadowDepth.name,
          LX_core::StringID("ShadowMap" + std::to_string(cascadeIndex))));
    }
    m_frameGraph.addPass(
        LX_core::FramePass{LX_core::Pass_Forward,
                           swapchainDesc,
                           {},
                           shadowReads,
                           {LX_core::FrameGraphWrite{swapchainColor},
                            LX_core::FrameGraphWrite{swapchainDepth}}});
    m_frameGraph.addPass(LX_core::FramePass{
        LX_core::Pass_DebugOverlay, swapchainDesc, {}, {}, {}});

    // RenderQueue::buildFromScene (invoked per pass below) internally:
    //   - filters renderables by supportsPass(pass)
    //   - merges scene.getSceneLevelResources(pass, target) (camera UBO
    //   filtered by
    //     target, light UBO filtered by pass mask)
    //   - sorts by PipelineKey
    // There is no more side-channel camera/light UBO injection here.
    m_frameGraph.buildFromScene(*m_scene);

    m_compiledFrameGraph = m_frameGraph.compile();
    if (!m_compiledFrameGraph.isValid()) {
      throw std::runtime_error(m_compiledFrameGraph.errorText());
    }
    attachFrameGraphSampledResources();
    resetOffscreenFramebuffers();
    m_resourceManager->clearFrameGraphAttachments();

    // Initial resource sync for every item across every pass in the FrameGraph.
    // SceneNode::getValidatedPassData() has already synced each per-draw model
    // matrix from the node world transform while building the queue.
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

    // Pre-build every pipeline the scene needs. Runtime cache misses still
    // work via getOrCreateRenderPipeline(item) but emit a warning log.
    auto infos = m_frameGraph.collectAllPipelineBuildDescs();
    m_resourceManager->preloadPipelines(infos);
  }

  void uploadData() {
    const u32 currentFrameIndex = m_frameIndex % kMaxFramesInFlight;
    m_resourceManager->beginFrame(currentFrameIndex);
    bool requiresSharedBufferSync = false;
    for (auto &pass : m_frameGraph.getPasses()) {
      for (auto &item : pass.queue.getItems()) {
        requiresSharedBufferSync =
            requiresSharedBufferSync ||
            isSharedHostBufferResource(item.vertexBuffer) ||
            isSharedHostBufferResource(item.indexBuffer);
        for (auto &cpuRes : item.descriptorResources) {
          requiresSharedBufferSync =
              requiresSharedBufferSync || isSharedHostBufferResource(cpuRes);
        }
        if (requiresSharedBufferSync) {
          break;
        }
      }
      if (requiresSharedBufferSync) {
        break;
      }
    }

    if (requiresSharedBufferSync) {
      // These buffers are single shared allocations, not per-frame slices.
      // Wait until every in-flight frame that could still read them has
      // completed before overwriting their contents from the CPU.
      m_swapchain->waitForAllFrames();
    }

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
    if (m_swapchainNeedsRebuild) {
      rebuildSwapchain();
      return;
    }

    // If the window has zero client area (minimized or in the middle of a
    // drag-resize on Windows), rebuilding or acquiring would either fail or
    // produce an invalid swapchain. Skip this frame cleanly; the next call
    // will retry once the window has non-zero size again.
    if (m_window && (m_window->getWidth() <= 0 || m_window->getHeight() <= 0)) {
      return;
    }

    const VkExtent2D extent = m_swapchain->getExtent();

    const u32 currentFrameIndex = m_frameIndex % kMaxFramesInFlight;
    u32 imageIndex = 0;

    VkResult acquireResult =
        m_swapchain->acquireNextImage(currentFrameIndex, imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR ||
        acquireResult == VK_SUBOPTIMAL_KHR) {
      m_swapchainNeedsRebuild = true;
      // No queue submission will happen on this path, so keep the frame fence
      // signaled. Resetting it here would leave the next acquire blocked if
      // swapchain rebuild is deferred while the window is zero-sized.
      rebuildSwapchain();
      return;
    }
    if (acquireResult != VK_SUCCESS) {
      std::cerr
          << "[VulkanRenderer] vkAcquireNextImageKHR failed with VkResult="
          << static_cast<int>(acquireResult) << std::endl;
      return;
    }

    m_cmdBufferMgr->beginFrame(currentFrameIndex);
    m_device->getDescriptorManager().beginFrame(currentFrameIndex);
    m_resourceManager->beginFrame(currentFrameIndex);

    auto cmd = m_cmdBufferMgr->allocateBuffer();
    cmd->begin();

    const bool skipGuiFrame = expEnvEnabled("LX_RENDER_SKIP_GUI_FRAME") ||
                              m_pendingScreenDump.has_value();

    bool swapchainRenderPassActive = false;
    bool guiFrameActive = false;
    const usize finalSwapchainPassIndex = findFinalSwapchainPassIndex();
    const usize finalSwapchainGroupStartIndex =
        findFinalSwapchainGroupStartIndex(finalSwapchainPassIndex);

    const auto &compiledPasses = m_compiledFrameGraph.getPasses();
    for (usize passIndex = 0; passIndex < compiledPasses.size(); ++passIndex) {
      const auto &compiledPass = compiledPasses[passIndex];
      if (compiledPass.target.role == LX_core::RenderTargetRole::Swapchain) {
        const bool isFinalSwapchainGroup =
            finalSwapchainPassIndex != compiledPasses.size() &&
            passIndex >= finalSwapchainGroupStartIndex &&
            passIndex <= finalSwapchainPassIndex;
        if (!swapchainRenderPassActive) {
          auto &renderPass = m_resourceManager->getRenderPass();
          cmd->beginRenderPass(
              renderPass.getHandle(),
              m_swapchain->getFramebuffer(imageIndex).getHandle(), extent,
              renderPass.getClearValues());
          cmd->setViewport(extent.width, extent.height);
          cmd->setScissor(extent.width, extent.height);
          swapchainRenderPassActive = true;
        }

        if (isFinalSwapchainGroup &&
            passIndex == finalSwapchainGroupStartIndex && !skipGuiFrame) {
          m_gui.beginFrame();
          guiFrameActive = true;
          if (m_drawUiCallback) {
            m_drawUiCallback();
          }
        }

        drawPassQueue(passIndex, *cmd);

        if (!isFinalSwapchainGroup || passIndex == finalSwapchainPassIndex) {
          if (guiFrameActive) {
            m_gui.endFrame(cmd->getHandle());
            guiFrameActive = false;
          }
          cmd->endRenderPass();
          swapchainRenderPassActive = false;
        }
        continue;
      }

      if (swapchainRenderPassActive) {
        if (guiFrameActive) {
          m_gui.endFrame(cmd->getHandle());
          guiFrameActive = false;
        }
        cmd->endRenderPass();
        swapchainRenderPassActive = false;
      }

      prepareShadowCascadePass(passIndex);
      const VkExtent2D passExtent = prepareOffscreenPass(
          passIndex, currentFrameIndex, compiledPass, extent, *cmd);
      auto &renderPass = m_resourceManager->getRenderPass(compiledPass.target);
      cmd->beginRenderPass(
          renderPass.getHandle(),
          m_offscreenFramebuffers[passIndex][currentFrameIndex]->getHandle(),
          passExtent, renderPass.getClearValues());
      cmd->setViewport(passExtent.width, passExtent.height);
      cmd->setScissor(passExtent.width, passExtent.height);
      drawPassQueue(passIndex, *cmd);
      cmd->endRenderPass();
      transitionPassWritesToShaderRead(compiledPass, *cmd);
    }

    if (swapchainRenderPassActive) {
      if (guiFrameActive) {
        m_gui.endFrame(cmd->getHandle());
      }
      cmd->endRenderPass();
    }
    recordPendingScreenDump(imageIndex, extent, *cmd);
    cmd->end();

    VkSemaphore waitSemaphores[] = {
        m_swapchain->getImageAvailableSemaphore(currentFrameIndex)};
    VkSemaphore signalSemaphores[] = {
        m_swapchain->getRenderFinishedSemaphore(imageIndex)};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT};

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
    const VkResult submitResult =
        vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, fence);
    if (submitResult != VK_SUCCESS) {
      consumeAcquireSemaphoreAndSignalFenceAfterFailedSubmit(
          waitSemaphores[0], waitStages[0], fence);
      std::cerr << "[VulkanRenderer] vkQueueSubmit failed with VkResult="
                << static_cast<int>(submitResult) << std::endl;
      return;
    }

    writeCompletedScreenDumpIfNeeded(fence);

    VkResult presentResult = m_swapchain->present(imageIndex);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR) {
      m_swapchainNeedsRebuild = true;
      rebuildSwapchain();
      return;
    }
    if (presentResult != VK_SUCCESS) {
      std::cerr << "[VulkanRenderer] vkQueuePresentKHR failed with VkResult="
                << static_cast<int>(presentResult) << std::endl;
    }

    m_frameIndex++;
  }

  void setDrawUiCallback(std::function<void()> cb) {
    m_drawUiCallback = std::move(cb);
  }

  [[nodiscard]] usize cachedResourceCount() const {
    return m_resourceManager ? m_resourceManager->getCachedResourceCount() : 0;
  }

  [[nodiscard]] usize frameGraphItemCount() const {
    usize total = 0;
    for (const auto &pass : m_frameGraph.getPasses()) {
      total += pass.queue.getItems().size();
    }
    return total;
  }

  [[nodiscard]] usize compiledFrameGraphPassCount() const {
    return m_compiledFrameGraph.getPasses().size();
  }

  [[nodiscard]] usize frameGraphAttachmentCount() const {
    return m_resourceManager ? m_resourceManager->getFrameGraphAttachmentCount()
                             : 0;
  }

  [[nodiscard]] usize initSceneCallCount() const {
    return m_initSceneCallCount;
  }

  VulkanRenderer::FrameGraphAttachmentDumpResult dumpFrameGraphAttachment(
      std::string_view attachmentName,
      const std::optional<std::filesystem::path> &requestedPath,
      const std::optional<std::filesystem::path> &requestedScreenPath) {
    if (!m_resourceManager || !m_cmdBufferMgr || !m_device) {
      throw std::runtime_error("renderer is not initialized");
    }

    const StringID attachmentId{std::string(attachmentName)};
    auto attachmentOpt =
        m_resourceManager->getFrameGraphAttachment(attachmentId);
    if (!attachmentOpt.has_value()) {
      throw std::runtime_error("frame graph attachment not available: " +
                               std::string(attachmentName));
    }
    auto &attachment = attachmentOpt->get();
    if (attachment.format != VK_FORMAT_D32_SFLOAT) {
      throw std::runtime_error("render debug dump only supports D32_SFLOAT "
                               "depth attachments for now; got " +
                               vkFormatName(attachment.format));
    }

    const u32 width = attachment.extent.width;
    const u32 height = attachment.extent.height;
    const VkDeviceSize byteSize =
        static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) *
        sizeof(float);
    if (width == 0 || height == 0 || byteSize == 0) {
      throw std::runtime_error("frame graph attachment has empty extent: " +
                               std::string(attachmentName));
    }

    const auto timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    const std::filesystem::path path = requestedPath.value_or(
        std::filesystem::path("data/debug/dump") /
        (std::to_string(timestamp) + "-" +
         sanitizeAttachmentName(attachmentName) + ".bmp"));
    (void)requestedScreenPath;

    auto readback = VulkanBuffer::create(
        *m_device, byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    m_device->waitIdle();
    const VkImageLayout previousLayout = attachment.currentLayout;
    auto cmd = m_cmdBufferMgr->beginSingleTimeCommands();
    transitionFrameGraphAttachment(
        LX_core::FrameGraphResourceRef{attachmentId,
                                       LX_core::FrameGraphAttachmentKind::Depth},
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, *cmd);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd->getHandle(), attachment.texture->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback->getHandle(), 1, &region);

    const bool restoreShaderRead =
        previousLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    transitionFrameGraphAttachment(
        LX_core::FrameGraphResourceRef{attachmentId,
                                       LX_core::FrameGraphAttachmentKind::Depth},
        previousLayout,
        restoreShaderRead ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                          : (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT),
        restoreShaderRead ? VK_ACCESS_SHADER_READ_BIT
                          : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        *cmd);
    m_cmdBufferMgr->endSingleTimeCommands(std::move(cmd),
                                          m_device->getGraphicsQueue());

    const auto *depthPixels = static_cast<const float *>(readback->map());
    std::vector<unsigned char> bgrPixels;
    bgrPixels.reserve(static_cast<usize>(width) * static_cast<usize>(height) *
                      3u);
    for (u32 y = 0; y < height; ++y) {
      for (u32 x = 0; x < width; ++x) {
        const float depth = std::clamp(
            depthPixels[static_cast<usize>(y) * width + x], 0.0f, 1.0f);
        const auto gray = static_cast<unsigned char>(depth * 255.0f);
        bgrPixels.push_back(gray);
        bgrPixels.push_back(gray);
        bgrPixels.push_back(gray);
      }
    }
    readback->unmap();

    writeBmp24File(path, width, height, bgrPixels);

    return VulkanRenderer::FrameGraphAttachmentDumpResult{
        .path = path,
        .screenPath = {},
        .width = width,
        .height = height,
        .format = vkFormatName(attachment.format),
    };
  }

  VulkanRenderer::FrameGraphAttachmentDumpResult dumpDebugRenderTarget(
      std::string_view passName,
      const std::optional<std::string> &cameraPath,
      const std::optional<std::filesystem::path> &requestedPath) {
    if (!m_resourceManager || !m_cmdBufferMgr || !m_device || !m_swapchain ||
        !m_scene) {
      throw std::runtime_error("renderer is not initialized");
    }

    const StringID pass = passIdFromDebugName(passName);
    auto camera = cameraForDebugDump(cameraPath);
    if (!camera.has_value()) {
      throw std::runtime_error("debug render target camera not found");
    }

    const auto timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    const std::filesystem::path path = requestedPath.value_or(
        std::filesystem::path("data/debug/dump") /
        (std::to_string(timestamp) + "-" + sanitizeAttachmentName(passName) +
         ".bmp"));

    LX_core::RenderTargetDesc targetDesc;
    targetDesc.role = LX_core::RenderTargetRole::Offscreen;
    targetDesc.colorFormat = LX_core::ImageFormat::BGRA8;
    targetDesc.depthFormat = LX_core::ImageFormat::D32Float;
    const LX_core::RenderTarget target{targetDesc};

    auto &cameraComponent = camera->get();
    const auto previousTarget = cameraComponent.getTarget();
    cameraComponent.setTarget(target);
    cameraComponent.updateMatrices();
    auto sceneResources = m_scene->getSceneLevelResources(pass, target);
    cameraComponent.setTarget(previousTarget);

    if (pass == LX_core::Pass_Forward) {
      for (u32 cascadeIndex = 0; cascadeIndex < LX_core::MaxShadowCascades;
           ++cascadeIndex) {
        const auto shadowDepth = LX_core::FrameGraphResourceRef::depthAttachment(
            LX_core::StringID("shadow.cascade" +
                              std::to_string(cascadeIndex)));
        sceneResources.push_back(
            std::make_shared<LX_core::FrameGraphSampledResource>(
                shadowDepth.name,
                LX_core::StringID("ShadowMap" + std::to_string(cascadeIndex))));
      }
    }

    LX_core::RenderQueue queue;
    queue.buildFromSceneWithOverrides(
        *m_scene, pass, target, std::move(sceneResources),
        cameraComponent.getCullingMask() & ~LX_core::Layer_EditorOverlay);
    if (queue.getItems().empty()) {
      throw std::runtime_error("debug render target produced no draw items");
    }

    for (auto &item : queue.getItems()) {
      m_resourceManager->syncResource(*m_cmdBufferMgr, item.vertexBuffer);
      m_resourceManager->syncResource(*m_cmdBufferMgr, item.indexBuffer);
      for (auto &cpuRes : item.descriptorResources) {
        m_resourceManager->syncResource(*m_cmdBufferMgr, cpuRes);
      }
    }
    m_resourceManager->preloadPipelines(queue.collectUniquePipelineBuildDescs());

    const VkExtent2D extent = m_swapchain->getExtent();
    const auto colorRef = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("debug.dump.color." + std::to_string(timestamp)));
    const auto depthRef = LX_core::FrameGraphResourceRef::depthAttachment(
        LX_core::StringID("debug.dump.depth." + std::to_string(timestamp)));

    const VkDeviceSize byteSize =
        static_cast<VkDeviceSize>(extent.width) *
        static_cast<VkDeviceSize>(extent.height) * 4u;
    auto readback = VulkanBuffer::create(
        *m_device, byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    m_device->waitIdle();
    auto cmd = m_cmdBufferMgr->beginSingleTimeCommands();

    auto &colorAttachment = m_resourceManager->createOrGetFrameGraphAttachment(
        colorRef.name, extent, VK_FORMAT_B8G8R8A8_UNORM,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT);
    auto &depthAttachment = m_resourceManager->createOrGetFrameGraphAttachment(
        depthRef.name, extent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    transitionFrameGraphAttachment(colorRef,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, *cmd);
    transitionFrameGraphAttachment(
        depthRef, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, *cmd);

    std::vector<VkImageView> attachments{
        colorAttachment.texture->getImageView(),
        depthAttachment.texture->getImageView(),
    };
    auto &renderPass = m_resourceManager->getRenderPass(targetDesc);
    auto framebuffer = VulkanFrameBuffer::create(
        *m_device, renderPass.getHandle(), attachments, extent);

    cmd->beginRenderPass(renderPass.getHandle(), framebuffer->getHandle(),
                         extent, renderPass.getClearValues());
    cmd->setViewport(extent.width, extent.height);
    cmd->setScissor(extent.width, extent.height);
    for (auto &item : queue.getItems()) {
      auto &pipeline = m_resourceManager->getOrCreateRenderPipeline(item);
      cmd->bindPipeline(pipeline);
      cmd->bindResources(*m_resourceManager, pipeline, item);
      cmd->drawItem(item);
    }
    cmd->endRenderPass();

    transitionFrameGraphAttachment(colorRef,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_ACCESS_TRANSFER_READ_BIT, *cmd);
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd->getHandle(),
                           colorAttachment.texture->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback->getHandle(), 1, &region);
    m_cmdBufferMgr->endSingleTimeCommands(std::move(cmd),
                                          m_device->getGraphicsQueue());

    const auto *rgba = static_cast<const unsigned char *>(readback->map());
    std::vector<unsigned char> bgrPixels;
    bgrPixels.reserve(static_cast<usize>(extent.width) *
                      static_cast<usize>(extent.height) * 3u);
    for (u32 y = 0; y < extent.height; ++y) {
      for (u32 x = 0; x < extent.width; ++x) {
        const usize i =
            (static_cast<usize>(y) * extent.width + static_cast<usize>(x)) *
            4u;
        bgrPixels.push_back(rgba[i + 0u]);
        bgrPixels.push_back(rgba[i + 1u]);
        bgrPixels.push_back(rgba[i + 2u]);
      }
    }
    readback->unmap();

    writeBmp24File(path, extent.width, extent.height, bgrPixels);
    return VulkanRenderer::FrameGraphAttachmentDumpResult{
        .path = path,
        .screenPath = {},
        .width = extent.width,
        .height = extent.height,
        .format = vkFormatName(VK_FORMAT_B8G8R8A8_UNORM),
    };
  }

private:
  struct PendingScreenDump final {
    std::filesystem::path path;
    VulkanBufferUniquePtr readback;
    u32 width = 0;
    u32 height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
  };

  usize findFinalSwapchainPassIndex() const {
    const auto &passes = m_compiledFrameGraph.getPasses();
    for (usize i = passes.size(); i > 0; --i) {
      if (passes[i - 1].target.role == LX_core::RenderTargetRole::Swapchain) {
        return i - 1;
      }
    }
    return passes.size();
  }

  usize findFinalSwapchainGroupStartIndex(usize finalSwapchainPassIndex) const {
    const auto &passes = m_compiledFrameGraph.getPasses();
    if (finalSwapchainPassIndex >= passes.size()) {
      return passes.size();
    }

    usize start = finalSwapchainPassIndex;
    while (start > 0 && passes[start - 1].target.role ==
                            LX_core::RenderTargetRole::Swapchain) {
      --start;
    }

    // Legacy VkRenderPass cannot preserve forward color for debug/GUI overlay
    // if we end and begin again with the current clear/load contract. Only the
    // final contiguous swapchain run is intentionally grouped under one active
    // VkRenderPass; offscreen passes and non-final swapchain passes remain
    // one begin/end per compiled pass.
    return start;
  }

  void drawPassQueue(usize passIndex, VulkanCommandBuffer &cmd) {
    if (passIndex >= m_frameGraph.getPasses().size()) {
      return;
    }

    for (auto &item : m_frameGraph.getPasses()[passIndex].queue.getItems()) {
      auto &pipeline = m_resourceManager->getOrCreateRenderPipeline(item);
      cmd.bindPipeline(pipeline);
      cmd.bindResources(*m_resourceManager, pipeline, item);
      cmd.drawItem(item);
    }
  }

  LX_core::DirectionalLightSharedPtr mainDirectionalLight() const {
    if (!m_scene) {
      return nullptr;
    }
    LX_core::DirectionalLightSharedPtr fallback;
    for (const auto &light : m_scene->getLights()) {
      if (auto directional =
              std::dynamic_pointer_cast<LX_core::DirectionalLight>(light)) {
        if (!fallback && directional->supportsPass(LX_core::Pass_Shadow)) {
          fallback = directional;
        }
        if (directional->supportsPass(LX_core::Pass_Shadow) &&
            directional->getSceneNode()) {
          return directional;
        }
      }
    }
    return fallback;
  }

  std::optional<std::reference_wrapper<LX_core::CameraComponent>>
  mainCameraComponent() const {
    if (!m_scene) {
      return std::nullopt;
    }
    for (const auto &cameraNode : m_scene->getCameras()) {
      if (!cameraNode) {
        continue;
      }
      auto camera = cameraNode->getComponent<LX_core::CameraComponent>();
      if (camera && camera->get().isActive()) {
        return camera->get();
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<LX_core::CameraComponent>>
  cameraForDebugDump(const std::optional<std::string> &cameraPath) const {
    if (!m_scene) {
      return std::nullopt;
    }
    if (cameraPath.has_value() && !cameraPath->empty()) {
      LX_core::SceneNode *node = m_scene->findByPath(*cameraPath);
      if (!node) {
        return std::nullopt;
      }
      auto camera = node->getComponent<LX_core::CameraComponent>();
      if (!camera) {
        return std::nullopt;
      }
      return camera->get();
    }
    return mainCameraComponent();
  }

  void updateDirectionalLightCascades() {
    const auto light = mainDirectionalLight();
    auto camera = mainCameraComponent();
    if (!light || !camera.has_value()) {
      return;
    }
    light->updateShadowCascadesForCamera(camera->get());
  }

  void prepareShadowCascadePass(usize passIndex) {
    if (passIndex >= m_compiledFrameGraph.getPasses().size()) {
      return;
    }
    const auto &pass = m_compiledFrameGraph.getPasses()[passIndex];
    if (pass.name != LX_core::Pass_Shadow) {
      return;
    }
    const auto light = mainDirectionalLight();
    if (!light) {
      return;
    }
    u32 cascadeIndex = 0;
    for (usize i = 0; i < passIndex; ++i) {
      if (m_compiledFrameGraph.getPasses()[i].name == LX_core::Pass_Shadow) {
        ++cascadeIndex;
      }
    }
    light->setActiveShadowCascade(cascadeIndex);
    m_resourceManager->syncResource(*m_cmdBufferMgr, light->getUBO());
  }

  void attachFrameGraphSampledResources() {
    const auto &compiledPasses = m_compiledFrameGraph.getPasses();
    auto &graphPasses = m_frameGraph.getPasses();
    const usize passCount = std::min(compiledPasses.size(), graphPasses.size());
    for (usize passIndex = 0; passIndex < passCount; ++passIndex) {
      for (const auto &read : compiledPasses[passIndex].reads) {
        if (read.bindingName == LX_core::StringID{}) {
          continue;
        }
        auto resource = std::make_shared<LX_core::FrameGraphSampledResource>(
            read.resource, read.bindingName);
        for (auto &item : graphPasses[passIndex].queue.getItems()) {
          item.descriptorResources.push_back(resource);
        }
      }
    }
  }

  void resetOffscreenFramebuffers() {
    m_offscreenFramebuffers.clear();
    m_offscreenFramebuffers.resize(m_compiledFrameGraph.getPasses().size());
    for (auto &passFramebuffers : m_offscreenFramebuffers) {
      passFramebuffers.resize(kMaxFramesInFlight);
    }
  }

  void consumeAcquireSemaphoreAndSignalFenceAfterFailedSubmit(
      VkSemaphore imageAvailableSemaphore, VkPipelineStageFlags waitStage,
      VkFence fence) {
    // The per-frame fence is reset immediately before queue submission. If the
    // real submit fails, the next acquire would block forever on that
    // unsignaled fence. The acquired image semaphore is already signaled, so
    // the recovery submit must wait on it before this frame slot can be reused.
    VkSubmitInfo recoverySubmit{};
    recoverySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    recoverySubmit.waitSemaphoreCount = 1;
    recoverySubmit.pWaitSemaphores = &imageAvailableSemaphore;
    recoverySubmit.pWaitDstStageMask = &waitStage;
    const VkResult recoveryResult =
        vkQueueSubmit(m_device->getGraphicsQueue(), 1, &recoverySubmit, fence);
    if (recoveryResult != VK_SUCCESS) {
      std::cerr
          << "[VulkanRenderer] failed to consume acquired semaphore and "
             "re-signal in-flight fence after submit failure; VkResult="
          << static_cast<int>(recoveryResult) << std::endl;
      m_swapchainNeedsRebuild = true;
    }
  }

  void transitionFrameGraphAttachment(
      const LX_core::FrameGraphResourceRef &resource, VkImageLayout newLayout,
      VkPipelineStageFlags dstStage, VkAccessFlags dstAccess,
      VulkanCommandBuffer &cmd) {
    auto attachmentOpt =
        m_resourceManager->getFrameGraphAttachment(resource.name);
    if (!attachmentOpt.has_value()) {
      throw std::runtime_error(
          "Frame graph attachment missing during layout transition");
    }
    auto &attachment = attachmentOpt->get();
    if (attachment.currentLayout == newLayout) {
      return;
    }

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags srcAccess = 0;
    if (attachment.currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      srcAccess = VK_ACCESS_SHADER_READ_BIT;
    } else if (attachment.currentLayout ==
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
      srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (attachment.currentLayout ==
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      srcStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    } else if (attachment.currentLayout ==
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
      srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = attachment.currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = attachment.texture->getHandle();
    barrier.subresourceRange.aspectMask = attachment.aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    cmd.pipelineBarrier(srcStage, dstStage, barrier);
    attachment.currentLayout = newLayout;
  }

  VkExtent2D prepareOffscreenPass(usize passIndex, u32 currentFrameIndex,
                                  const LX_core::CompiledFrameGraphPass &pass,
                                  VkExtent2D fallbackExtent,
                                  VulkanCommandBuffer &cmd) {
    if (pass.target.role == LX_core::RenderTargetRole::Swapchain) {
      return fallbackExtent;
    }

    validateOffscreenWritesMatchTarget(pass);

    std::vector<VkImageView> attachments;
    attachments.reserve(2);
    const auto appendAttachment = [&](const LX_core::FrameGraphWrite &write,
                                      LX_core::ImageFormat format) {
      const auto kind = write.resource.kind;
      const VkImageUsageFlags usage =
          kind == LX_core::FrameGraphAttachmentKind::Color
              ? (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
              : (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
      auto &attachment = m_resourceManager->createOrGetFrameGraphAttachment(
          write.resource.name, fallbackExtent, toVkFormat(format),
          attachmentAspect(kind), usage);
      transitionFrameGraphAttachment(
          write.resource, attachmentWriteLayout(kind),
          attachmentWriteStage(kind), attachmentWriteAccess(kind), cmd);
      attachments.push_back(attachment.texture->getImageView());
    };

    if (pass.target.colorFormat.has_value()) {
      const auto write =
          findWriteForKind(pass, LX_core::FrameGraphAttachmentKind::Color);
      appendAttachment(write->get(), *pass.target.colorFormat);
    }
    if (pass.target.depthFormat.has_value()) {
      const auto write =
          findWriteForKind(pass, LX_core::FrameGraphAttachmentKind::Depth);
      appendAttachment(write->get(), *pass.target.depthFormat);
    }

    auto &framebuffer = m_offscreenFramebuffers[passIndex][currentFrameIndex];
    if (!framebuffer) {
      auto &renderPass = m_resourceManager->getRenderPass(pass.target);
      framebuffer = VulkanFrameBuffer::create(*m_device, renderPass.getHandle(),
                                              attachments, fallbackExtent);
    }
    return fallbackExtent;
  }

  void
  transitionPassWritesToShaderRead(const LX_core::CompiledFrameGraphPass &pass,
                                   VulkanCommandBuffer &cmd) {
    for (const auto &write : pass.writes) {
      if (pass.target.role == LX_core::RenderTargetRole::Swapchain) {
        continue;
      }
      transitionFrameGraphAttachment(write.resource,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     VK_ACCESS_SHADER_READ_BIT, cmd);
    }
  }

  void recordPendingScreenDump(u32 imageIndex, VkExtent2D extent,
                               VulkanCommandBuffer &cmd) {
    if (!m_pendingScreenDump.has_value()) {
      return;
    }
    auto &dump = *m_pendingScreenDump;
    dump.width = extent.width;
    dump.height = extent.height;
    dump.format = m_swapchain->getImageFormat();
    const VkDeviceSize byteSize =
        static_cast<VkDeviceSize>(extent.width) *
        static_cast<VkDeviceSize>(extent.height) * 4u;
    dump.readback = VulkanBuffer::create(
        *m_device, byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = m_swapchain->getImage(imageIndex);
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel = 0;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.baseArrayLayer = 0;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    cmd.pipelineBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, toTransfer);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd.getHandle(), m_swapchain->getImage(imageIndex),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           dump.readback->getHandle(), 1, &region);

    VkImageMemoryBarrier toPresent = toTransfer;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = 0;
    cmd.pipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, toPresent);
  }

  void writeCompletedScreenDumpIfNeeded(VkFence fence) {
    if (!m_pendingScreenDump.has_value() ||
        !m_pendingScreenDump->readback) {
      return;
    }

    auto dump = std::move(*m_pendingScreenDump);
    m_pendingScreenDump.reset();
    vkWaitForFences(m_device->getLogicalDevice(), 1, &fence, VK_TRUE,
                    UINT64_MAX);

    const auto *rgba = static_cast<const unsigned char *>(dump.readback->map());
    std::vector<unsigned char> bgrPixels;
    bgrPixels.reserve(static_cast<usize>(dump.width) *
                      static_cast<usize>(dump.height) * 3u);
    const bool sourceIsBgra =
        dump.format == VK_FORMAT_B8G8R8A8_UNORM ||
        dump.format == VK_FORMAT_B8G8R8A8_SRGB;
    for (u32 y = 0; y < dump.height; ++y) {
      for (u32 x = 0; x < dump.width; ++x) {
        const usize i =
            (static_cast<usize>(y) * dump.width + static_cast<usize>(x)) * 4u;
        if (sourceIsBgra) {
          bgrPixels.push_back(rgba[i + 0u]);
          bgrPixels.push_back(rgba[i + 1u]);
          bgrPixels.push_back(rgba[i + 2u]);
        } else {
          bgrPixels.push_back(rgba[i + 2u]);
          bgrPixels.push_back(rgba[i + 1u]);
          bgrPixels.push_back(rgba[i + 0u]);
        }
      }
    }
    dump.readback->unmap();
    writeBmp24File(dump.path, dump.width, dump.height, bgrPixels);
  }

  void rebuildSwapchain() {
    // A zero-sized window (minimized, or mid-drag) produces an invalid
    // swapchain. Let draw() retry later when the window has real size.
    if (m_window && (m_window->getWidth() <= 0 || m_window->getHeight() <= 0)) {
      return;
    }
    m_swapchain->waitIdle();
    if (!m_swapchain->rebuild(m_resourceManager->getRenderPass())) {
      return;
    }
    resetOffscreenFramebuffers();
    m_resourceManager->clearFrameGraphAttachments();
    m_swapchainNeedsRebuild = false;
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
    // Offscreen frame-graph framebuffers depend on the Vulkan device and must
    // be released before the device/resource manager are torn down.
    m_offscreenFramebuffers.clear();
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
  LX_core::CompiledFrameGraph m_compiledFrameGraph{};
  std::vector<std::vector<std::unique_ptr<VulkanFrameBuffer>>>
      m_offscreenFramebuffers;
  u32 m_frameIndex = 0;
  usize m_initSceneCallCount = 0;
  bool m_swapchainNeedsRebuild = false;
  infra::Gui m_gui{};
  std::function<void()> m_drawUiCallback{};
  std::optional<PendingScreenDump> m_pendingScreenDump;
};

VulkanRenderer::VulkanRenderer(Token)
    : p_impl(std::make_unique<VulkanRendererImpl>()) {}

VulkanRenderer::~VulkanRenderer() = default;

void VulkanRenderer::initialize(WindowSharedPtr window, const char *appName) {
  p_impl->initialize(window, appName);
}

void VulkanRenderer::shutdown() { p_impl->shutdown(); }

void VulkanRenderer::initScene(SceneSharedPtr scene) {
  p_impl->initScene(scene);
}

void VulkanRenderer::uploadData() { p_impl->uploadData(); }

void VulkanRenderer::draw() { p_impl->draw(); }

void VulkanRenderer::setDrawUiCallback(std::function<void()> cb) {
  p_impl->setDrawUiCallback(std::move(cb));
}

usize VulkanRenderer::cachedResourceCount() const {
  return p_impl->cachedResourceCount();
}

usize VulkanRenderer::frameGraphItemCount() const {
  return p_impl->frameGraphItemCount();
}

usize VulkanRenderer::compiledFrameGraphPassCount() const {
  return p_impl->compiledFrameGraphPassCount();
}

usize VulkanRenderer::frameGraphAttachmentCount() const {
  return p_impl->frameGraphAttachmentCount();
}

usize VulkanRenderer::initSceneCallCount() const {
  return p_impl->initSceneCallCount();
}

VulkanRenderer::FrameGraphAttachmentDumpResult
VulkanRenderer::dumpFrameGraphAttachment(
    std::string_view attachmentName,
    const std::optional<std::filesystem::path> &path,
    const std::optional<std::filesystem::path> &screenPath) {
  return p_impl->dumpFrameGraphAttachment(attachmentName, path, screenPath);
}

VulkanRenderer::FrameGraphAttachmentDumpResult
VulkanRenderer::dumpDebugRenderTarget(
    std::string_view passName, const std::optional<std::string> &cameraPath,
    const std::optional<std::filesystem::path> &path) {
  return p_impl->dumpDebugRenderTarget(passName, cameraPath, path);
}

} // namespace LX_core::backend
