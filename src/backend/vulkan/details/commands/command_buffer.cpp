#include "command_buffer.hpp"
#include "core/asset/texture.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"
#include "../descriptors/descriptor_manager.hpp"
#include "../device.hpp"
#include "../device_resources/buffer.hpp"
#include "../device_resources/texture.hpp"
#include "../pipelines/compute_pipeline.hpp"
#include "../pipelines/graphics_pipeline.hpp"
#include "../resource_manager.hpp"
#include <array>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace LX_core::backend {

VkViewport makeVulkanViewport(u32 width, u32 height) {
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(width);
  viewport.height = static_cast<float>(height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  return viewport;
}

void VulkanCommandBuffer::begin() {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = 0;
  beginInfo.pInheritanceInfo = nullptr;
  if (vkBeginCommandBuffer(m_handle, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("Failed to begin command buffer");
  }
}

void VulkanCommandBuffer::end() { vkEndCommandBuffer(m_handle); }

void VulkanCommandBuffer::beginRenderPass(
    VkRenderPass renderPass, VkFramebuffer framebuffer, VkExtent2D extent,
    const std::vector<VkClearValue> &clearValues) {
  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = renderPass;
  renderPassInfo.framebuffer = framebuffer;
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = extent;
  renderPassInfo.clearValueCount = static_cast<u32>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(m_handle, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanCommandBuffer::beginRendering(
    VkExtent2D extent,
    const std::vector<VkRenderingAttachmentInfo> &colorAttachments,
    const VkRenderingAttachmentInfo *depthAttachment, u32 layerCount) {
  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea.offset = {0, 0};
  renderingInfo.renderArea.extent = extent;
  renderingInfo.layerCount = layerCount;
  renderingInfo.colorAttachmentCount =
      static_cast<u32>(colorAttachments.size());
  renderingInfo.pColorAttachments =
      colorAttachments.empty() ? nullptr : colorAttachments.data();
  renderingInfo.pDepthAttachment = depthAttachment;
  vkCmdBeginRendering(m_handle, &renderingInfo);
}

void VulkanCommandBuffer::setViewport(u32 width, u32 height) {
  const VkViewport viewport = makeVulkanViewport(width, height);
  vkCmdSetViewport(m_handle, 0, 1, &viewport);
}

void VulkanCommandBuffer::setScissor(u32 width, u32 height) {
  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = {width, height};
  vkCmdSetScissor(m_handle, 0, 1, &scissor);
}

namespace {
constexpr int kDebugBurstFrames = 3;

template <typename T>
bool shouldLogBurst(const T &next, T &state, int &remainingFrames) {
  if (!(next == state)) {
    state = next;
    remainingFrames = kDebugBurstFrames;
    return true;
  }
  if (remainingFrames > 0) {
    --remainingFrames;
    return true;
  }
  return false;
}

void logMissingDescriptorBindingOnce(const RenderWorkItem &item,
                                     const ShaderResourceBinding &binding) {
  if (!expRendererDebugEnabled()) {
    return;
  }

  static std::unordered_set<std::string> loggedKeys;
  std::ostringstream oss;
  oss << item.pipelineKey.id.id << "|" << item.pass.id << "|" << binding.set
      << "|" << binding.binding << "|" << binding.name;
  const std::string key = oss.str();
  if (!loggedKeys.insert(key).second) {
    return;
  }

  std::cerr << "[RendererDebug] bindResources: missing binding name="
            << binding.name << " set=" << binding.set
            << " binding=" << binding.binding << " pass="
            << LX_core::GlobalStringTable::get().getName(item.pass.id)
            << " pipelineKey=" << item.pipelineKey.id.id << std::endl;
}

[[noreturn]] void throwDescriptorBindingError(
    const RenderWorkItem &item, const ShaderResourceBinding &binding,
    const char *reason) {
  logMissingDescriptorBindingOnce(item, binding);
  std::ostringstream oss;
  oss << "bindResources failed for reflected descriptor binding name="
      << binding.name << " set=" << binding.set
      << " binding=" << binding.binding << " pass="
      << LX_core::GlobalStringTable::get().getName(item.pass.id)
      << " pipelineKey=" << item.pipelineKey.id.id << ": " << reason;
  throw std::runtime_error(oss.str());
}

std::string directRasterWorkItemContext(const RenderWorkItem &item) {
  std::ostringstream oss;
  oss << " pass="
      << LX_core::GlobalStringTable::get().toDebugString(item.pass)
      << " debugId="
      << LX_core::GlobalStringTable::get().toDebugString(item.debugId)
      << " purpose="
      << LX_core::directRasterPassPurposeName(item.directRaster.purpose);
  return oss.str();
}

void logDescriptorBufferBindingIfChanged(
    const RenderWorkItem &item, const ShaderResourceBinding &binding,
    const IGpuResource &cpuRes, const VkDescriptorBufferInfo &bufferInfo) {
  if (!expRendererDebugEnabled()) {
    return;
  }

  struct BindingLogState {
    ResourceCacheIdentity identity = 0;
    usize bufferToken = 0;
    VkDeviceSize range = 0;

    bool operator==(const BindingLogState &other) const = default;
  };
  struct BindingLogEntry {
    BindingLogState state{};
    int remainingFrames = 0;
  };
  static std::unordered_map<std::string, BindingLogEntry> logged;

  std::ostringstream keyBuilder;
  keyBuilder << item.pipelineKey.id.id << "|" << item.pass.id << "|"
             << binding.set << "|" << binding.binding << "|" << binding.name;
  const std::string key = keyBuilder.str();

  BindingLogState next{};
  next.identity = cpuRes.getBackendCacheIdentity();
  next.bufferToken = std::hash<VkBuffer>{}(bufferInfo.buffer);
  next.range = bufferInfo.range;

  auto &entry = logged[key];
  if (!shouldLogBurst(next, entry.state, entry.remainingFrames)) {
    return;
  }

  std::cerr << "[RendererDebug] bindResources: buffer name=" << binding.name
            << " set=" << binding.set << " binding=" << binding.binding
            << " pass="
            << LX_core::GlobalStringTable::get().getName(item.pass.id)
            << " pipelineKey=" << item.pipelineKey.id.id
            << " identity=" << next.identity
            << " bufferToken=" << next.bufferToken << " range=" << next.range
            << std::endl;
}

void logDescriptorImageBindingIfChanged(
    const RenderWorkItem &item, const ShaderResourceBinding &binding,
    const IGpuResource &cpuRes, const VkDescriptorImageInfo &imageInfo) {
  if (!expRendererDebugEnabled()) {
    return;
  }

  struct BindingLogState {
    ResourceCacheIdentity identity = 0;
    usize imageViewToken = 0;
    usize samplerToken = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

    bool operator==(const BindingLogState &other) const = default;
  };
  struct BindingLogEntry {
    BindingLogState state{};
    int remainingFrames = 0;
  };
  static std::unordered_map<std::string, BindingLogEntry> logged;

  std::ostringstream keyBuilder;
  keyBuilder << item.pipelineKey.id.id << "|" << item.pass.id << "|"
             << binding.set << "|" << binding.binding << "|" << binding.name;
  const std::string key = keyBuilder.str();

  BindingLogState next{};
  next.identity = cpuRes.getBackendCacheIdentity();
  next.imageViewToken = std::hash<VkImageView>{}(imageInfo.imageView);
  next.samplerToken = std::hash<VkSampler>{}(imageInfo.sampler);
  next.layout = imageInfo.imageLayout;

  auto &entry = logged[key];
  if (!shouldLogBurst(next, entry.state, entry.remainingFrames)) {
    return;
  }

  std::cerr << "[RendererDebug] bindResources: image name=" << binding.name
            << " set=" << binding.set << " binding=" << binding.binding
            << " pass="
            << LX_core::GlobalStringTable::get().getName(item.pass.id)
            << " pipelineKey=" << item.pipelineKey.id.id
            << " identity=" << next.identity
            << " imageViewToken=" << next.imageViewToken
            << " samplerToken=" << next.samplerToken
            << " layout=" << static_cast<int>(next.layout) << std::endl;
}
} // namespace

void VulkanCommandBuffer::bindPipeline(VulkanGraphicsPipeline &pipeline) {
  vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.getHandle());
}

void VulkanCommandBuffer::bindPipeline(VulkanComputePipeline &pipeline) {
  vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.getHandle());
}

void VulkanCommandBuffer::bindPipeline(VulkanPipelineRef pipeline) {
  std::visit([this](auto ref) { bindPipeline(ref.get()); }, pipeline);
}

void VulkanCommandBuffer::bindResourcesWithLayout(
    VulkanResourceManager &resourceManager,
    const std::vector<ShaderResourceBinding> &bindings,
    const VkPipelineLayout pipelineLayout, const VkPipelineBindPoint bindPoint,
    const RenderWorkItem &item) {
  auto &descriptorMgr = m_device.getDescriptorManager();

  // Build a name→resource map from the item's descriptorResources so backend
  // routing can match reflected binding names without any slot enum.
  std::unordered_map<LX_core::StringID, LX_core::DescriptorResourceRef,
                     LX_core::StringID::Hash>
      resourceByName;
  for (const auto &cpuRes : item.descriptorResources) {
    const auto name = cpuRes.getBindingName();
    if (name.id == 0) {
      continue;
    }
    resourceByName.emplace(name, cpuRes);
  }

  // Group reflection bindings by descriptor set index.
  std::unordered_map<u32, std::vector<LX_core::ShaderResourceBinding>>
      setGroups;
  for (const auto &b : bindings) {
    setGroups[b.set].push_back(b);
  }

  std::vector<DescriptorSetUniquePtr> allocatedSets;
  allocatedSets.reserve(setGroups.size());

  for (auto &kv : setGroups) {
    const u32 setIndex = kv.first;
    const auto &bindings = kv.second;

    auto setPtr = descriptorMgr.allocateSet(bindings);

    for (const auto &b : bindings) {
      auto it = resourceByName.find(LX_core::StringID(b.name));
      if (it == resourceByName.end()) {
        throwDescriptorBindingError(item, b,
                                    "no descriptor resource with this binding "
                                    "name was provided");
      }

      const auto &cpuRes = it->second;

      if (b.type == LX_core::ShaderPropertyType::UniformBuffer ||
          b.type == LX_core::ShaderPropertyType::StorageBuffer) {
        if (!cpuRes.isResource() || !cpuRes.resource().isValid()) {
          throwDescriptorBindingError(
              item, b,
              "descriptor resource is not a valid GPU buffer resource");
        }
        const LX_core::IGpuResource &resource = cpuRes.resource().get();
        auto bufferOpt =
            resourceManager.getBuffer(resource.getBackendCacheIdentity());
        if (!bufferOpt) {
          throwDescriptorBindingError(
              item, b,
              "GPU buffer was not uploaded before descriptor binding");
        }
        auto &buffer = bufferOpt->get();

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer.getHandle();
        bufferInfo.offset = 0;
        bufferInfo.range = buffer.getSize();

        logDescriptorBufferBindingIfChanged(item, b, resource, bufferInfo);
        setPtr->updateBuffer(b.binding, bufferInfo,
                             b.type ==
                                     LX_core::ShaderPropertyType::UniformBuffer
                                 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                 : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      } else if (b.type == LX_core::ShaderPropertyType::Texture2D ||
                 b.type == LX_core::ShaderPropertyType::TextureCube) {
        if (cpuRes.isTextureArray()) {
          const auto &textures = cpuRes.textures();
          if (textures.size() != b.descriptorCount) {
            throw std::runtime_error(
                "texture array resource descriptor count mismatch");
          }

          std::vector<VkDescriptorImageInfo> imageInfos;
          imageInfos.reserve(textures.size());
          bool complete = true;
          for (const auto &textureResource : textures) {
            if (!textureResource.isValid()) {
              complete = false;
              break;
            }
            auto textureOpt = resourceManager.getTexture(
                textureResource.getBackendCacheIdentity());
            if (!textureOpt) {
              complete = false;
              break;
            }
            imageInfos.push_back(textureOpt->get().getDescriptorInfo());
          }
          if (!complete) {
            throwDescriptorBindingError(
                item, b,
                "texture array contains an invalid or non-uploaded texture");
          }

          setPtr->updateImageArray(b.binding, imageInfos,
                                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
          continue;
        }

        if (!cpuRes.isResource() || !cpuRes.resource().isValid()) {
          throwDescriptorBindingError(
              item, b,
              "descriptor resource is not a valid texture resource");
        }
        const LX_core::IGpuResource &resource = cpuRes.resource().get();
        if (const auto *frameGraphResource =
                dynamic_cast<const LX_core::FrameGraphSampledResource *>(
                    &resource)) {
          auto attachmentOpt = resourceManager.getFrameGraphAttachment(
              frameGraphResource->getResourceName());
          if (!attachmentOpt) {
            throwDescriptorBindingError(
                item, b,
                "frame graph sampled attachment is not available");
          }
          auto &attachment = attachmentOpt->get();
          VkDescriptorImageInfo imageInfo =
              attachment.texture->getDescriptorInfo();
          logDescriptorImageBindingIfChanged(item, b, resource, imageInfo);
          setPtr->updateImage(b.binding, imageInfo,
                              VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
          continue;
        }

        auto textureOpt =
            resourceManager.getTexture(resource.getBackendCacheIdentity());
        if (!textureOpt) {
          throwDescriptorBindingError(
              item, b,
              "GPU texture was not uploaded before descriptor binding");
        }
        auto &texture = textureOpt->get();

        VkDescriptorImageInfo imageInfo = texture.getDescriptorInfo();
        logDescriptorImageBindingIfChanged(item, b, resource, imageInfo);
        setPtr->updateImage(b.binding, imageInfo,
                            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
      }
    }

    VkDescriptorSet setHandle = setPtr->getHandle();
    vkCmdBindDescriptorSets(m_handle, bindPoint, pipelineLayout, setIndex, 1,
                            &setHandle, 0, nullptr);
    allocatedSets.push_back(std::move(setPtr));
  }

  const auto &directRaster = item.directRaster;

  if (directRaster.vertexBuffer.isValid()) {
    auto vbOpt = resourceManager.getBuffer(
        directRaster.vertexBuffer.getBackendCacheIdentity());
    if (vbOpt) {
      VkBuffer vbHandle = vbOpt->get().getHandle();
      VkDeviceSize offsets[] = {0};
      vkCmdBindVertexBuffers(m_handle, 0, 1, &vbHandle, offsets);
    }
  }

  if (directRaster.indexBuffer.isValid()) {
    auto ibOpt = resourceManager.getBuffer(
        directRaster.indexBuffer.getBackendCacheIdentity());
    if (ibOpt) {
      vkCmdBindIndexBuffer(m_handle, ibOpt->get().getHandle(), 0,
                           VK_INDEX_TYPE_UINT32);
    }
  }

}

void VulkanCommandBuffer::bindResources(VulkanResourceManager &resourceManager,
                                        VulkanGraphicsPipeline &pipeline,
                                        const RenderWorkItem &item) {
  bindResourcesWithLayout(resourceManager, pipeline.getBindings(),
                          pipeline.getLayout(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                          item);
}

void VulkanCommandBuffer::bindResources(VulkanResourceManager &resourceManager,
                                        VulkanComputePipeline &pipeline,
                                        const RenderWorkItem &item) {
  bindResourcesWithLayout(resourceManager, pipeline.getBindings(),
                          pipeline.getLayout(), VK_PIPELINE_BIND_POINT_COMPUTE,
                          item);
}

void VulkanCommandBuffer::bindResources(VulkanResourceManager &resourceManager,
                                        VulkanPipelineRef pipeline,
                                        const RenderWorkItem &item) {
  std::visit([this, &resourceManager, &item](
                 auto ref) { bindResources(resourceManager, ref.get(), item); },
             pipeline);
}

void VulkanCommandBuffer::executeDirectRasterPassItem(
    const RenderWorkItem &item) {
  if (item.kind != RenderWorkKind::DirectRasterPass) {
    throw std::runtime_error(
        "executeDirectRasterPassItem received non-DirectRasterPass work item" +
        directRasterWorkItemContext(item));
  }

  const auto &directRaster = item.directRaster;
  if (!directRaster.vertexBuffer.isValid()) {
    throw std::runtime_error(
        "executeDirectRasterPassItem missing vertex buffer" +
        directRasterWorkItemContext(item));
  }
  if (!directRaster.indexBuffer.isValid()) {
    throw std::runtime_error(
        "executeDirectRasterPassItem missing index buffer" +
        directRasterWorkItemContext(item));
  }

  if (directRaster.indexCount == 0) {
    throw std::runtime_error(
        "executeDirectRasterPassItem has zero indexCount" +
        directRasterWorkItemContext(item));
  }
  vkCmdDrawIndexed(
      m_handle, directRaster.indexCount, directRaster.instanceCount,
      directRaster.firstIndex, directRaster.vertexOffset,
      directRaster.drawRecordIndex == u32_max ? 0u
                                              : directRaster.drawRecordIndex);
}

void VulkanCommandBuffer::executeComputeDispatchItem(
    const RenderWorkItem &item) {
  if (item.kind != RenderWorkKind::ComputeDispatch) {
    return;
  }
  vkCmdDispatch(m_handle, item.compute.groupCountX, item.compute.groupCountY,
                item.compute.groupCountZ);
}

void VulkanCommandBuffer::executeWorkItem(const RenderWorkItem &item) {
  switch (item.kind) {
  case RenderWorkKind::DirectRasterPass:
    executeDirectRasterPassItem(item);
    return;
  case RenderWorkKind::ComputeDispatch:
    executeComputeDispatchItem(item);
    return;
  case RenderWorkKind::RayTracingDispatch:
    throw std::runtime_error("RayTracingDispatch work is not implemented");
  case RenderWorkKind::Unspecified:
    throw std::runtime_error("Unspecified RenderWorkKind cannot be executed");
  }
  throw std::runtime_error("unknown RenderWorkKind");
}

} // namespace LX_core::backend
