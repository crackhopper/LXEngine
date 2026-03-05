#pragma once
#include "../pipeline_system.hpp"
#include <vulkan/vulkan.h>

namespace LX_core {

class VulkanCommandBuffer : public CommandBuffer {
public:
  // 这两个配套使用，当不是render pass时，调用这两个函数
  virtual void begin() override;
  virtual void end() override;

  // 这两个配套使用，当是render pass时，调用这两个函数
  virtual void beginRenderPass(const RenderPassInfo &renderPassInfo) override;
  virtual void endRenderPass() override;

  // 渲染中的子流程
  virtual void bindPipeline(const PipelineInfo &pipelineInfo) override;
  virtual void bindMeshList(const MeshListInfo &meshListInfo) override;
  virtual void bindDescriptorSet(const DescriptorSetInfo &desSetInfo) override;
  // 动态变量
  virtual void setViewport(const Rect2Di *viewport = nullptr) override;
  virtual void setScissor(const Rect2Di *scissor = nullptr) override;

private:
  VkCommandBuffer commandBuffer;
  Rect2Di currentRenderArea;
};
} // namespace LX_graphics