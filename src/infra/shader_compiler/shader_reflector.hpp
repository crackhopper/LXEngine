#pragma once
#include "core/asset/shader.hpp"
#include <vector>

namespace LX_infra {

class ShaderReflector {
public:
  /// Reflect on multiple compiled stages and produce a merged list of bindings.
  /// Bindings at the same (set, binding) across stages are merged with combined
  /// stageFlags.
  static std::vector<LX_core::ShaderResourceBinding>
  reflect(const std::vector<LX_core::ShaderStageCode> &stages);

  static std::vector<LX_core::VertexInputAttribute>
  reflectVertexInputs(const std::vector<LX_core::ShaderStageCode> &stages);

  static std::vector<LX_core::ShaderSpecializationConstantInfo>
  reflectSpecializationConstants(
      const std::vector<LX_core::ShaderStageCode> &stages);

private:
  /// Reflect a single stage's SPIR-V bytecode.
  static std::vector<LX_core::ShaderResourceBinding>
  reflectSingleStage(const LX_core::ShaderStageCode &stage);

  static std::vector<LX_core::VertexInputAttribute>
  reflectSingleStageInputs(const LX_core::ShaderStageCode &stage);

  static std::vector<LX_core::ShaderSpecializationConstantInfo>
  reflectSingleStageSpecializationConstants(
      const LX_core::ShaderStageCode &stage);
};

} // namespace LX_infra
