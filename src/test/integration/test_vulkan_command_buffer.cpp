#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/descriptors/descriptor_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/texture.hpp"
#include "backend/vulkan/details/render_objects/framebuffer.hpp"
#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/frame_graph/render_input.hpp"
#include "core/frame_graph/render_upload_plan.hpp"
#include "core/frame_graph/render_work_compiler.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"

#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"
#include "scene_test_helpers.hpp"

#include <vulkan/vulkan.h>

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

bool isKnownEnvironmentSetupFailure(const std::string_view message) {
  return message.find("No available video device") != std::string_view::npos ||
         message.find("Failed to find GPUs with Vulkan support") !=
             std::string_view::npos ||
         message.find("Failed to find a suitable GPU") !=
             std::string_view::npos ||
         message.find("Failed to create Vulkan surface handle") !=
             std::string_view::npos;
}

bool recordRejectedRenderInputNoop(
    LX_core::backend::VulkanCommandBufferManager &cmdBufferMgr,
    const LX_core::RenderDrawInput &drawInput) {
  auto cmd = cmdBufferMgr.allocateBuffer();
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (vkBeginCommandBuffer(cmd->getHandle(), &beginInfo) != VK_SUCCESS) {
    std::cerr << "Failed to begin rejected render input command buffer\n";
    return false;
  }

  LX_core::RenderInputDesc rejectedDesc;
  rejectedDesc.status = LX_core::RenderInputStatus::Rejected;
  rejectedDesc.inputIndex = drawInput.inputIndex;
  rejectedDesc.pass = drawInput.pass;
  rejectedDesc.debugId = drawInput.debugId;
  cmd->executeRenderInput(drawInput, rejectedDesc);

  if (vkEndCommandBuffer(cmd->getHandle()) != VK_SUCCESS) {
    std::cerr << "Failed to end rejected render input command buffer\n";
    return false;
  }
  return true;
}

bool recordFullscreenTriangleWithoutGeometry(
    LX_core::backend::VulkanCommandBufferManager &cmdBufferMgr,
    LX_core::backend::VulkanResourceManager &resourceManager,
    LX_core::backend::VulkanRenderPass &renderPass,
    LX_core::backend::VulkanFrameBuffer &framebuffer, VkExtent2D extent,
    LX_core::backend::VulkanPipelineRef pipeline,
    const LX_core::RenderInput &input, const LX_core::RenderInputDesc &desc) {
  auto cmd = cmdBufferMgr.allocateBuffer();
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (vkBeginCommandBuffer(cmd->getHandle(), &beginInfo) != VK_SUCCESS) {
    std::cerr << "Failed to begin fullscreen render input command buffer\n";
    return false;
  }
  cmd->beginRenderPass(renderPass.getHandle(), framebuffer.getHandle(), extent,
                       renderPass.getClearValues());
  cmd->setViewport(extent.width, extent.height);
  cmd->setScissor(extent.width, extent.height);
  cmd->bindPipeline(pipeline);
  cmd->bindResources(resourceManager, pipeline, input, desc);
  cmd->executeRenderInput(input, desc);

  cmd->endRenderPass();
  if (vkEndCommandBuffer(cmd->getHandle()) != VK_SUCCESS) {
    std::cerr << "Failed to end fullscreen render input command buffer\n";
    return false;
  }
  return true;
}

struct PreparedFullscreenWork final {
  std::vector<std::unique_ptr<LX_core::RenderInput>> inputs;
  std::vector<LX_core::RenderInputDesc> descs;
};

PreparedFullscreenWork buildCompilerPreparedFullscreenWork() {
  constexpr const char *kShaderName = "ibl_brdf_lut";
  std::vector<LX_core::ShaderStageCode> stages{
      LX_test::loadTestShaderStage(kShaderName, "vert.spv",
                                   LX_core::ShaderStage::Vertex),
      LX_test::loadTestShaderStage(kShaderName, "frag.spv",
                                   LX_core::ShaderStage::Fragment),
  };
  auto shader = std::make_shared<LX_infra::CompiledShader>(
      stages, LX_infra::ShaderReflector::reflect(stages),
      LX_infra::ShaderReflector::reflectVertexInputs(stages), kShaderName);

  LX_core::FramePass pass;
  pass.name = LX_core::Pass_PostProcess;
  pass.stage = LX_core::RenderPassStage::Raster;
  pass.dispatch = LX_core::RenderPassDispatch::Fullscreen;
  pass.input.kind = LX_core::RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = LX_core::ResourceUri(kShaderName);

  LX_core::RenderWorkBuildContext::PassPreparationFacts facts;
  facts.pass = pass.name;
  facts.pipelineVariantKey = LX_core::StringID("vulkan_test_fullscreen");
  facts.shaderProgram.shaderName = kShaderName;
  facts.shaderProgram.shader = shader;
  facts.shaderInfo = shader;
  facts.renderState.cullMode = LX_core::CullMode::None;
  facts.renderState.depthTestEnable = false;
  facts.renderState.depthWriteEnable = false;

  LX_core::RenderWorkBuildContext::RealtimeOptions options;
  options.passPreparationFacts.push_back(std::move(facts));

  LX_core::Scene scene("vulkan_fullscreen_compiler_scene");
  const LX_core::RenderWorkBuildContext context =
      LX_core::RenderWorkBuildContext::realtime(scene, std::move(options));
  LX_core::RenderWorkCompiler compiler;
  PreparedFullscreenWork work;
  compiler.buildInputs(pass, context, work.inputs);
  work.descs = compiler.prepare(pass, context, work.inputs);
  if (work.inputs.size() != 1u || work.descs.size() != 1u ||
      !work.descs.front().accepted()) {
    throw std::runtime_error(
        "compiler did not produce accepted fullscreen Vulkan test desc");
  }
  return work;
}

