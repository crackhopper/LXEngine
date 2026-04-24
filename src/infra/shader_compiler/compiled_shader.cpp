#include "compiled_shader.hpp"
#include <functional>

namespace LX_infra {

CompiledShader::CompiledShader(std::vector<LX_core::ShaderStageCode> stages,
                               std::vector<LX_core::ShaderResourceBinding> bindings,
                               std::vector<LX_core::VertexInputAttribute> vertexInputs,
                               std::string logicalName)
    : m_stages(std::move(stages)), m_bindings(std::move(bindings)),
      m_vertexInputs(std::move(vertexInputs)),
      m_logicalName(std::move(logicalName)) {
  buildIndices();
  computeHash();
}

const std::vector<LX_core::ShaderStageCode> &
CompiledShader::getAllStages() const {
  return m_stages;
}

const std::vector<LX_core::ShaderResourceBinding> &
CompiledShader::getReflectionBindings() const {
  return m_bindings;
}

const std::vector<LX_core::VertexInputAttribute> &
CompiledShader::getVertexInputs() const {
  return m_vertexInputs;
}

std::optional<std::reference_wrapper<const LX_core::ShaderResourceBinding>>
CompiledShader::findBinding(u32 set, u32 binding) const {
  const u32 key = (set << 16) | binding;
  auto it = m_setBindingIndex.find(key);
  if (it != m_setBindingIndex.end()) {
    return std::cref(m_bindings[it->second]);
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<const LX_core::ShaderResourceBinding>>
CompiledShader::findBinding(const std::string &name) const {
  auto it = m_nameIndex.find(name);
  if (it != m_nameIndex.end()) {
    return std::cref(m_bindings[it->second]);
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<const LX_core::VertexInputAttribute>>
CompiledShader::findVertexInput(u32 location) const {
  auto it = m_vertexInputIndex.find(location);
  if (it != m_vertexInputIndex.end()) {
    return std::cref(m_vertexInputs[it->second]);
  }
  return std::nullopt;
}

size_t CompiledShader::getProgramHash() const { return m_hash; }

void CompiledShader::buildIndices() {
  for (size_t i = 0; i < m_bindings.size(); ++i) {
    const auto &b = m_bindings[i];
    const u32 key = (b.set << 16) | b.binding;
    m_setBindingIndex[key] = i;
    if (!b.name.empty()) {
      m_nameIndex[b.name] = i;
    }
  }
  for (size_t i = 0; i < m_vertexInputs.size(); ++i) {
    m_vertexInputIndex[m_vertexInputs[i].location] = i;
  }
}

void CompiledShader::computeHash() {
  size_t h = 0;
  for (const auto &stage : m_stages) {
    for (auto word : stage.bytecode) {
      LX_core::hash_combine(h, word);
    }
  }
  m_hash = h;
}

} // namespace LX_infra
