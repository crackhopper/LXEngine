#include "material_instance.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <utility>

namespace LX_core {

namespace {

bool isSupportedMaterialDescriptorType(ShaderPropertyType type) {
  switch (type) {
  case ShaderPropertyType::UniformBuffer:
  case ShaderPropertyType::StorageBuffer:
  case ShaderPropertyType::Texture2D:
  case ShaderPropertyType::TextureCube:
    return true;
  default:
    return false;
  }
}

bool isBufferType(ShaderPropertyType type) {
  return type == ShaderPropertyType::UniformBuffer ||
         type == ShaderPropertyType::StorageBuffer;
}

ResourceType toResourceType(ShaderPropertyType type) {
  switch (type) {
  case ShaderPropertyType::StorageBuffer:
    return ResourceType::StorageBuffer;
  case ShaderPropertyType::UniformBuffer:
    return ResourceType::UniformBuffer;
  default:
    assert(false && "toResourceType called with non-buffer type");
    return ResourceType::UniformBuffer;
  }
}

const std::vector<uint8_t> kEmptyBuffer;

[[noreturn]] void fatalUndefinedPass(const MaterialTemplate *tmpl,
                                     StringID pass) {
  std::cerr << "FATAL [MaterialInstance] template="
            << (tmpl ? tmpl->getName() : std::string("<null>"))
            << " pass=" << GlobalStringTable::get().toDebugString(pass)
            << " reason=setPassEnabled called for undefined pass" << std::endl;
  std::terminate();
}

[[noreturn]] void fatalUnsupportedDescriptorType(const std::string &name,
                                                  ShaderPropertyType type) {
  std::cerr << "FATAL [MaterialInstance] binding='" << name
            << "' has unsupported material-owned descriptor type ("
            << static_cast<int>(type) << ")" << std::endl;
  std::terminate();
}

} // namespace

/*****************************************************************
 * MaterialParameterData
 *****************************************************************/

MaterialParameterData::MaterialParameterData(StringID bindingName,
                                             const ShaderResourceBinding &binding,
                                             ResourceType resType)
    : m_bindingName(bindingName), m_binding(&binding),
      m_buffer(binding.size, uint8_t{0}), m_resType(resType) {}

void MaterialParameterData::writeMember(StringID memberName, const void *src,
                                        size_t nbytes,
                                        ShaderPropertyType expected) {
  if (!m_binding) {
    assert(false && "MaterialParameterData has no binding");
    return;
  }
  for (const auto &member : m_binding->members) {
    if (StringID(member.name) != memberName)
      continue;
    assert(member.type == expected &&
           "MaterialInstance setter type does not match reflected member type");
    assert(static_cast<size_t>(member.offset) + nbytes <= m_buffer.size() &&
           "UBO write would overflow the reflected buffer");
    std::memcpy(m_buffer.data() + member.offset, src, nbytes);
    m_dirty = true;
    return;
  }
  assert(false && "MaterialInstance setter: member not found in parameter data");
}

/*****************************************************************
 * MaterialInstance
 *****************************************************************/

MaterialInstance::MaterialInstance(Token, MaterialTemplate::Ptr tmpl)
    : m_template(std::move(tmpl)) {
  if (!m_template) {
    return;
  }

  for (const auto &[pass, _] : m_template->getPasses()) {
    m_enabledPasses.insert(pass);
  }

  // Collect material-owned buffer bindings across enabled passes.
  // Use the per-pass material interface from the template (REQ-032).
  std::unordered_map<std::string, const ShaderResourceBinding *> seenBuffers;

  for (const auto &pass : getEnabledPasses()) {
    for (const auto &binding : m_template->getMaterialBindings(pass)) {
      // Fail fast on unsupported descriptor types.
      if (!isSupportedMaterialDescriptorType(binding.type)) {
        fatalUnsupportedDescriptorType(binding.name, binding.type);
      }

      if (!isBufferType(binding.type))
        continue;

      auto it = seenBuffers.find(binding.name);
      if (it != seenBuffers.end()) {
        // Cross-pass consistency check.
        const auto *first = it->second;
        assert(first->type == binding.type &&
               first->size == binding.size &&
               first->members == binding.members &&
               "MaterialInstance: cross-pass buffer binding layout mismatch");
        continue;
      }
      seenBuffers[binding.name] = &binding;
    }
  }

  m_bufferSlots.reserve(seenBuffers.size());
  for (const auto &[name, binding] : seenBuffers) {
    m_bufferSlots.push_back(std::make_shared<MaterialParameterData>(
        StringID(name), *binding, toResourceType(binding->type)));
  }
}

/*****************************************************************
 * Buffer slot lookup helpers
 *****************************************************************/

MaterialParameterData *MaterialInstance::findSlot(StringID bindingName) {
  for (auto &slot : m_bufferSlots) {
    if (slot && slot->getBindingName() == bindingName)
      return slot.get();
  }
  return nullptr;
}

const MaterialParameterData *
MaterialInstance::findSlot(StringID bindingName) const {
  for (const auto &slot : m_bufferSlots) {
    if (slot && slot->getBindingName() == bindingName)
      return slot.get();
  }
  return nullptr;
}

MaterialParameterData *MaterialInstance::findSlotByMember(StringID memberName) {
  MaterialParameterData *found = nullptr;
  for (auto &slot : m_bufferSlots) {
    if (!slot || !slot->getBinding())
      continue;
    for (const auto &m : slot->getBinding()->members) {
      if (StringID(m.name) == memberName) {
        assert(!found &&
               "Ambiguous member name across multiple buffer slots; "
               "use setParameter(bindingName, memberName, value) instead");
        found = slot.get();
        break;
      }
    }
  }
  return found;
}

/*****************************************************************
 * setParameter (primary API)
 *****************************************************************/

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    float value) {
  auto *slot = findSlot(bindingName);
  assert(slot && "setParameter: binding name not found in buffer slots");
  if (slot)
    slot->writeMember(memberName, &value, sizeof(float),
                      ShaderPropertyType::Float);
}

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    int32_t value) {
  auto *slot = findSlot(bindingName);
  assert(slot && "setParameter: binding name not found in buffer slots");
  if (slot)
    slot->writeMember(memberName, &value, sizeof(int32_t),
                      ShaderPropertyType::Int);
}

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    const Vec3f &value) {
  auto *slot = findSlot(bindingName);
  assert(slot && "setParameter: binding name not found in buffer slots");
  if (slot)
    slot->writeMember(memberName, &value, sizeof(float) * 3,
                      ShaderPropertyType::Vec3);
}

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    const Vec4f &value) {
  auto *slot = findSlot(bindingName);
  assert(slot && "setParameter: binding name not found in buffer slots");
  if (slot)
    slot->writeMember(memberName, &value, sizeof(Vec4f),
                      ShaderPropertyType::Vec4);
}

