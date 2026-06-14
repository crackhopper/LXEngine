#pragma once

#include "core/asset/shader.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <vulkan/vulkan.h>
#include <memory>
#include <utility>
#include <vector>

namespace LX_core::backend {
class VulkanDevice;

class VulkanComputePipeline;
using VulkanComputePipelineUniquePtr = std::unique_ptr<VulkanComputePipeline>;

class VulkanComputePipeline final {
  struct Token {};

public:
  VulkanComputePipeline(Token, VulkanDevice &device,
                        const PipelineBuildDesc &buildInfo);
  ~VulkanComputePipeline();

  [[nodiscard]] static VulkanComputePipelineUniquePtr
  create(VulkanDevice &device, const PipelineBuildDesc &buildInfo) {
    return std::make_unique<VulkanComputePipeline>(Token{}, device, buildInfo);
  }

  [[nodiscard]] VkPipeline getHandle() const { return m_pipeline; }
  [[nodiscard]] VkPipelineLayout getLayout() const { return m_layout; }
  [[nodiscard]] const std::vector<ShaderResourceBinding> &getBindings() const {
    return m_bindings;
  }
  [[nodiscard]] const PushConstantRange &getPushConstantRange() const {
    return m_pushConstant;
  }

private:
  VulkanDevice &m_device;
  VkDevice m_deviceHandle = VK_NULL_HANDLE;
  VkPipelineLayout m_layout = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;
  VkShaderModule m_shaderModule = VK_NULL_HANDLE;
  std::vector<ShaderResourceBinding> m_bindings;
  PushConstantRange m_pushConstant{};
};

} // namespace LX_core::backend
