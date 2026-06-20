#include "vulkan_frame_graph_executor.hpp"

#include "details/commands/command_buffer.hpp"
#include "details/commands/command_buffer_manager.hpp"
#include "details/device.hpp"
#include "details/device_resources/buffer.hpp"
#include "details/device_resources/texture.hpp"
#include "details/resource_manager.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace LX_core::backend {
namespace {

void addDiagnostic(FrameGraphExecutionResult &result, std::string message) {
  result.diagnostics.push_back(std::move(message));
}

std::string passNameText(StringID passName) {
  return GlobalStringTable::get().toDebugString(passName);
}

const PreparedFramePassWork *
findPreparedWork(std::span<const PreparedFramePassWork> preparedPasses,
                 StringID passName) {
  const auto it =
      std::find_if(preparedPasses.begin(), preparedPasses.end(),
                   [passName](const PreparedFramePassWork &work) {
                     return work.passName == passName;
                   });
  return it == preparedPasses.end() ? nullptr : &*it;
}

bool targetHasFormat(const RenderTargetDesc &target) {
  return !target.getColorFormats().empty() || target.depthFormat.has_value();
}

bool descNeedsRenderTargetContract(const RenderInputDesc &desc) {
  return desc.pipelineBuildDesc.type == PipelineBuildType::Graphics;
}

std::optional<std::reference_wrapper<const FrameGraphWrite>>
findWriteForKind(const CompiledFrameGraphPass &pass,
                 FrameGraphAttachmentKind kind) {
  std::optional<std::reference_wrapper<const FrameGraphWrite>> found;
  for (const FrameGraphWrite &write : pass.writes) {
    if (write.resource.kind != kind) {
      continue;
    }
    if (found.has_value()) {
      throw std::runtime_error(
          "frame graph pass declares duplicate writes for one attachment kind");
    }
    found = std::cref(write);
  }
  return found;
}

std::vector<std::reference_wrapper<const FrameGraphWrite>>
findWritesForKind(const CompiledFrameGraphPass &pass,
                  FrameGraphAttachmentKind kind) {
  std::vector<std::reference_wrapper<const FrameGraphWrite>> found;
  for (const FrameGraphWrite &write : pass.writes) {
    if (write.resource.kind == kind) {
      found.emplace_back(write);
    }
  }
  return found;
}

VkFormat toVkFormat(ImageFormat format) {
  switch (format) {
  case ImageFormat::RGBA8:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case ImageFormat::RGBA8Srgb:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case ImageFormat::RG16Float:
    return VK_FORMAT_R16G16_SFLOAT;
  case ImageFormat::RGBA16Float:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case ImageFormat::BGRA8:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case ImageFormat::BGRA8Srgb:
    return VK_FORMAT_B8G8R8A8_SRGB;
  case ImageFormat::R8:
    return VK_FORMAT_R8_UNORM;
  case ImageFormat::D32Float:
    return VK_FORMAT_D32_SFLOAT;
  case ImageFormat::D24UnormS8:
    return VK_FORMAT_D24_UNORM_S8_UINT;
  case ImageFormat::D32FloatS8:
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
  }
  throw std::runtime_error("unsupported image format");
}

VkImageAspectFlags attachmentAspect(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Depth
             ? VK_IMAGE_ASPECT_DEPTH_BIT
             : VK_IMAGE_ASPECT_COLOR_BIT;
}

VkImageLayout attachmentWriteLayout(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Depth
             ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
             : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

VkPipelineStageFlags attachmentWriteStage(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Depth
             ? (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT)
             : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
}

VkAccessFlags attachmentWriteAccess(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Depth
             ? (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
             : (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
}

usize readbackByteSize(const RenderInputDesc::Readback &readback) {
  const usize pixelCount =
      static_cast<usize>(readback.extent.x) * readback.extent.y;
  if (readback.format == "RGBA32Float") {
    return pixelCount * 16u;
  }
  if (readback.format == "RGBA16Float") {
    return pixelCount * 8u;
  }
  if (readback.format == "RGBA8" || readback.format == "BGRA8") {
    return pixelCount * 4u;
  }
  return static_cast<usize>(readback.extent.x) * readback.extent.y *
         readback.extent.z;
}

bool inputMatchesPipelineType(const RenderInput &input,
                              const RenderInputDesc &desc) {
  switch (desc.pipelineBuildDesc.type) {
  case PipelineBuildType::Graphics:
    return input.kind() == RenderInputKind::Draw;
  case PipelineBuildType::Compute:
    return input.kind() == RenderInputKind::Compute;
  case PipelineBuildType::RayTracing:
    return false;
  }
  return false;
}

void validateGraphPointers(const FrameGraphExecutionRequest &request,
                           FrameGraphExecutionResult &result) {
  if (request.graph == nullptr) {
    addDiagnostic(result, "frame graph is required");
  }
  if (request.compiled == nullptr) {
    addDiagnostic(result, "compiled graph is required");
  }
}

void validateCompiledPasses(const FrameGraphExecutionRequest &request,
                            FrameGraphExecutionResult &result) {
  if (request.graph == nullptr || request.compiled == nullptr) {
    return;
  }
  if (!request.compiled->isValid()) {
    addDiagnostic(result, "compiled graph is invalid: " +
                              request.compiled->errorText());
    return;
  }

  const std::vector<FramePass> &graphPasses = request.graph->getPasses();
  for (const CompiledFrameGraphPass &compiledPass :
       request.compiled->getPasses()) {
    const std::string passName = passNameText(compiledPass.name);
    if (compiledPass.sourcePassIndex >= graphPasses.size()) {
      addDiagnostic(result, "source pass is required for " + passName);
      continue;
    }
    const FramePass &sourcePass = graphPasses[compiledPass.sourcePassIndex];
    if (sourcePass.shaderUri.empty()) {
      addDiagnostic(result, "shader is required for " + passName);
    }
    if (sourcePass.stage != RenderPassStage::Compute &&
        !targetHasFormat(compiledPass.target)) {
      addDiagnostic(result, "target format is required for " + passName);
    }
  }
}

void validatePreparedPasses(const FrameGraphExecutionRequest &request,
                            FrameGraphExecutionResult &result) {
  if (request.compiled == nullptr || !request.compiled->isValid()) {
    return;
  }

  for (const CompiledFrameGraphPass &compiledPass :
       request.compiled->getPasses()) {
    const std::string passName = passNameText(compiledPass.name);
    const PreparedFramePassWork *work =
        findPreparedWork(request.preparedPasses, compiledPass.name);
    if (work == nullptr) {
      addDiagnostic(result, "prepared pass work missing for " + passName);
      continue;
    }

    if (work->descs.size() > work->inputs.size()) {
      addDiagnostic(result, "typed payload is required for " + passName);
    }
    for (usize descIndex = 0; descIndex < work->descs.size(); ++descIndex) {
      const RenderInputDesc &desc = work->descs[descIndex];
      if (desc.pass != compiledPass.name) {
        addDiagnostic(result, "prepared pass contract mismatch for " +
                                  passName);
      }
      if (!desc.accepted()) {
        addDiagnostic(result, "input desc rejected for " + passName);
        continue;
      }
      if (desc.shaderUri.id == 0) {
        addDiagnostic(result, "shader is required for " + passName);
      }
      if (descNeedsRenderTargetContract(desc) &&
          !targetHasFormat(desc.pipelineBuildDesc.target)) {
        addDiagnostic(result, "target format is required for " + passName);
      }
      if (descNeedsRenderTargetContract(desc) &&
          desc.pipelineBuildDesc.target != compiledPass.target) {
        addDiagnostic(result, "prepared pass target mismatch for " + passName);
      }
      if (desc.inputIndex >= work->inputs.size() ||
          !work->inputs[desc.inputIndex]) {
        addDiagnostic(result, "typed payload is required for " + passName);
        continue;
      }

      const RenderInput &input = *work->inputs[desc.inputIndex];
      if (input.pass != compiledPass.name ||
          !inputMatchesPipelineType(input, desc)) {
        addDiagnostic(result, "typed payload is required for " + passName);
      }
    }
  }
}

void validateExecutionTarget(const VulkanFrameGraphExecutionTarget &target,
                             FrameGraphExecutionResult &result) {
  if (target.isPartial()) {
    addDiagnostic(result, "complete Vulkan execution target is required");
  }
}

void validateReadbackContracts(const FrameGraphExecutionRequest &request,
                               const VulkanFrameGraphExecutionTarget &target,
                               FrameGraphExecutionResult &result) {
  if (target.mode != VulkanFrameGraphExecutionMode::ImmediateSubmitReadback) {
    return;
  }
  for (const PreparedFramePassWork &work : request.preparedPasses) {
    for (const RenderInputDesc &desc : work.descs) {
      if (!desc.accepted()) {
        continue;
      }
      for (const RenderInputDesc::Readback &readback : desc.readbacks) {
        if (readback.name.empty()) {
          addDiagnostic(result, "readback name is required");
        }
        if (readback.target.id == 0) {
          addDiagnostic(result, "readback target is required");
        }
        if (readback.binding.id != 0 && !readback.resource.isValid()) {
          addDiagnostic(result,
                        "readback descriptor resource is required");
        }
        if (readback.extent.x == 0u || readback.extent.y == 0u ||
            readback.extent.z == 0u) {
          addDiagnostic(result, "readback extent is required");
        }
      }
    }
  }
}

const FramePass &sourcePassFor(const FrameGraphExecutionRequest &request,
                               const CompiledFrameGraphPass &compiledPass) {
  if (request.graph == nullptr ||
      compiledPass.sourcePassIndex >= request.graph->getPasses().size()) {
    throw std::runtime_error("compiled frame graph pass source is invalid");
  }
  return request.graph->getPasses()[compiledPass.sourcePassIndex];
}

VkExtent2D executionReadbackExtent(const FrameGraphExecutionRequest &request) {
  for (const PreparedFramePassWork &work : request.preparedPasses) {
    for (const RenderInputDesc &desc : work.descs) {
      if (!desc.accepted()) {
        continue;
      }
      for (const RenderInputDesc::Readback &readback : desc.readbacks) {
        if (readback.extent.x != 0u && readback.extent.y != 0u) {
          return VkExtent2D{readback.extent.x, readback.extent.y};
        }
      }
    }
  }
  throw std::runtime_error(
      "ImmediateSubmitReadback execution requires a readback extent");
}

void transitionFrameGraphAttachment(VulkanCommandBuffer &commandBuffer,
                                    VulkanFrameGraphAttachment &attachment,
                                    VkImageLayout newLayout,
                                    VkPipelineStageFlags dstStage,
                                    VkAccessFlags dstAccess) {
  if (attachment.currentLayout == newLayout) {
    return;
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
  barrier.dstAccessMask = dstAccess;
  switch (attachment.currentLayout) {
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    commandBuffer.pipelineBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  dstStage, barrier);
    break;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    commandBuffer.pipelineBarrier(
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        dstStage, barrier);
    break;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    commandBuffer.pipelineBarrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  dstStage, barrier);
    break;
  default:
    barrier.srcAccessMask = 0;
    commandBuffer.pipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dstStage,
                                  barrier);
    break;
  }
  attachment.currentLayout = newLayout;
}

VkRenderingAttachmentInfo
makeColorAttachmentInfo(VkImageView view, const FrameGraphWrite &write) {
  VkRenderingAttachmentInfo info{};
  info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  info.imageView = view;
  info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  info.loadOp = write.writeMode.has_value() ? VK_ATTACHMENT_LOAD_OP_LOAD
                                            : VK_ATTACHMENT_LOAD_OP_CLEAR;
  info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  info.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  return info;
}

VkRenderingAttachmentInfo
makeDepthAttachmentInfo(VkImageView view, const FrameGraphWrite &write) {
  VkRenderingAttachmentInfo info{};
  info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  info.imageView = view;
  info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  info.loadOp = write.writeMode.has_value() ? VK_ATTACHMENT_LOAD_OP_LOAD
                                            : VK_ATTACHMENT_LOAD_OP_CLEAR;
  info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  info.clearValue.depthStencil = {1.0f, 0};
  return info;
}

u32 dynamicRenderingLayerCount(const FramePass &graphPass) {
  if (graphPass.attachments.empty()) {
    return 1u;
  }
  const u32 layers = graphPass.attachments.front().layers;
  for (const RenderPathAttachmentContract &attachment :
       graphPass.attachments) {
    if (attachment.layers != layers) {
      throw std::runtime_error(
          "dynamic rendering pass attachment layers mismatch: " +
          passNameText(graphPass.name));
    }
  }
  return layers;
}

const RenderPathAttachmentContract *
depthAttachmentContract(const FramePass &graphPass) {
  const auto it = std::find_if(
      graphPass.attachments.begin(), graphPass.attachments.end(),
      [](const RenderPathAttachmentContract &attachment) {
        return attachment.depth;
      });
  return it == graphPass.attachments.end() ? nullptr : &*it;
}

void prepareOffscreenAttachments(
    const CompiledFrameGraphPass &compiledPass,
    VulkanResourceManager &resourceManager,
    VulkanCommandBuffer &commandBuffer,
    VkExtent2D extent) {
  if (compiledPass.target.role == RenderTargetRole::Swapchain) {
    throw std::runtime_error(
        "ImmediateSubmitReadback does not support swapchain frame graph "
        "targets");
  }

  const std::vector<ImageFormat> colorFormats =
      compiledPass.target.getColorFormats();
  const auto colorWrites =
      findWritesForKind(compiledPass, FrameGraphAttachmentKind::Color);
  if (colorFormats.size() != colorWrites.size()) {
    throw std::runtime_error(
        "frame graph offscreen pass color write count does not match target: " +
        passNameText(compiledPass.name));
  }
  for (usize i = 0; i < colorFormats.size(); ++i) {
    const FrameGraphWrite &write = colorWrites[i].get();
    auto &attachment = resourceManager.createOrGetFrameGraphAttachment(
        write.resource.name, extent, toVkFormat(colorFormats[i]),
        attachmentAspect(write.resource.kind),
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    transitionFrameGraphAttachment(commandBuffer, attachment,
                                   attachmentWriteLayout(write.resource.kind),
                                   attachmentWriteStage(write.resource.kind),
                                   attachmentWriteAccess(write.resource.kind));
  }

  if (!compiledPass.target.depthFormat.has_value()) {
    return;
  }
  const auto depthWrite =
      findWriteForKind(compiledPass, FrameGraphAttachmentKind::Depth);
  if (!depthWrite.has_value()) {
    return;
  }
  const FrameGraphWrite &write = depthWrite->get();
  auto &attachment = resourceManager.createOrGetFrameGraphAttachment(
      write.resource.name, extent, toVkFormat(*compiledPass.target.depthFormat),
      attachmentAspect(write.resource.kind),
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  transitionFrameGraphAttachment(commandBuffer, attachment,
                                 attachmentWriteLayout(write.resource.kind),
                                 attachmentWriteStage(write.resource.kind),
                                 attachmentWriteAccess(write.resource.kind));
}

void syncDescriptorResources(VulkanResourceManager &resourceManager,
                             VulkanCommandBufferManager &commandManager,
                             const DescriptorResourceList &resources) {
  for (const DescriptorResourceRef &resource : resources) {
    if (resource.isTextureArray()) {
      for (const TextureSamplerRef &texture : resource.textures()) {
        if (texture.isValid()) {
          resourceManager.syncResource(commandManager,
                                       GpuResourceRef{texture.get()});
        }
      }
      continue;
    }
    if (resource.resource().isValid()) {
      resourceManager.syncResource(commandManager, resource.resource());
    }
  }
}

void syncResourceDependencies(VulkanResourceManager &resourceManager,
                              VulkanCommandBufferManager &commandManager,
                              const std::vector<GpuResourceRef> &resources) {
  for (const GpuResourceRef &resource : resources) {
    if (resource.isValid()) {
      resourceManager.syncResource(commandManager, resource);
    }
  }
}

void recordDynamicRasterPass(const FrameGraphExecutionRequest &request,
                             const CompiledFrameGraphPass &compiledPass,
                             const PreparedFramePassWork &work,
                             const VulkanFrameGraphExecutionTarget &target,
                             VkExtent2D extent) {
  if (target.resourceManager == nullptr || target.commandBuffer == nullptr) {
    return;
  }
  const FramePass &graphPass = sourcePassFor(request, compiledPass);
  if (graphPass.renderingMode.has_value() &&
      *graphPass.renderingMode != RenderPathNodeRenderingMode::Dynamic) {
    throw std::runtime_error(
        "ImmediateSubmitReadback supports only dynamic raster passes: " +
        passNameText(compiledPass.name));
  }

  prepareOffscreenAttachments(compiledPass, *target.resourceManager,
                              *target.commandBuffer, extent);

  std::vector<VkRenderingAttachmentInfo> colorAttachments;
  const auto colorWrites =
      findWritesForKind(compiledPass, FrameGraphAttachmentKind::Color);
  colorAttachments.reserve(colorWrites.size());
  for (const auto &write : colorWrites) {
    auto attachment =
        target.resourceManager->getFrameGraphAttachment(write.get().resource.name);
    if (!attachment.has_value() || !attachment->get().texture) {
      throw std::runtime_error(
          "dynamic offscreen pass missing color attachment: " +
          passNameText(write.get().resource.name));
    }
    colorAttachments.push_back(makeColorAttachmentInfo(
        attachment->get().texture->getImageView(), write.get()));
  }

  std::optional<VkRenderingAttachmentInfo> depthAttachment;
  const RenderPathAttachmentContract *depthContract =
      depthAttachmentContract(graphPass);
  const auto depthWrite =
      findWriteForKind(compiledPass, FrameGraphAttachmentKind::Depth);
  if (depthContract != nullptr && depthWrite.has_value()) {
    auto attachment =
        target.resourceManager->getFrameGraphAttachment(depthWrite->get().resource.name);
    if (!attachment.has_value() || !attachment->get().texture) {
      throw std::runtime_error(
          "dynamic offscreen pass missing depth attachment: " +
          passNameText(depthWrite->get().resource.name));
    }
    depthAttachment = makeDepthAttachmentInfo(
        attachment->get().texture->getImageView(), depthWrite->get());
  }

  target.commandBuffer->beginRendering(
      extent, colorAttachments,
      depthAttachment.has_value() ? &*depthAttachment : nullptr,
      dynamicRenderingLayerCount(graphPass));
  target.commandBuffer->setViewport(extent.width, extent.height);
  target.commandBuffer->setScissor(extent.width, extent.height);
  (void)detail::recordPreparedFramePassWork(target, compiledPass, work);
  target.commandBuffer->endRendering();

  for (const FrameGraphWrite &write : compiledPass.writes) {
    auto attachment =
        target.resourceManager->getFrameGraphAttachment(write.resource.name);
    if (!attachment.has_value()) {
      continue;
    }
    transitionFrameGraphAttachment(
        *target.commandBuffer, attachment->get(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);
  }
}

bool hasAnyReadback(const FrameGraphExecutionRequest &request) {
  for (const PreparedFramePassWork &work : request.preparedPasses) {
    for (const RenderInputDesc &desc : work.descs) {
      if (desc.accepted() && !desc.readbacks.empty()) {
        return true;
      }
    }
  }
  return false;
}

void recordHostReadbackBarrier(VulkanCommandBuffer &commandBuffer) {
  VkMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask =
      VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(commandBuffer.getHandle(),
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                       0, nullptr);
}

struct PendingAttachmentReadback final {
  std::string name;
  std::string target;
  std::string format;
  RenderPathOutputKind kind = RenderPathOutputKind::Buffer;
  Vec3u extent{1u, 1u, 1u};
  std::string mediaType;
  VulkanBufferUniquePtr buffer;
};

void recordAttachmentReadbacks(
    const FrameGraphExecutionRequest &request,
    VulkanResourceManager &resourceManager,
    VulkanDevice &device,
    VulkanCommandBuffer &commandBuffer,
    std::vector<PendingAttachmentReadback> &pending) {
  for (const PreparedFramePassWork &work : request.preparedPasses) {
    for (const RenderInputDesc &desc : work.descs) {
      if (!desc.accepted()) {
        continue;
      }
      for (const RenderInputDesc::Readback &readback : desc.readbacks) {
        if (readback.resource.isValid()) {
          continue;
        }
        auto attachment =
            resourceManager.getFrameGraphAttachment(readback.target);
        if (!attachment.has_value() || !attachment->get().texture) {
          throw std::runtime_error("readback attachment is missing for " +
                                   passNameText(readback.target));
        }
        transitionFrameGraphAttachment(
            commandBuffer, attachment->get(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        auto buffer = VulkanBuffer::create(
            device, readbackByteSize(readback),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        commandBuffer.copyImageToBuffer(attachment->get().texture->getHandle(),
                                        buffer->getHandle(), readback.extent.x,
                                        readback.extent.y);
        pending.push_back(PendingAttachmentReadback{
            .name = readback.name,
            .target = passNameText(readback.target),
            .format = readback.format,
            .kind = readback.kind,
            .extent = readback.extent,
            .mediaType = readback.mediaType,
            .buffer = std::move(buffer),
        });
      }
    }
  }
}

void collectAttachmentReadbackPayloads(
    std::vector<PendingAttachmentReadback> &pending,
    FrameGraphExecutionResult &result) {
  for (PendingAttachmentReadback &readback : pending) {
    FrameGraphExecutionPayload payload;
    payload.name = std::move(readback.name);
    payload.target = std::move(readback.target);
    payload.format = std::move(readback.format);
    payload.kind = readback.kind;
    payload.extent = readback.extent;
    payload.mediaType = std::move(readback.mediaType);
    payload.bytes.resize(static_cast<usize>(readback.buffer->getSize()));
    void *mapped = readback.buffer->map();
    if (!payload.bytes.empty()) {
      std::memcpy(payload.bytes.data(), mapped, payload.bytes.size());
    }
    readback.buffer->unmap();
    result.outputs.push_back(std::move(payload));
  }
}

void collectReadbackPayloads(const FrameGraphExecutionRequest &request,
                             VulkanResourceManager &resourceManager,
                             FrameGraphExecutionResult &result) {
  for (const PreparedFramePassWork &work : request.preparedPasses) {
    for (const RenderInputDesc &desc : work.descs) {
      if (!desc.accepted()) {
        continue;
      }
      for (const RenderInputDesc::Readback &readback : desc.readbacks) {
        if (!readback.resource.isValid()) {
          continue;
        }
        const auto buffer = resourceManager.getBuffer(
            readback.resource.getBackendCacheIdentity());
        if (!buffer.has_value()) {
          addDiagnostic(result, "readback backend buffer is missing for " +
                                    readback.name);
          continue;
        }
        FrameGraphExecutionPayload payload;
        payload.name = readback.name;
        payload.target = passNameText(readback.target);
        payload.format = readback.format;
        payload.kind = readback.kind;
        payload.extent = readback.extent;
        payload.mediaType = readback.mediaType;
        payload.bytes.resize(
            static_cast<usize>(readback.resource.get().getByteSize()));
        void *mapped = buffer->get().map();
        if (!payload.bytes.empty()) {
          std::memcpy(payload.bytes.data(), mapped, payload.bytes.size());
        }
        buffer->get().unmap();
        result.outputs.push_back(std::move(payload));
      }
    }
  }
}

} // namespace

namespace detail {

void requireResolvedPassContract(const CompiledFrameGraphPass &compiledPass,
                                 const PreparedFramePassWork &work) {
  if (work.passName != compiledPass.name) {
    throw std::runtime_error(
        "prepared pass work does not match compiled pass");
  }
}

void requireResolvedDescContract(const CompiledFrameGraphPass &compiledPass,
                                 const RenderInputDesc &desc) {
  if (desc.pass != compiledPass.name) {
    throw std::runtime_error(
        "prepared input desc does not match compiled pass");
  }
  if (desc.accepted() && descNeedsRenderTargetContract(desc) &&
      desc.pipelineBuildDesc.target != compiledPass.target) {
    throw std::runtime_error(
        "prepared input desc target does not match compiled pass");
  }
}

void requireResolvedInputContract(const CompiledFrameGraphPass &compiledPass,
                                  const RenderInputDesc &desc,
                                  const RenderInput &input) {
  if (input.pass != compiledPass.name) {
    throw std::runtime_error(
        "prepared input payload does not match compiled pass");
  }
  if (!inputMatchesPipelineType(input, desc)) {
    throw std::runtime_error(
        "prepared input payload type does not match pipeline contract");
  }
}

VulkanPreparedFramePassRecordStats recordPreparedFramePassWork(
    const VulkanFrameGraphExecutionTarget &target,
    const CompiledFrameGraphPass &compiledPass,
    const PreparedFramePassWork &work,
    const VulkanPreparedFramePassRecordHooks &hooks) {
  VulkanPreparedFramePassRecordStats stats;
  requireResolvedPassContract(compiledPass, work);
  for (const RenderInputDesc &desc : work.descs) {
    requireResolvedDescContract(compiledPass, desc);
    if (hooks.observeDesc) {
      hooks.observeDesc(desc);
    }
    if (!desc.accepted()) {
      if (hooks.observeRejectedDesc) {
        hooks.observeRejectedDesc(desc);
      }
      continue;
    }
    if (!target.recordsCommands()) {
      continue;
    }

    if (desc.inputIndex >= work.inputs.size() || !work.inputs[desc.inputIndex]) {
      throw std::runtime_error("prepared input payload is missing");
    }
    const RenderInput &input = *work.inputs.at(desc.inputIndex);
    requireResolvedInputContract(compiledPass, desc, input);
    auto pipeline = target.resourceManager->getOrCreatePipeline(desc);
    ++stats.pipelineLookupCount;
    target.commandBuffer->bindPipeline(pipeline);
    target.commandBuffer->bindResources(*target.resourceManager, pipeline,
                                        input, desc);
    ++stats.boundInputCount;
    target.commandBuffer->executeRenderInput(input, desc);
    ++stats.executedInputCount;
  }
  return stats;
}

} // namespace detail

void executePreparedPasses(const FrameGraphExecutionRequest &request,
                           const VulkanFrameGraphExecutionTarget &target,
                           FrameGraphExecutionResult &result) {
  if (request.compiled == nullptr) {
    return;
  }
  try {
    for (const CompiledFrameGraphPass &compiledPass :
         request.compiled->getPasses()) {
      const PreparedFramePassWork *work =
          findPreparedWork(request.preparedPasses, compiledPass.name);
      if (work == nullptr) {
        continue;
      }
      (void)detail::recordPreparedFramePassWork(target, compiledPass, *work);
    }
  } catch (const std::exception &error) {
    addDiagnostic(result, error.what());
  }
}

void executeImmediateSubmitReadback(
    const FrameGraphExecutionRequest &request,
    const VulkanFrameGraphExecutionTarget &target,
    FrameGraphExecutionResult &result) {
  if (request.compiled == nullptr || target.device == nullptr ||
      target.commandManager == nullptr || target.resourceManager == nullptr) {
    return;
  }

  try {
    auto commandBuffer = target.commandManager->beginSingleTimeCommands();
    VulkanFrameGraphExecutionTarget recordTarget = target;
    recordTarget.commandBuffer = commandBuffer.get();
    const VkExtent2D executionExtent = executionReadbackExtent(request);
    std::vector<PendingAttachmentReadback> attachmentReadbacks;

    for (const CompiledFrameGraphPass &compiledPass :
         request.compiled->getPasses()) {
      const PreparedFramePassWork *work =
          findPreparedWork(request.preparedPasses, compiledPass.name);
      if (work == nullptr) {
        continue;
      }
      for (const RenderInputDesc &desc : work->descs) {
        if (desc.accepted()) {
          syncDescriptorResources(*target.resourceManager,
                                  *target.commandManager,
                                  desc.bindingPlan.descriptors);
          syncResourceDependencies(*target.resourceManager,
                                   *target.commandManager,
                                   desc.resourceDependencies);
        }
      }
      const FramePass &sourcePass = sourcePassFor(request, compiledPass);
      if (compiledPass.target.role == RenderTargetRole::Offscreen &&
          sourcePass.stage == RenderPassStage::Raster) {
        recordDynamicRasterPass(request, compiledPass, *work, recordTarget,
                                executionExtent);
      } else {
        (void)detail::recordPreparedFramePassWork(recordTarget, compiledPass,
                                                  *work);
      }
    }

    recordAttachmentReadbacks(request, *target.resourceManager, *target.device,
                              *commandBuffer, attachmentReadbacks);
    if (hasAnyReadback(request)) {
      recordHostReadbackBarrier(*commandBuffer);
    }

    target.commandManager->endSingleTimeCommands(
        std::move(commandBuffer), target.device->getGraphicsQueue());
    collectReadbackPayloads(request, *target.resourceManager, result);
    collectAttachmentReadbackPayloads(attachmentReadbacks, result);
  } catch (const std::exception &error) {
    addDiagnostic(result, error.what());
  }
}

FrameGraphExecutionResult
VulkanFrameGraphExecutor::execute(const FrameGraphExecutionRequest &request) {
  FrameGraphExecutionResult result;
  validateGraphPointers(request, result);
  validateExecutionTarget(m_executionTarget, result);
  validateCompiledPasses(request, result);
  validatePreparedPasses(request, result);
  validateReadbackContracts(request, m_executionTarget, result);
  if (result.diagnostics.empty()) {
    if (m_executionTarget.mode ==
        VulkanFrameGraphExecutionMode::ImmediateSubmitReadback) {
      executeImmediateSubmitReadback(request, m_executionTarget, result);
    } else {
      executePreparedPasses(request, m_executionTarget, result);
    }
  }
  result.ok = result.diagnostics.empty();
  return result;
}

} // namespace LX_core::backend
