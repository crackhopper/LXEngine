#include "shader_impl.hpp"
#include <functional>

namespace LX_infra {

ShaderImpl::ShaderImpl(std::vector<LX_core::ShaderStageCode> stages,
                       std::vector<LX_core::ShaderResourceBinding> bindings)
    : m_stages(std::move(stages)), m_bindings(std::move(bindings)) {
  buildIndices();
  computeHash();
}

const std::vector<LX_core::ShaderStageCode> &
ShaderImpl::getAllStages() const {
  return m_stages;
}

const std::vector<LX_core::ShaderResourceBinding> &
ShaderImpl::getReflectionBindings() const {
  return m_bindings;
}

std::optional<std::reference_wrapper<const LX_core::ShaderResourceBinding>>
ShaderImpl::findBinding(uint32_t set, uint32_t binding) const {
  uint32_t key = (set << 16) | binding;
  auto it = m_setBindingIndex.find(key);
  if (it != m_setBindingIndex.end()) {
    return std::cref(m_bindings[it->second]);
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<const LX_core::ShaderResourceBinding>>
ShaderImpl::findBinding(const std::string &name) const {
  auto it = m_nameIndex.find(name);
  if (it != m_nameIndex.end()) {
    return std::cref(m_bindings[it->second]);
  }
  return std::nullopt;
}

size_t ShaderImpl::getProgramHash() const { return m_hash; }

void ShaderImpl::buildIndices() {
  for (size_t i = 0; i < m_bindings.size(); ++i) {
    const auto &b = m_bindings[i];
    uint32_t key = (b.set << 16) | b.binding;
    m_setBindingIndex[key] = i;
    if (!b.name.empty()) {
      m_nameIndex[b.name] = i;
    }
  }
}

void ShaderImpl::computeHash() {
  size_t h = 0;
  for (const auto &stage : m_stages) {
    for (auto word : stage.bytecode) {
      LX_core::hash_combine(h, word);
    }
  }
  m_hash = h;
}

} // namespace LX_infra
