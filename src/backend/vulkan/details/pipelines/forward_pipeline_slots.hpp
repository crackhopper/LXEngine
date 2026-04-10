#pragma once

#include "core/gpu/render_resource.hpp"
#include "core/resources/material.hpp"
#include "core/resources/skeleton.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "vkp_pipeline_slot.hpp"
#include <vector>

namespace LX_core::backend {

/// Descriptor slot table for `blinnphong_0` forward shading (matches legacy
/// VkPipelineBlinnPhong layout).
inline std::vector<PipelineSlotDetails> blinnPhongForwardSlots() {
  return {
      {PipelineSlotId::LightUBO, ResourceType::UniformBuffer,
       PipelineSlotStage::ALL, 0, 0, DirectionalLightUBO::ResourceSize},
      {PipelineSlotId::CameraUBO, ResourceType::UniformBuffer,
       PipelineSlotStage::ALL, 1, 0, CameraUBO::ResourceSize},
      {PipelineSlotId::MaterialUBO, ResourceType::UniformBuffer,
       PipelineSlotStage::FRAGMENT, 2, 0, BlinnPhongMaterialUBO::ResourceSize},
      {PipelineSlotId::AlbedoTexture, ResourceType::CombinedImageSampler,
       PipelineSlotStage::FRAGMENT, 2, 1, 0},
      {PipelineSlotId::NormalTexture, ResourceType::CombinedImageSampler,
       PipelineSlotStage::ALL, 2, 2, 0},
      {PipelineSlotId::SkeletonUBO, ResourceType::UniformBuffer,
       PipelineSlotStage::VERTEX, 3, 0, SkeletonUBO::ResourceSize},
  };
}

inline PushConstantDetails blinnPhongPushConstants() {
  PushConstantDetails pc{};
  pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pc.size = sizeof(PC_Draw);
  pc.offset = 0;
  return pc;
}

} // namespace LX_core::backend
