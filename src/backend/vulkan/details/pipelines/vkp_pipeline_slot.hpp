#pragma once
#include "core/gpu/render_resource.hpp"

namespace LX_core {
namespace backend {

enum class PipelineSlotStage : u8 {
  NONE = 0,
  VERTEX = 1,
  FRAGMENT = 2,
  ALL = VERTEX | FRAGMENT,
};


struct PipelineSlotDetails {
  LX_core::PipelineSlotId id = LX_core::PipelineSlotId::None;
  LX_core::ResourceType type = LX_core::ResourceType::None;
  PipelineSlotStage stage = PipelineSlotStage::NONE;
  u32 setIndex = 0;
  u32 binding = 0;
  usize size = 0;
};



} // namespace backend
} // namespace LX_core