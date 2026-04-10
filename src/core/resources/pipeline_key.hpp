#pragma once

#include "core/resources/index_buffer.hpp"
#include "core/resources/material.hpp"
#include "core/resources/shader.hpp"
#include "core/utils/string_table.hpp"
#include <sstream>
#include <string>

namespace LX_core {

struct PipelineKey {
  StringID id;

  bool operator==(const PipelineKey &rhs) const { return id == rhs.id; }
  bool operator!=(const PipelineKey &rhs) const { return id != rhs.id; }

  struct Hash {
    size_t operator()(const PipelineKey &k) const {
      return StringID::Hash{}(k.id);
    }
  };

  /// Assemble identity from shader program, mesh pipeline hash (layout + index
  /// topology), render state, primitive topology, and skinning participation.
  static PipelineKey build(const ShaderProgramSet &shaderSet,
                           size_t meshPipelineHash, const RenderState &renderState,
                           PrimitiveTopology topology, bool hasSkeleton);
};

} // namespace LX_core
