#pragma once
#include "../renderer.hpp"
#include "vulkan_commandbuffer.hpp"
#include "vulkan_framebuffer.hpp"
#include "vulkan_renderpass.hpp"
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include "core/mesh/mesh.hpp"
#include "vulkan_descriptorset.hpp"
namespace LX_core {

class VulkanRenderer : public Renderer {
public:
  void recordCommandBuffer(uint32_t frameIndex);

  // 渲染目标
  int framesInFlight = 2;
  Rect2Di renderArea;
  std::vector<std::shared_ptr<VulkanFrameBuffer>> pFrameBuffers;
  std::vector<std::shared_ptr<VulkanCommandBuffer>> pCommandBuffers;

  // 渲染流程
  std::shared_ptr<VulkanRenderPass> pRenderPass;
  std::shared_ptr<VulkanPipeline> pPipeline;

  // Descriptor
  std::vector<std::shared_ptr<VulkanDescriptorSet>> pDescriptorSets;

  // 渲染输入
  std::vector<std::weak_ptr<LX_core::Mesh>> pMeshes;
};

} // namespace LX_graphics
