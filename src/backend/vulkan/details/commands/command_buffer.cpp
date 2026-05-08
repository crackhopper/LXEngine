#include "command_buffer.hpp"
#include "../descriptors/descriptor_manager.hpp"
#include "../pipelines/pipeline.hpp"
#include "../device_resources/buffer.hpp"
#include "../device_resources/texture.hpp"
#include "../device.hpp"
#include "../resource_manager.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"
#include <array>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace LX_core::backend {

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

void VulkanCommandBuffer::setViewport(u32 width, u32 height) {
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.width = static_cast<float>(width);
  if (expEnvEnabled("LX_RENDER_FLIP_VIEWPORT_Y")) {
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

void VulkanCommandBuffer::setScissor(u32 width, u32 height) {
  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = {width, height};
  vkCmdSetScissor(m_handle, 0, 1, &scissor);
}

namespace {
constexpr int kDebugBurstFrames = 3;

VkShaderStageFlags pushConstantStageMaskToVk(ShaderStageMask32 mask) {
  VkShaderStageFlags out = 0;
  if (mask & static_cast<ShaderStageMask32>(LX_core::ShaderStage::Vertex))
    out |= VK_SHADER_STAGE_VERTEX_BIT;
  if (mask & static_cast<ShaderStageMask32>(LX_core::ShaderStage::Fragment))
    out |= VK_SHADER_STAGE_FRAGMENT_BIT;
  if (mask & static_cast<ShaderStageMask32>(LX_core::ShaderStage::Compute))
    out |= VK_SHADER_STAGE_COMPUTE_BIT;
  if (mask & static_cast<ShaderStageMask32>(LX_core::ShaderStage::Geometry))
    out |= VK_SHADER_STAGE_GEOMETRY_BIT;
  return out;
}

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

void logMissingDescriptorBindingOnce(const RenderingItem &item,
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
            << " binding=" << binding.binding
            << " pass="
            << LX_core::GlobalStringTable::get().getName(item.pass.id)
            << " pipelineKey=" << item.pipelineKey.id.id << std::endl;
}

void logDescriptorBufferBindingIfChanged(
    const RenderingItem &item, const ShaderResourceBinding &binding,
    const IGpuResourceSharedPtr &cpuRes,
    const VkDescriptorBufferInfo &bufferInfo) {
  if (!expRendererDebugEnabled() || !cpuRes) {
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
  next.identity = cpuRes->getBackendCacheIdentity();
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
            << " bufferToken=" << next.bufferToken
            << " range=" << next.range << std::endl;
}

void logDescriptorImageBindingIfChanged(
    const RenderingItem &item, const ShaderResourceBinding &binding,
    const IGpuResourceSharedPtr &cpuRes,
    const VkDescriptorImageInfo &imageInfo) {
  if (!expRendererDebugEnabled() || !cpuRes) {
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
  next.identity = cpuRes->getBackendCacheIdentity();
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
  std::unordered_map<LX_core::StringID, LX_core::IGpuResourceSharedPtr,
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
  std::unordered_map<u32, std::vector<LX_core::ShaderResourceBinding>>
      setGroups;
  for (const auto &b : pipeline.getBindings()) {
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
        logMissingDescriptorBindingOnce(item, b);
        continue; // Leave descriptor uninitialized (shader should not access
                  // it).
      }

      const auto &cpuRes = it->second;

      if (b.type == LX_core::ShaderPropertyType::UniformBuffer ||
          b.type == LX_core::ShaderPropertyType::StorageBuffer) {
        auto bufferOpt =
            resourceManager.getBuffer(cpuRes->getBackendCacheIdentity());
        if (!bufferOpt)
          continue;
        auto &buffer = bufferOpt->get();

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer.getHandle();
        bufferInfo.offset = 0;
        bufferInfo.range = buffer.getSize();

        logDescriptorBufferBindingIfChanged(item, b, cpuRes, bufferInfo);
        setPtr->updateBuffer(b.binding, bufferInfo,
                             b.type ==
                                     LX_core::ShaderPropertyType::UniformBuffer
                                 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                 : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      } else if (b.type == LX_core::ShaderPropertyType::Texture2D ||
                 b.type == LX_core::ShaderPropertyType::TextureCube) {
        auto textureOpt =
            resourceManager.getTexture(cpuRes->getBackendCacheIdentity());
        if (!textureOpt)
          continue;
        auto &texture = textureOpt->get();

        VkDescriptorImageInfo imageInfo = texture.getDescriptorInfo();
        logDescriptorImageBindingIfChanged(item, b, cpuRes, imageInfo);
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
        resourceManager.getBuffer(item.vertexBuffer->getBackendCacheIdentity());
    if (vbOpt) {
      VkBuffer vbHandle = vbOpt->get().getHandle();
      VkDeviceSize offsets[] = {0};
      vkCmdBindVertexBuffers(m_handle, 0, 1, &vbHandle, offsets);
    }
  }

  if (item.indexBuffer) {
    auto ibOpt =
        resourceManager.getBuffer(item.indexBuffer->getBackendCacheIdentity());
    if (ibOpt) {
      vkCmdBindIndexBuffer(m_handle, ibOpt->get().getHandle(), 0,
                           VK_INDEX_TYPE_UINT32);
    }
  }

  if (item.drawData && m_pushConstants.size > 0) {
    vkCmdPushConstants(m_handle, m_pipelineLayout, m_pushConstants.stageFlags,
                       m_pushConstants.offset, m_pushConstants.size,
                       item.drawData->rawData());
  }
}

void VulkanCommandBuffer::drawItem(const RenderingItem &item) {
  if (!item.vertexBuffer || !item.indexBuffer) {
    return;
  }

  // Indexed draw.
  const usize indexCount = item.indexBuffer->getByteSize() / sizeof(u32);
  if (indexCount == 0) {
    return;
  }
  vkCmdDrawIndexed(m_handle, static_cast<u32>(indexCount), 1, 0, 0, 0);
}

} // namespace LX_core::backend
