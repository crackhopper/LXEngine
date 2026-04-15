#include "command_buffer.hpp"
#include "../descriptors/descriptor_manager.hpp"
#include "../pipelines/pipeline.hpp"
#include "../device_resources/buffer.hpp"
#include "../device_resources/texture.hpp"
#include "../device.hpp"
#include "../resource_manager.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace LX_core::backend {

namespace {
bool envEnabled(const char *name) {
  const char *value = std::getenv(name);
  return value != nullptr && std::strcmp(value, "0") != 0;
}
} // namespace

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
  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(m_handle, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanCommandBuffer::setViewport(uint32_t width, uint32_t height) {
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.width = static_cast<float>(width);
  if (envEnabled("LX_RENDER_FLIP_VIEWPORT_Y")) {
    viewport.y = static_cast<float>(height);
    viewport.height = -static_cast<float>(height);
  } else {
    viewport.y = 0.0f;
    viewport.height = static_cast<float>(height);
  }
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(m_handle, 0, 1, &viewport);
}

void VulkanCommandBuffer::setScissor(uint32_t width, uint32_t height) {
  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = {width, height};
  vkCmdSetScissor(m_handle, 0, 1, &scissor);
}

namespace {
VkShaderStageFlags pushConstantStageMaskToVk(uint32_t mask) {
  VkShaderStageFlags out = 0;
  if (mask & static_cast<uint32_t>(LX_core::ShaderStage::Vertex))
    out |= VK_SHADER_STAGE_VERTEX_BIT;
  if (mask & static_cast<uint32_t>(LX_core::ShaderStage::Fragment))
    out |= VK_SHADER_STAGE_FRAGMENT_BIT;
  if (mask & static_cast<uint32_t>(LX_core::ShaderStage::Compute))
    out |= VK_SHADER_STAGE_COMPUTE_BIT;
  if (mask & static_cast<uint32_t>(LX_core::ShaderStage::Geometry))
    out |= VK_SHADER_STAGE_GEOMETRY_BIT;
  return out;
}
} // namespace

void VulkanCommandBuffer::bindPipeline(VulkanPipeline &pipeline) {
  vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.getHandle());
  m_pipelineLayout = pipeline.getLayout();
  const auto &pcr = pipeline.getPushConstantRange();
  m_pushConstants.stageFlags = pushConstantStageMaskToVk(pcr.stageFlagsMask);
  m_pushConstants.offset = pcr.offset;
  m_pushConstants.size = pcr.size;
}

void VulkanCommandBuffer::bindResources(VulkanResourceManager &resourceManager,
                                        VulkanPipeline &pipeline,
                                        const RenderingItem &item) {
  auto &descriptorMgr = m_device.getDescriptorManager();

  // Build a name→resource map from the item's descriptorResources so backend
  // routing can match reflected binding names without any slot enum.
  std::unordered_map<LX_core::StringID, LX_core::IRenderResourcePtr,
                     LX_core::StringID::Hash>
      resourceByName;
  for (const auto &cpuRes : item.descriptorResources) {
    if (!cpuRes)
      continue;
    auto name = cpuRes->getBindingName();
    if (name.id == 0)
      continue;
    resourceByName.emplace(name, cpuRes);
  }

  // Group reflection bindings by descriptor set index.
  std::unordered_map<uint32_t, std::vector<LX_core::ShaderResourceBinding>>
      setGroups;
  for (const auto &b : pipeline.getBindings()) {
    setGroups[b.set].push_back(b);
  }

  std::vector<DescriptorSetPtr> allocatedSets;
  allocatedSets.reserve(setGroups.size());

  for (auto &kv : setGroups) {
    const uint32_t setIndex = kv.first;
    const auto &bindings = kv.second;

    auto setPtr = descriptorMgr.allocateSet(bindings);

    for (const auto &b : bindings) {
      auto it = resourceByName.find(LX_core::StringID(b.name));
      if (it == resourceByName.end())
        continue; // Leave descriptor uninitialized (shader should not access
                  // it).

      const auto &cpuRes = it->second;

      if (b.type == LX_core::ShaderPropertyType::UniformBuffer ||
          b.type == LX_core::ShaderPropertyType::StorageBuffer) {
        auto bufferOpt = resourceManager.getBuffer(cpuRes->getResourceHandle());
        if (!bufferOpt)
          continue;
        auto &buffer = bufferOpt->get();

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer.getHandle();
        bufferInfo.offset = 0;
        bufferInfo.range = buffer.getSize();

        setPtr->updateBuffer(b.binding, bufferInfo,
                             b.type ==
                                     LX_core::ShaderPropertyType::UniformBuffer
                                 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                 : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      } else if (b.type == LX_core::ShaderPropertyType::Texture2D ||
                 b.type == LX_core::ShaderPropertyType::TextureCube) {
        auto textureOpt =
            resourceManager.getTexture(cpuRes->getResourceHandle());
        if (!textureOpt)
          continue;
        auto &texture = textureOpt->get();

        VkDescriptorImageInfo imageInfo = texture.getDescriptorInfo();
        setPtr->updateImage(b.binding, imageInfo,
                            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
      }
    }

    VkDescriptorSet setHandle = setPtr->getHandle();
    vkCmdBindDescriptorSets(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline.getLayout(), setIndex, 1, &setHandle, 0,
                            nullptr);
    allocatedSets.push_back(std::move(setPtr));
  }

  if (item.vertexBuffer) {
    auto vbOpt =
        resourceManager.getBuffer(item.vertexBuffer->getResourceHandle());
    if (vbOpt) {
      VkBuffer vbHandle = vbOpt->get().getHandle();
      VkDeviceSize offsets[] = {0};
      vkCmdBindVertexBuffers(m_handle, 0, 1, &vbHandle, offsets);
    }
  }

  if (item.indexBuffer) {
    auto ibOpt =
        resourceManager.getBuffer(item.indexBuffer->getResourceHandle());
    if (ibOpt) {
      vkCmdBindIndexBuffer(m_handle, ibOpt->get().getHandle(), 0,
                           VK_INDEX_TYPE_UINT32);
    }
  }

  if (item.objectInfo && m_pushConstants.size > 0) {
    vkCmdPushConstants(m_handle, m_pipelineLayout, m_pushConstants.stageFlags,
                       m_pushConstants.offset, m_pushConstants.size,
                       item.objectInfo->getRawData());
  }
}

void VulkanCommandBuffer::drawItem(const RenderingItem &item) {
  if (!item.vertexBuffer || !item.indexBuffer) {
    return;
  }

  // Indexed draw.
  const uint32_t indexCount =
      static_cast<uint32_t>(item.indexBuffer->getByteSize() / sizeof(uint32_t));
  if (indexCount == 0) {
    return;
  }
  vkCmdDrawIndexed(m_handle, indexCount, 1, 0, 0, 0);
}

} // namespace LX_core::backend
