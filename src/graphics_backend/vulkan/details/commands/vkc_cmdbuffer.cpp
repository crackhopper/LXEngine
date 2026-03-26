#include "vkc_cmdbuffer.hpp"
#include "../vk_device.hpp"
#include "../vk_resource_manager.hpp"
#include "../descriptors/vkd_descriptor_manager.hpp"
#include "../pipelines/vkp_pipeline.hpp"
#include "../pipelines/vkp_blinnphong.hpp"
#include "../resources/vkr_buffer.hpp"
#include "../resources/vkr_texture.hpp"
#include <array>
#include <stdexcept>
#include <unordered_map>

namespace LX_core::graphic_backend {

void VulkanCommandBuffer::begin() {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = 0;
  beginInfo.pInheritanceInfo = nullptr;
  if (vkBeginCommandBuffer(m_handle, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("Failed to begin command buffer");
  }
}

void VulkanCommandBuffer::end() {
  vkEndCommandBuffer(m_handle);
}

void VulkanCommandBuffer::beginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                                        VkExtent2D extent, const std::vector<VkClearValue> &clearValues) {
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
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(width);
  viewport.height = static_cast<float>(height);
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

void VulkanCommandBuffer::bindPipeline(VulkanPipelineBase &pipeline) {
  vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.getHandle());
  m_pipelineLayout = pipeline.getLayout();
  m_pushConstantsDetails = pipeline.getPushConstantDetails();
}

void VulkanCommandBuffer::bindResources(VulkanResourceManager &resourceManager, VulkanPipelineBase &pipeline, const RenderItem &item) {
  auto &descriptorMgr = m_device.getDescriptorManager();

  auto findBySlotId = [&](PipelineSlotId slotId) -> IRenderResourcePtr {
    for (const auto &cpuRes : item.descriptorResources) {
      if (cpuRes && cpuRes->getPipelineSlotId() == slotId) {
        return cpuRes;
      }
    }
    return nullptr;
  };

  // Group pipeline slots by descriptor set index.
  std::unordered_map<uint32_t, std::vector<PipelineSlotDetails>> setGroups;
  for (const auto &slot : pipeline.getSlots()) {
    setGroups[slot.setIndex].push_back(slot);
  }

  // Keep allocated descriptor sets alive until the end of this recording call.
  std::vector<DescriptorSetPtr> allocatedSets;
  allocatedSets.reserve(setGroups.size());

  for (auto &kv : setGroups) {
    const uint32_t setIndex = kv.first;
    const auto &slots = kv.second;

    auto setPtr = descriptorMgr.allocateSet(slots);

    for (const auto &slot : slots) {
      auto cpuRes = findBySlotId(slot.id);
      if (!cpuRes) {
        continue; // Leave descriptor uninitialized (shader should not access it when disabled).
      }

      if (slot.type == ResourceType::UniformBuffer) {
        auto bufferOpt = resourceManager.getBuffer(cpuRes->getResourceHandle());
        if (!bufferOpt) {
          continue;
        }
        auto &buffer = bufferOpt->get();

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer.getHandle();
        bufferInfo.offset = 0;
        bufferInfo.range = buffer.getSize();

        setPtr->updateBuffer(slot.binding, bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
      } else if (slot.type == ResourceType::CombinedImageSampler) {
        auto textureOpt = resourceManager.getTexture(cpuRes->getResourceHandle());
        if (!textureOpt) {
          continue;
        }
        auto &texture = textureOpt->get();

        VkDescriptorImageInfo imageInfo = texture.getDescriptorInfo();
        setPtr->updateImage(slot.binding, imageInfo,
                            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
      }
    }

    VkDescriptorSet setHandle = setPtr->getHandle();
    vkCmdBindDescriptorSets(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline.getLayout(), setIndex, 1, &setHandle,
                            0, nullptr);
    allocatedSets.push_back(std::move(setPtr));
  }

  // Bind vertex buffer.
  if (item.vertexBuffer) {
    auto vbOpt = resourceManager.getBuffer(item.vertexBuffer->getResourceHandle());
    if (vbOpt) {
      VkBuffer vbHandle = vbOpt->get().getHandle();
      VkDeviceSize offsets[] = {0};
      vkCmdBindVertexBuffers(m_handle, 0, 1, &vbHandle, offsets);
    }
  }

  // Bind index buffer.
  if (item.indexBuffer) {
    auto ibOpt = resourceManager.getBuffer(item.indexBuffer->getResourceHandle());
    if (ibOpt) {
      vkCmdBindIndexBuffer(m_handle, ibOpt->get().getHandle(), 0, VK_INDEX_TYPE_UINT32);
    }
  }

  // Push constants (object-level data: model matrix, etc.).
  if (item.objectInfo && m_pushConstantsDetails.size > 0) {
    vkCmdPushConstants(m_handle, m_pipelineLayout,
                       m_pushConstantsDetails.stageFlags,
                       m_pushConstantsDetails.offset,
                       m_pushConstantsDetails.size,
                       item.objectInfo->getRawData());
  }
}

void VulkanCommandBuffer::drawItem(const RenderItem &item) {
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

} // namespace LX_core::graphic_backend
