#include "material_instance.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <utility>

namespace LX_core {

namespace {

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

} // namespace

/*****************************************************************
 * MaterialParameterData
 *****************************************************************/

MaterialParameterData::MaterialParameterData(StringID bindingName,
                                             const ShaderResourceBinding &binding,
                                             ResourceType resType)
    : m_bindingName(bindingName), m_binding(binding),
      m_buffer(binding.size, uint8_t{0}), m_resType(resType) {}

void MaterialParameterData::writeParameterMember(StringID memberName,
                                                 const void *src,
                                                 size_t nbytes,
                                                 ShaderPropertyType expected) {
  for (const auto &member : m_binding.get().members) {
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
  assert(false &&
         "MaterialInstance setter: member not found in parameter data");
}

/*****************************************************************
 * MaterialInstance
 *****************************************************************/

MaterialInstance::MaterialInstance(Token, MaterialTemplate::Ptr tmpl)
    : m_template(std::move(tmpl)) {
  if (!m_template) {
    return;
  }

  for (const auto &[pass, _] : m_template->getAllPassDefinitions()) {
    m_enabledPasses.insert(pass);
  }

  m_parameterDataByBinding.reserve(
      m_template->getCanonicalMaterialBindings().size());
  for (const auto &[bindingId, binding] :
       m_template->getCanonicalMaterialBindings()) {
    if (!isBufferType(binding.type))
      continue;

    m_parameterDataByBinding.emplace(
        bindingId,
        std::make_shared<MaterialParameterData>(bindingId, binding,
                                                toResourceType(binding.type)));
  }
}

/*****************************************************************
 * Parameter-data lookup helpers
 *****************************************************************/

MaterialParameterData *
MaterialInstance::findParameterDataByBinding(StringID bindingName) {
  auto it = m_parameterDataByBinding.find(bindingName);
  return it != m_parameterDataByBinding.end() ? it->second.get() : nullptr;
}

const MaterialParameterData *
MaterialInstance::findParameterDataByBinding(StringID bindingName) const {
  auto it = m_parameterDataByBinding.find(bindingName);
  return it != m_parameterDataByBinding.end() ? it->second.get() : nullptr;
}

/*****************************************************************
 * setParameter (primary API)
 *****************************************************************/

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    float value) {
  auto *parameterData = findParameterDataByBinding(bindingName);
  assert(parameterData &&
         "setParameter: binding name not found in canonical parameter data");
  if (parameterData)
    parameterData->writeParameterMember(memberName, &value, sizeof(float),
                                        ShaderPropertyType::Float);
}

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    int32_t value) {
  auto *parameterData = findParameterDataByBinding(bindingName);
  assert(parameterData &&
         "setParameter: binding name not found in canonical parameter data");
  if (parameterData)
    parameterData->writeParameterMember(memberName, &value, sizeof(int32_t),
                                        ShaderPropertyType::Int);
}

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    const Vec3f &value) {
  auto *parameterData = findParameterDataByBinding(bindingName);
  assert(parameterData &&
         "setParameter: binding name not found in canonical parameter data");
  if (parameterData)
    parameterData->writeParameterMember(memberName, &value, sizeof(float) * 3,
                                        ShaderPropertyType::Vec3);
}

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    const Vec4f &value) {
  auto *parameterData = findParameterDataByBinding(bindingName);
  assert(parameterData &&
         "setParameter: binding name not found in canonical parameter data");
  if (parameterData)
    parameterData->writeParameterMember(memberName, &value, sizeof(Vec4f),
                                        ShaderPropertyType::Vec4);
}

void MaterialInstance::setTexture(StringID bindingName,
                                  CombinedTextureSamplerPtr tex) {
  auto bindingOpt = m_template->findCanonicalMaterialBinding(bindingName);
  assert(bindingOpt &&
         "texture binding not found in canonical material interface");
  const auto type = bindingOpt->get().type;
  assert((type == ShaderPropertyType::Texture2D ||
          type == ShaderPropertyType::TextureCube) &&
         "setTexture target is not a sampled image binding");
  (void)type;
  m_textureBindings[bindingName] = std::move(tex);
}