bool recordComputeDispatchInput(
    LX_core::backend::VulkanCommandBufferManager &cmdBufferMgr) {
  auto cmd = cmdBufferMgr.allocateBuffer();
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (vkBeginCommandBuffer(cmd->getHandle(), &beginInfo) != VK_SUCCESS) {
    std::cerr << "Failed to begin compute render input command buffer\n";
    return false;
  }

  LX_core::RenderComputeInput computeInput;
  computeInput.pass = LX_core::StringID("vulkan_test_compute_pass");
  computeInput.debugId = LX_core::StringID("vulkan_test_compute_input");
  computeInput.inputIndex = 0;
  computeInput.groupCountX = 2;
  computeInput.groupCountY = 3;
  computeInput.groupCountZ = 1;

  LX_core::RenderInputDesc computeDesc;
  computeDesc.status = LX_core::RenderInputStatus::Accepted;
  computeDesc.inputIndex = computeInput.inputIndex;
  computeDesc.pass = computeInput.pass;
  computeDesc.debugId = computeInput.debugId;
  cmd->executeRenderInput(computeInput, computeDesc);

  if (vkEndCommandBuffer(cmd->getHandle()) != VK_SUCCESS) {
    std::cerr << "Failed to end compute render input command buffer\n";
    return false;
  }
  return true;
}

} // namespace