/*****************************************************************
 * Legacy convenience setters
 *****************************************************************/

void MaterialInstance::setVec4(StringID id, const Vec4f &value) {
  auto *slot = findSlotByMember(id);
  assert(slot && "setVec4: member not found in any buffer slot");
  if (slot)
    slot->writeMember(id, &value, sizeof(Vec4f), ShaderPropertyType::Vec4);
}

void MaterialInstance::setVec3(StringID id, const Vec3f &value) {
  auto *slot = findSlotByMember(id);
  assert(slot && "setVec3: member not found in any buffer slot");
  if (slot)
    slot->writeMember(id, &value, sizeof(float) * 3,
                      ShaderPropertyType::Vec3);
}

void MaterialInstance::setFloat(StringID id, float value) {
  auto *slot = findSlotByMember(id);
  assert(slot && "setFloat: member not found in any buffer slot");
  if (slot)
    slot->writeMember(id, &value, sizeof(float), ShaderPropertyType::Float);
}

void MaterialInstance::setInt(StringID id, int32_t value) {
  auto *slot = findSlotByMember(id);
  assert(slot && "setInt: member not found in any buffer slot");
  if (slot)
    slot->writeMember(id, &value, sizeof(int32_t), ShaderPropertyType::Int);
}

void MaterialInstance::setTexture(StringID id, CombinedTextureSamplerPtr tex) {
  auto bindingOpt = m_template->findMaterialBinding(id);
  assert(bindingOpt && "texture binding not found in material-owned bindings");
  const auto type = bindingOpt->get().type;
  assert((type == ShaderPropertyType::Texture2D ||
          type == ShaderPropertyType::TextureCube) &&
         "setTexture target is not a sampled image binding");
  (void)type;
  m_textures[id] = std::move(tex);
}

/*****************************************************************
 * GPU sync
 *****************************************************************/

void MaterialInstance::syncGpuData() {
  for (auto &slot : m_bufferSlots) {
    if (slot && slot->hasPendingSync()) {
      slot->setDirty();
      slot->clearPendingSync();
    }
  }
}

/*****************************************************************
 * Pass-aware descriptor resources (REQ-032 R4)
 *****************************************************************/

