#include "material_instance.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
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

const std::vector<u8> kEmptyBuffer;

[[noreturn]] void fatalUndefinedPass(const std::string &templateName,
                                     StringID pass) {
  throw std::logic_error("MaterialInstance template=" + templateName +
                         " pass=" +
                         GlobalStringTable::get().toDebugString(pass) +
                         " reason=setPassEnabled called for undefined pass");
}

} // namespace

/*****************************************************************
 * MaterialInstance
 *****************************************************************/

MaterialInstance::MaterialInstance(Token, MaterialTemplateSharedPtr tmpl)
    : m_template(std::move(tmpl)) {
  if (!m_template) {
    return;
  }

  for (const auto &[pass, _] : m_template->getAllPassDefinitions()) {
    m_enabledPasses.insert(pass);
  }

  m_parameterBuffersByName.reserve(
      m_template->getCanonicalMaterialBindings().size());
  for (const auto &[bindingId, binding] :
       m_template->getCanonicalMaterialBindings()) {
    if (!isBufferType(binding.type))
      continue;

    m_parameterBuffersByName.emplace(bindingId,
                                     std::make_shared<ParameterBuffer>(
                                         bindingId, binding,
                                         toResourceType(binding.type)));
  }
}

/*****************************************************************
 * Buffer-binding lookup helpers
 *****************************************************************/

std::optional<std::reference_wrapper<ParameterBuffer>>
MaterialInstance::findParameterBuffer(StringID bindingName) {
  auto it = m_parameterBuffersByName.find(bindingName);
  if (it == m_parameterBuffersByName.end())
    return std::nullopt;
  return std::ref(*it->second);
}

std::optional<std::reference_wrapper<const ParameterBuffer>>
MaterialInstance::findParameterBuffer(StringID bindingName) const {
  auto it = m_parameterBuffersByName.find(bindingName);
  if (it == m_parameterBuffersByName.end())
    return std::nullopt;
  return std::cref(*it->second);
}

/*****************************************************************
 * setParameter (primary API)
 *****************************************************************/

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    float value) {
  auto parameterBuffer = findParameterBuffer(bindingName);
  assert(parameterBuffer &&
         "setParameter: binding name not found in canonical buffer bindings");
  if (parameterBuffer)
    parameterBuffer->get().writeBindingMember(memberName, &value, sizeof(float),
                                              ShaderPropertyType::Float);
}

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    i32 value) {
  auto parameterBuffer = findParameterBuffer(bindingName);
  assert(parameterBuffer &&
         "setParameter: binding name not found in canonical buffer bindings");
  if (parameterBuffer)
    parameterBuffer->get().writeBindingMember(memberName, &value, sizeof(i32),
                                              ShaderPropertyType::Int);
}

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    const Vec3f &value) {
  auto parameterBuffer = findParameterBuffer(bindingName);
  assert(parameterBuffer &&
         "setParameter: binding name not found in canonical buffer bindings");
  if (parameterBuffer)
    parameterBuffer->get().writeBindingMember(memberName, &value,
                                              sizeof(float) * 3,
                                              ShaderPropertyType::Vec3);
}

void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    const Vec4f &value) {
  auto parameterBuffer = findParameterBuffer(bindingName);
  assert(parameterBuffer &&
         "setParameter: binding name not found in canonical buffer bindings");
  if (parameterBuffer)
    parameterBuffer->get().writeBindingMember(memberName, &value,
                                              sizeof(Vec4f),
                                              ShaderPropertyType::Vec4);
}

void MaterialInstance::setTexture(StringID bindingName,
                                  CombinedTextureSamplerSharedPtr tex) {
  auto bindingOpt = m_template->findCanonicalMaterialBinding(bindingName);
  assert(bindingOpt &&
         "texture binding not found in canonical material interface");
  const auto type = bindingOpt->get().type;
  assert((type == ShaderPropertyType::Texture2D ||
          type == ShaderPropertyType::TextureCube) &&
         "setTexture target is not a sampled image binding");
  (void)type;
  m_textureBindingsByName[bindingName] = std::move(tex);
}

/*****************************************************************
 * GPU sync
 *****************************************************************/

void MaterialInstance::syncGpuData() {
  for (auto &[_, parameterBuffer] : m_parameterBuffersByName) {
    if (parameterBuffer && parameterBuffer->hasPendingSync()) {
      parameterBuffer->setDirty();
      parameterBuffer->clearPendingSync();
    }
  }
}

/*****************************************************************
 * Pass-aware descriptor resources (REQ-032 R4)
 *****************************************************************/

