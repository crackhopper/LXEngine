#pragma once

#include "core/resources/material.hpp"
#include "core/resources/mesh.hpp"
#include "core/resources/shader.hpp"
#include "core/resources/skeleton.hpp"
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

  /// Assemble identity by querying getPipelineHash() on each participating
  /// resource. `skeleton` may be null (contributes 0 — no skinning).
  static PipelineKey build(const ShaderProgramSet &shaderSet, const Mesh &mesh,
                           const RenderState &renderState,
                           const SkeletonPtr &skeleton);
};

} // namespace LX_core
