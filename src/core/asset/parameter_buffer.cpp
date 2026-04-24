#include "parameter_buffer.hpp"

#include <cstring>

namespace LX_core {

ParameterBuffer::ParameterBuffer(StringID bindingName,
                                 const ShaderResourceBinding &binding,
                                 ResourceType resType)
    : m_bindingName(bindingName), m_binding(binding),
      m_buffer(binding.size, uint8_t{0}), m_resType(resType) {}

void ParameterBuffer::writeBindingMember(StringID memberName, const void *src,
                                         size_t nbytes,
                                         ShaderPropertyType expected) {
  for (const auto &member : m_binding.get().members) {
    if (StringID(member.name) != memberName) {
      continue;
    }
    assert(member.type == expected &&
           "MaterialInstance setter type does not match reflected member type");
    assert(static_cast<size_t>(member.offset) + nbytes <= m_buffer.size() &&
           "UBO write would overflow the reflected buffer");
    std::memcpy(m_buffer.data() + member.offset, src, nbytes);
    m_dirty = true;
    return;
  }
  assert(false &&
         "MaterialInstance setter: member not found in parameter buffer");
}

} // namespace LX_core