std::vector<IGpuResourceSharedPtr>
MaterialInstance::getDescriptorResources(StringID pass) const {
  std::vector<std::pair<u32, IGpuResourceSharedPtr>> sorted;

  const auto &bindingIds = m_template->getPassMaterialBindingIds(pass);
  for (const auto &bindingId : bindingIds) {
    auto binding = m_template->findCanonicalMaterialBinding(bindingId);
    if (!binding)
      continue;
    const auto &bindingRef = binding->get();
    const u32 lookupKey =
        (bindingRef.set << 16) | bindingRef.binding;
    const u32 key = lookupKey;

    if (isBufferType(bindingRef.type)) {
      auto it = m_parameterBuffersByName.find(bindingId);
      if (it != m_parameterBuffersByName.end() && it->second &&
          !it->second->getBuffer().empty()) {
        sorted.emplace_back(
            key, std::static_pointer_cast<IGpuResource>(it->second));
      }
    } else if (bindingRef.type == ShaderPropertyType::Texture2D ||
               bindingRef.type == ShaderPropertyType::TextureCube) {
      CombinedTextureSamplerSharedPtr tex;
      auto it = m_textureBindingsByName.find(bindingId);
      if (it != m_textureBindingsByName.end())
        tex = it->second;
      if (tex) {
        tex->setBindingName(bindingId);
        sorted.emplace_back(key, std::static_pointer_cast<IGpuResource>(tex));
      }
    }
  }

  std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });

  std::vector<IGpuResourceSharedPtr> out;
  out.reserve(sorted.size());
  for (auto &[_, r] : sorted) {
    out.push_back(std::move(r));
  }
  return out;
}

/*****************************************************************
 * Accessors
 *****************************************************************/

const std::vector<u8> &
MaterialInstance::getParameterBufferBytes(StringID bindingName) const {
  if (auto parameterBuffer = findParameterBuffer(bindingName))
    return parameterBuffer->get().getBuffer();
  return kEmptyBuffer;
}

std::optional<std::reference_wrapper<const ShaderResourceBinding>>
MaterialInstance::getParameterBufferLayout(StringID bindingName) const {
  if (auto parameterBuffer = findParameterBuffer(bindingName))
    return std::cref(parameterBuffer->get().getBinding());
  return std::nullopt;
}

const std::vector<u8> &MaterialInstance::getParameterBufferBytes() const {
  assert(m_parameterBuffersByName.size() <= 1 &&
         "getParameterBufferBytes(): multiple parameter buffers; use "
         "getParameterBufferBytes(bindingName) instead");
  if (m_parameterBuffersByName.empty()) {
    static const std::vector<u8> kEmpty;
    return kEmpty;
  }
  return m_parameterBuffersByName.begin()->second->getBuffer();
}

std::optional<std::reference_wrapper<const ShaderResourceBinding>>
MaterialInstance::getParameterBufferLayout() const {
  assert(m_parameterBuffersByName.size() <= 1 &&
         "getParameterBufferLayout(): multiple parameter buffers; use "
         "getParameterBufferLayout(bindingName) instead");
  if (m_parameterBuffersByName.empty())
    return std::nullopt;
  return std::cref(m_parameterBuffersByName.begin()->second->getBinding());
}

IShaderSharedPtr MaterialInstance::getPassShader(StringID pass) const {
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

StringID MaterialInstance::getPipelineSignature(StringID pass) const {
  if (!m_template)
    return StringID{};
  StringID passSig = m_template->getPipelineSignature(pass);
  StringID fields[] = {passSig};
  return GlobalStringTable::get().compose(TypeTag::MaterialRender, fields);
}

bool MaterialInstance::isPassEnabled(StringID pass) const {
  return hasDefinedPass(pass) &&
         m_enabledPasses.find(pass) != m_enabledPasses.end();
}

void MaterialInstance::setPassEnabled(StringID pass, bool enabled) {
  if (!m_template || !hasDefinedPass(pass)) {
    fatalUndefinedPass(m_template ? m_template->getName() : std::string("<null>"),
                       pass);
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

u64
MaterialInstance::addPassStateListener(std::function<void()> callback) {
  const u64 id = m_nextListenerId++;
  m_passStateListeners.emplace(id, std::move(callback));
  return id;
}

void MaterialInstance::removePassStateListener(u64 listenerId) {
  m_passStateListeners.erase(listenerId);
}

bool MaterialInstance::hasDefinedPass(StringID pass) const {
  return m_template && m_template->getPassDefinition(pass).has_value();
}

} // namespace LX_core
