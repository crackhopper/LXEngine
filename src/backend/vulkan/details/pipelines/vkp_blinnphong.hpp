#pragma once
#include "core/resources/vertex_buffer.hpp"
#include "vkp_pipeline.hpp"
#include "vkp_pipeline_slot.hpp"
#include <vector>
#include <vulkan/vulkan.h>

#include "core/scene/camera.hpp"
#include "core/scene/components/material.hpp"
#include "core/scene/components/skeleton.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"

namespace LX_core {
namespace backend {

// 定义与 Shader 匹配的 Push Constant 结构 (C++ 侧)
using BlinnPhongPushConstant = PC_BlinnPhong;

// descriptor 参数
constexpr PipelineSlotDetails VkPipelineBlinnPhongSlotDetails[] = {
    {PipelineSlotId::LightUBO, ResourceType::UniformBuffer,
     PipelineSlotStage::ALL, 0, 0, DirectionalLightUBO::ResourceSize},
    {PipelineSlotId::CameraUBO, ResourceType::UniformBuffer,
     PipelineSlotStage::ALL, 1, 0, CameraUBO::ResourceSize},
    {PipelineSlotId::MaterialUBO, ResourceType::UniformBuffer,
     PipelineSlotStage::FRAGMENT, 2, 0, MaterialBlinnPhongUBO::ResourceSize},
    {PipelineSlotId::AlbedoTexture, ResourceType::CombinedImageSampler,
     PipelineSlotStage::FRAGMENT, 2, 1, 0},
    {PipelineSlotId::NormalTexture, ResourceType::CombinedImageSampler,
     PipelineSlotStage::ALL, 2, 2, 0},
    {PipelineSlotId::SkeletonUBO, ResourceType::UniformBuffer,
     PipelineSlotStage::VERTEX, 3, 0, SkeletonUBO::ResourceSize},
};

class VkPipelineBlinnPhong : public VulkanPipeline {
  using VertexType = VertexPosNormalUvBone;

public:
  using VulkanPipeline::VulkanPipeline;

  static VulkanPipelinePtr create(VulkanDevice &device, VkExtent2D extent) {
    PushConstantDetails pushConstant;
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.size = sizeof(BlinnPhongPushConstant);
    pushConstant.offset = 0;

    auto p = std::make_unique<VkPipelineBlinnPhong>(
        Token{}, device, extent, "blinnphong_0",
        const_cast<PipelineSlotDetails *>(VkPipelineBlinnPhongSlotDetails),
        static_cast<uint32_t>(sizeof(VkPipelineBlinnPhongSlotDetails) / sizeof(PipelineSlotDetails)),
        pushConstant);

    p->loadShaders();
    p->createLayout();
    return p;
  }

  VertexFormat getVertexFormat() const override {
    return VertexFormat::PosNormalUvBone;
  }
  std::string getShaderName() const override { return m_shaderName; }
  std::string getPipelineId() const override { return m_pipelineId; }

private:
  std::string m_pipelineId = "blinnphong";
  std::string m_shaderName = "blinnphong_0";
};

} // namespace backend
} // namespace LX_core
