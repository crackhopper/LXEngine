#pragma once

#include "core/rhi/index_buffer.hpp"
#include "core/asset/material_instance.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/asset/shader.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include <cstdint>
#include <vector>

namespace LX_core {

struct RenderingItem; // forward decl

/// Backend-agnostic push constant declaration. Engine-wide convention today is
/// a 128-byte range visible to vertex + fragment stages; future shader-declared
/// ranges can extend this struct without touching backend-neutral code.
struct PushConstantRange {
  u32 offset = 0;
  u32 size = 128;
  /// Bitmask of ShaderStage values that need access. Default: Vertex |
  /// Fragment.
  ShaderStageMask32 stageFlagsMask =
      static_cast<ShaderStageMask32>(ShaderStage::Vertex) |
      static_cast<ShaderStageMask32>(ShaderStage::Fragment);
};

/// Aggregates every input a backend needs to construct a graphics pipeline.
/// Single source of truth for "what does this pipeline need" — built from a
/// RenderingItem via `fromRenderingItem`; backends translate its fields to
/// their own types (e.g., VkVertexInputAttributeDescription).
struct PipelineBuildDesc {
  PipelineKey key;
  std::vector<ShaderStageCode> stages;
  std::vector<ShaderResourceBinding> bindings;
  VertexLayout vertexLayout;
  RenderState renderState;
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
  PushConstantRange pushConstant;

  /// Derive a complete PipelineBuildDesc from a fully-built RenderingItem.
  /// Requires `item.shaderInfo`, `item.vertexBuffer`, `item.indexBuffer`, and
  /// `item.material` to be non-null.
  static PipelineBuildDesc fromRenderingItem(const RenderingItem &item);
};

} // namespace LX_core
