#include "vulkan_renderer.hpp"
#include "vulkan_realtime_renderer.hpp"

namespace LX_core::backend {

VulkanRenderer::VulkanRenderer(Token)
    : p_realtime(std::make_unique<VulkanRealtimeRenderer>()) {}

VulkanRenderer::~VulkanRenderer() = default;

void VulkanRenderer::initialize(WindowSharedPtr window, const char *appName) {
  p_realtime->initialize(std::move(window), appName);
}

void VulkanRenderer::shutdown() { p_realtime->shutdown(); }

void VulkanRenderer::initScene(SceneSharedPtr scene) {
  p_realtime->initScene(std::move(scene));
}

void VulkanRenderer::uploadData() { p_realtime->uploadData(); }

void VulkanRenderer::draw() { p_realtime->draw(); }

void VulkanRenderer::setLiveRenderView(
    std::optional<gpu::LiveRenderView> view) {
  p_realtime->setLiveRenderView(std::move(view));
}

gpu::LiveRenderSubmissionStats
VulkanRenderer::liveRenderSubmissionStats() const {
  return p_realtime->liveRenderSubmissionStats();
}

void VulkanRenderer::setDrawUiCallback(std::function<void()> cb) {
  p_realtime->setDrawUiCallback(std::move(cb));
}

void VulkanRenderer::setPostProcessSettings(
    const PostProcessSettings &settings) {
  p_realtime->setPostProcessSettings(settings);
}

const VulkanRenderer::PostProcessSettings &
VulkanRenderer::postProcessSettings() const {
  return p_realtime->postProcessSettings();
}

usize VulkanRenderer::cachedResourceCount() const {
  return p_realtime->cachedResourceCount();
}

usize VulkanRenderer::frameGraphItemCount() const {
  return p_realtime->frameGraphItemCount();
}

usize VulkanRenderer::compiledFrameGraphPassCount() const {
  return p_realtime->compiledFrameGraphPassCount();
}

std::vector<std::string> VulkanRenderer::compiledFrameGraphPassNames() const {
  return p_realtime->compiledFrameGraphPassNames();
}

usize VulkanRenderer::frameGraphAttachmentCount() const {
  return p_realtime->frameGraphAttachmentCount();
}

usize VulkanRenderer::initSceneCallCount() const {
  return p_realtime->initSceneCallCount();
}

VulkanRenderer::FrameGraphAttachmentDumpResult
VulkanRenderer::dumpFrameGraphAttachment(
    std::string_view attachmentName,
    const std::optional<std::filesystem::path> &path,
    const std::optional<std::filesystem::path> &screenPath) {
  return p_realtime->dumpFrameGraphAttachment(attachmentName, path, screenPath);
}

VulkanRenderer::FrameGraphAttachmentDumpResult
VulkanRenderer::statsFrameGraphAttachment(std::string_view attachmentName) {
  return p_realtime->statsFrameGraphAttachment(attachmentName);
}

VulkanRenderer::FrameGraphAttachmentDumpResult
VulkanRenderer::dumpDebugRenderTarget(
    std::string_view passName, const std::optional<std::string> &cameraPath,
    const std::optional<std::filesystem::path> &path) {
  return p_realtime->dumpDebugRenderTarget(passName, cameraPath, path);
}

VulkanRealtimeProfileOutputResult VulkanRenderer::generateRealtimeProfileOutput(
    SceneSharedPtr scene, const LX_core::offline::OutputProfile &output,
    const std::filesystem::path &basePath) {
  return p_realtime->generateRealtimeProfileOutput(std::move(scene), output,
                                                   basePath);
}

VulkanDebugColorTransferExportResult VulkanRenderer::exportDebugColorTransfer(
    const VulkanDebugColorTransferExportRequest &request) {
  return p_realtime->exportDebugColorTransfer(request);
}

} // namespace LX_core::backend