std::vector<IGpuResourcePtr>
MaterialInstance::getDescriptorResources(StringID pass) const {
  std::vector<std::pair<uint32_t, IGpuResourcePtr>> sorted;

  const auto &matBindings = m_template->getMaterialBindings(pass);
  for (const auto &binding : matBindings) {
    const uint32_t key = (binding.set << 16) | binding.binding;
    const StringID bindingId(binding.name);

    if (isBufferType(binding.type)) {
      for (const auto &slot : m_bufferSlots) {
        if (slot && slot->getBindingName() == bindingId &&
            !slot->getBuffer().empty()) {
          sorted.emplace_back(key, std::static_pointer_cast<IGpuResource>(slot));
          break;
        }
      }
    } else if (binding.type == ShaderPropertyType::Texture2D ||
               binding.type == ShaderPropertyType::TextureCube) {
      CombinedTextureSamplerPtr tex;
      auto it = m_textures.find(bindingId);
      if (it != m_textures.end())
        tex = it->second;
      if (tex) {
        tex->setBindingName(bindingId);
        sorted.emplace_back(key, std::static_pointer_cast<IGpuResource>(tex));
      }
    }
  }

  std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });

  std::vector<IGpuResourcePtr> out;
  out.reserve(sorted.size());
  for (auto &[_, r] : sorted) {
    out.push_back(std::move(r));
  }
  return out;
}

/*****************************************************************
 * Accessors
 *****************************************************************/

const std::vector<uint8_t> &
MaterialInstance::getParameterBuffer(StringID bindingName) const {
  if (const auto *slot = findSlot(bindingName))
    return slot->getBuffer();
  return kEmptyBuffer;
}

const ShaderResourceBinding *
MaterialInstance::getParameterBinding(StringID bindingName) const {
  if (const auto *slot = findSlot(bindingName))
    return slot->getBinding();
  return nullptr;
}

const std::vector<uint8_t> &MaterialInstance::getParameterBuffer() const {
  assert(m_bufferSlots.size() <= 1 &&
         "getParameterBuffer(): multiple buffer slots; use "
         "getParameterBuffer(bindingName) instead");
  if (m_bufferSlots.empty()) {
    static const std::vector<uint8_t> kEmpty;
    return kEmpty;
  }
  return m_bufferSlots[0]->getBuffer();
}

const ShaderResourceBinding *MaterialInstance::getParameterBinding() const {
  assert(m_bufferSlots.size() <= 1 &&
         "getParameterBinding(): multiple buffer slots; use "
         "getParameterBinding(bindingName) instead");
  return m_bufferSlots.empty() ? nullptr : m_bufferSlots[0]->getBinding();
}

IShaderPtr MaterialInstance::getShaderInfo(StringID pass) const {
  if (!m_template)
    return nullptr;
  auto entry = m_template->getEntry(pass);
  if (!entry)
    return nullptr;
  return entry->get().shaderSet.getShader();
}

RenderState MaterialInstance::getRenderState(StringID pass) const {
  if (!m_template)
    return RenderState{};
  auto entry = m_template->getEntry(pass);
  return entry ? entry->get().renderState : RenderState{};
}

StringID MaterialInstance::getRenderSignature(StringID pass) const {
  if (!m_template)
    return StringID{};
  StringID passSig = m_template->getRenderPassSignature(pass);
  StringID fields[] = {passSig};
  return GlobalStringTable::get().compose(TypeTag::MaterialRender, fields);
}

bool MaterialInstance::isPassEnabled(StringID pass) const {
  return hasDefinedPass(pass) &&
         m_enabledPasses.find(pass) != m_enabledPasses.end();
}

void MaterialInstance::setPassEnabled(StringID pass, bool enabled) {
  if (!m_template || !hasDefinedPass(pass)) {
    fatalUndefinedPass(m_template.get(), pass);
  }

  const bool currentlyEnabled = isPassEnabled(pass);
  if (enabled == currentlyEnabled)
    return;

  if (enabled) {
    m_enabledPasses.insert(pass);
  } else {
    m_enabledPasses.erase(pass);
  }

  for (const auto &[_, callback] : m_passStateListeners) {
    if (callback)
      callback();
  }
}

std::vector<StringID> MaterialInstance::getEnabledPasses() const {
  std::vector<StringID> out;
  out.reserve(m_enabledPasses.size());
  for (const auto &pass : m_enabledPasses)
    out.push_back(pass);
  return out;
}

uint64_t
MaterialInstance::addPassStateListener(std::function<void()> callback) {
  const uint64_t id = m_nextListenerId++;
  m_passStateListeners.emplace(id, std::move(callback));
  return id;
}

void MaterialInstance::removePassStateListener(uint64_t listenerId) {
  m_passStateListeners.erase(listenerId);
}

bool MaterialInstance::hasDefinedPass(StringID pass) const {
  return m_template && m_template->getEntry(pass).has_value();
}

} // namespace LX_core
