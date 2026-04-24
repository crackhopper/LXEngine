#pragma once
#include "core/asset/shader.hpp"
#include <unordered_map>

namespace LX_infra {

class CompiledShader : public LX_core::IShader {
public:
  CompiledShader(std::vector<LX_core::ShaderStageCode> stages,
                 std::vector<LX_core::ShaderResourceBinding> bindings,
                 std::vector<LX_core::VertexInputAttribute> vertexInputs = {},
                 std::string logicalName = {});

  // --- IShader interface ---
  const std::vector<LX_core::ShaderStageCode> &getAllStages() const override;

  const std::vector<LX_core::ShaderResourceBinding> &
  getReflectionBindings() const override;

  const std::vector<LX_core::VertexInputAttribute> &
  getVertexInputs() const override;

  std::optional<std::reference_wrapper<const LX_core::ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override;

  std::optional<std::reference_wrapper<const LX_core::ShaderResourceBinding>>
  findBinding(const std::string &name) const override;

  std::optional<std::reference_wrapper<const LX_core::VertexInputAttribute>>
  findVertexInput(u32 location) const override;

  usize getProgramHash() const override;

  std::string getShaderName() const override { return m_logicalName; }

private:
  void buildIndices();
  void computeHash();

  std::vector<LX_core::ShaderStageCode> m_stages;
  std::vector<LX_core::ShaderResourceBinding> m_bindings;
  std::vector<LX_core::VertexInputAttribute> m_vertexInputs;

  // Fast lookup: packed (set << 16 | binding) -> index in m_bindings
  std::unordered_map<u32, usize> m_setBindingIndex;
  // Fast lookup: name -> index in m_bindings
  std::unordered_map<std::string, usize> m_nameIndex;
  // Fast lookup: location -> index in m_vertexInputs
  std::unordered_map<u32, usize> m_vertexInputIndex;

  usize m_hash = 0;
  std::string m_logicalName;
};

} // namespace LX_infra