/*****************************************************************
 * GPU sync
 *****************************************************************/

void MaterialInstance::syncGpuData() {
  for (auto &[_, parameterData] : m_parameterDataByBinding) {
    if (parameterData && parameterData->hasPendingSync()) {
      parameterData->setDirty();
      parameterData->clearPendingSync();
    }
  }
}

/*****************************************************************
 * Pass-aware descriptor resources (REQ-032 R4)
 *****************************************************************/

std::vector<IGpuResourcePtr>
MaterialInstance::getDescriptorResources(StringID pass) const {
  std::vector<std::pair<uint32_t, IGpuResourcePtr>> sorted;

  const auto &bindingIds = m_template->getPassMaterialBindingIds(pass);
  for (const auto &bindingId : bindingIds) {
    const auto *binding = m_template->getCanonicalMaterialBinding(bindingId);
    if (!binding)
      continue;
    const uint32_t key = (binding->set << 16) | binding->binding;

    if (isBufferType(binding->type)) {
      auto it = m_parameterDataByBinding.find(bindingId);
      if (it != m_parameterDataByBinding.end() && it->second &&
          !it->second->getBuffer().empty()) {
        sorted.emplace_back(
            key, std::static_pointer_cast<IGpuResource>(it->second));
      }
    } else if (binding->type == ShaderPropertyType::Texture2D ||
               binding->type == ShaderPropertyType::TextureCube) {
      CombinedTextureSamplerPtr tex;
      auto it = m_textureBindings.find(bindingId);
      if (it != m_textureBindings.end())
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
  if (const auto *parameterData = findParameterDataByBinding(bindingName))
    return parameterData->getBuffer();
  return kEmptyBuffer;
}

const ShaderResourceBinding *
MaterialInstance::getParameterBinding(StringID bindingName) const {
  if (const auto *parameterData = findParameterDataByBinding(bindingName))
    return &parameterData->getBinding();
  return nullptr;
}

const std::vector<uint8_t> &MaterialInstance::getParameterBuffer() const {
  assert(m_parameterDataByBinding.size() <= 1 &&
         "getParameterBuffer(): multiple parameter bindings; use "
         "getParameterBuffer(bindingName) instead");
  if (m_parameterDataByBinding.empty()) {
    static const std::vector<uint8_t> kEmpty;
    return kEmpty;
  }
  return m_parameterDataByBinding.begin()->second->getBuffer();
}

const ShaderResourceBinding *MaterialInstance::getParameterBinding() const {
  assert(m_parameterDataByBinding.size() <= 1 &&
         "getParameterBinding(): multiple parameter bindings; use "
         "getParameterBinding(bindingName) instead");
  return m_parameterDataByBinding.empty()
             ? nullptr
             : &m_parameterDataByBinding.begin()->second->getBinding();
}

IShaderPtr MaterialInstance::getPassShader(StringID pass) const {
  if (!m_template)
    return nullptr;
  auto passDefinition = m_template->getPassDefinition(pass);
  if (!passDefinition)
    return nullptr;
  return passDefinition->get().shaderProgram.getShader();
}

RenderState MaterialInstance::getPassRenderState(StringID pass) const {
  if (!m_template)
    return RenderState{};
  auto passDefinition = m_template->getPassDefinition(pass);
  return passDefinition ? passDefinition->get().renderState : RenderState{};
}

StringID MaterialInstance::getMaterialSignature(StringID pass) const {
  if (!m_template)
    return StringID{};
  StringID passSig = m_template->getPassDefinitionSignature(pass);
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
  return m_template && m_template->getPassDefinition(pass).has_value();
}

} // namespace LX_core
