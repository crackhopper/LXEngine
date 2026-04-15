#include "graphics_shader_program.hpp"
#include "../device.hpp"

namespace LX_core::backend {

VulkanShaderGraphicsPipeline::VulkanShaderGraphicsPipeline(
    Token t, VulkanDevice &device, const PipelineBuildDesc &buildInfo,
    std::string shaderName)
    : VulkanPipeline(t, device, buildInfo),
      m_shaderName(std::move(shaderName)) {}

VulkanPipelinePtr VulkanShaderGraphicsPipeline::create(
    VulkanDevice &device, const PipelineBuildDesc &buildInfo,
    VkRenderPass renderPass, std::string shaderName) {
  auto pipeline = VulkanPipelinePtr(new VulkanShaderGraphicsPipeline(
      Token{}, device, buildInfo, std::move(shaderName)));
  pipeline->loadShaders();
  pipeline->createLayout();
  pipeline->buildGraphicsPpl(renderPass);
  return pipeline;
}

} // namespace LX_core::backend