int main() {
  expSetEnvVK();
  bool commandRecordingStarted = false;
  try {
    auto success = initializeRuntimeAssetRoot();
    if (!success) {
      std::cerr << "Failed to find shader files\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window =
        std::make_shared<LX_infra::Window>("Test Vulkan CommandBuffer", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanCommandBuffer");
    const u32 maxFrameInFlight = 2;

    // Render pass / pipeline formats.
    VkFormat depthFormat = device->getDepthFormat();
    VkImageAspectFlags depthAspectMask = device->getDepthAspectMask();
    VkSurfaceFormatKHR surfaceFormat = device->getSurfaceFormat();

    // Create command buffer manager first (needed for resource manager)
    auto cmdBufferMgr = LX_core::backend::VulkanCommandBufferManager::create(
        *device, maxFrameInFlight, device->getGraphicsQueueFamilyIndex());
    auto resourceManager =
        LX_core::backend::VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(surfaceFormat,
                                                     depthFormat);

    auto &renderPass = resourceManager->getRenderPass();

    // Create minimal framebuffer attachments.
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

    // Build a minimal non-material direct helper item.
    auto vertexBufferPtr = LX_core::VertexBuffer<V>::create({
        V({-5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, -5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
    });
    auto indexBufferPtr = LX_core::IndexBuffer::create(
        {0u, 1u, 2u, 0u, 1u, 2u, 0u, 2u, 1u});
    auto renderInput = LX_test::makeMinimalRenderDrawInputForVulkanTests(
        *vertexBufferPtr, *indexBufferPtr);

    // Sync all CPU-side resources to GPU.
    auto pipelineDesc =
        LX_test::makeMinimalDirectRasterHelperPipelineBuildDescForVulkanTests(
            *vertexBufferPtr, *indexBufferPtr);
    auto renderDesc =
        LX_test::makeAcceptedRenderInputDescForVulkanTests(pipelineDesc,
                                                           renderInput);
    std::vector<std::unique_ptr<LX_core::RenderInput>> uploadInputs;
    uploadInputs.push_back(std::make_unique<LX_core::RenderDrawInput>(
        renderInput));
    std::vector<LX_core::RenderInputDesc> uploadDescs{renderDesc};
    const LX_core::RenderUploadPlan uploadPlan =
        LX_core::buildRenderUploadPlan(uploadInputs, uploadDescs);
    for (const auto &resource : uploadPlan.resources) {
      resourceManager->syncResource(*cmdBufferMgr, resource);
    }
    resourceManager->collectGarbage();

    auto pipeline = resourceManager->getOrCreatePipeline(renderDesc);
    PreparedFullscreenWork fullscreenWork =
        buildCompilerPreparedFullscreenWork();
    const LX_core::RenderInput &fullscreenInput =
        *fullscreenWork.inputs.front();
    const LX_core::RenderInputDesc &fullscreenDesc =
        fullscreenWork.descs.front();
    auto fullscreenPipeline =
        resourceManager->getOrCreatePipeline(fullscreenDesc);
    const VkPipeline pipelineHandle =
        std::visit([](auto ref) { return ref.get().getHandle(); }, pipeline);
    if (pipelineHandle == VK_NULL_HANDLE) {
      std::cerr << "Pipeline not created correctly\n";
      return 1;
    }

    cmdBufferMgr->beginFrame(0);
    commandRecordingStarted = true;
    if (!recordRejectedRenderInputNoop(*cmdBufferMgr, renderInput)) {
      return 1;
    }
    if (!recordFullscreenTriangleWithoutGeometry(
            *cmdBufferMgr, *resourceManager, renderPass, *framebuffer, extent,
            fullscreenPipeline, fullscreenInput, fullscreenDesc)) {
      return 1;
    }
    if (!recordComputeDispatchInput(*cmdBufferMgr)) {
      return 1;
    }

    auto cmd = cmdBufferMgr->allocateBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;
    vkBeginCommandBuffer(cmd->getHandle(), &beginInfo);

    cmd->beginRenderPass(renderPass.getHandle(), framebuffer->getHandle(),
                         extent, renderPass.getClearValues());
    cmd->setViewport(extent.width, extent.height);
    cmd->setScissor(extent.width, extent.height);
    cmd->bindPipeline(pipeline);
    cmd->bindResources(*resourceManager, pipeline, renderInput, renderDesc);
    cmd->executeRenderInput(renderInput, renderDesc);
    cmd->endRenderPass();

    if (vkEndCommandBuffer(cmd->getHandle()) != VK_SUCCESS) {
      std::cerr << "Failed to end batch command buffer\n";
      return 1;
    }

    VkCommandBuffer submittedCommand = cmd->getHandle();
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &submittedCommand;
    if (vkQueueSubmit(device->getGraphicsQueue(), 1, &submitInfo,
                      VK_NULL_HANDLE) != VK_SUCCESS) {
      std::cerr << "Failed to submit batch command buffer\n";
      return 1;
    }
    vkQueueWaitIdle(device->getGraphicsQueue());

    auto &descriptorMgr = device->getDescriptorManager();
    const auto descriptorFootprint = [&descriptorMgr](const u32 frameCount) {
      usize total = 0;
      for (u32 frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        total += descriptorMgr.getFreeSetCount(frameIndex);
        total += descriptorMgr.getPendingReturnCount(frameIndex);
      }
      return total;
    };

    const usize initialDescriptorFootprint =
        descriptorFootprint(maxFrameInFlight);
    for (u32 frame = 0; frame < 600; ++frame) {
      const u32 frameIndex = frame % maxFrameInFlight;
      cmdBufferMgr->beginFrame(frameIndex);
      descriptorMgr.beginFrame(frameIndex);
      auto loopCmd = cmdBufferMgr->allocateBuffer();

      VkCommandBufferBeginInfo loopBeginInfo{};
      loopBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      vkBeginCommandBuffer(loopCmd->getHandle(), &loopBeginInfo);
      loopCmd->beginRenderPass(renderPass.getHandle(), framebuffer->getHandle(),
                               extent, renderPass.getClearValues());
      loopCmd->setViewport(extent.width, extent.height);
      loopCmd->setScissor(extent.width, extent.height);
      loopCmd->bindPipeline(pipeline);
      loopCmd->bindResources(*resourceManager, pipeline, renderInput,
                             renderDesc);
      loopCmd->executeRenderInput(renderInput, renderDesc);
      loopCmd->endRenderPass();
      vkEndCommandBuffer(loopCmd->getHandle());
    }

    const usize finalDescriptorFootprint =
        descriptorFootprint(maxFrameInFlight);
    if (finalDescriptorFootprint > initialDescriptorFootprint + 16) {
      std::cerr << "Descriptor footprint grew unexpectedly: initial="
                << initialDescriptorFootprint
                << " final=" << finalDescriptorFootprint << "\n";
      return 1;
    }

    framebuffer.reset();

    return 0;
  } catch (const std::exception &e) {
    if (!commandRecordingStarted && isKnownEnvironmentSetupFailure(e.what())) {
      std::cerr << "SKIP VulkanCommandBuffer test: " << e.what() << "\n";
      return 0;
    }
    std::cerr << "FAIL VulkanCommandBuffer test: " << e.what() << "\n";
    return 1;
  }
}
