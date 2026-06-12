#include "material_instance.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include <algorithm>
#include <cstring>
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
  throw std::logic_error(
      "MaterialInstance template=" + templateName +
      " pass=" + GlobalStringTable::get().toDebugString(pass) +
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

    m_parameterBuffersByName.emplace(
        bindingId, std::make_unique<ParameterBuffer>(
                       bindingId, binding, toResourceType(binding.type)));
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
 * writeShaderBindingParameter (primary API)
 *****************************************************************/

void MaterialInstance::writeShaderBindingParameter(StringID bindingName,
                                                   StringID memberName,
                                                   float value) {
  if (m_usesEnvelopeStorage) {
    return;
  }
  auto parameterBuffer = findParameterBuffer(bindingName);
  assert(parameterBuffer && "writeShaderBindingParameter: binding name not "
                            "found in canonical buffer bindings");
  if (parameterBuffer) {
    parameterBuffer->get().writeBindingMember(memberName, &value, sizeof(float),
                                              ShaderPropertyType::Float);
    markMaterialStateDirty();
  }
}

void MaterialInstance::writeShaderBindingParameter(StringID bindingName,
                                                   StringID memberName,
                                                   i32 value) {
  if (m_usesEnvelopeStorage) {
    return;
  }
  auto parameterBuffer = findParameterBuffer(bindingName);
  assert(parameterBuffer && "writeShaderBindingParameter: binding name not "
                            "found in canonical buffer bindings");
  if (parameterBuffer) {
    parameterBuffer->get().writeBindingMember(memberName, &value, sizeof(i32),
                                              ShaderPropertyType::Int);
    markMaterialStateDirty();
  }
}

void MaterialInstance::writeShaderBindingParameter(StringID bindingName,
                                                   StringID memberName,
                                                   const Vec3f &value) {
  if (m_usesEnvelopeStorage) {
    return;
  }
  auto parameterBuffer = findParameterBuffer(bindingName);
  assert(parameterBuffer && "writeShaderBindingParameter: binding name not "
                            "found in canonical buffer bindings");
  if (parameterBuffer) {
    parameterBuffer->get().writeBindingMember(
        memberName, &value, sizeof(float) * 3, ShaderPropertyType::Vec3);
    markMaterialStateDirty();
  }
}

void MaterialInstance::writeShaderBindingParameter(StringID bindingName,
                                                   StringID memberName,
                                                   const Vec4f &value) {
  if (m_usesEnvelopeStorage) {
    return;
  }
  auto parameterBuffer = findParameterBuffer(bindingName);
  assert(parameterBuffer && "writeShaderBindingParameter: binding name not "
                            "found in canonical buffer bindings");
  if (parameterBuffer) {
    parameterBuffer->get().writeBindingMember(memberName, &value, sizeof(Vec4f),
                                              ShaderPropertyType::Vec4);
    markMaterialStateDirty();
  }
}

void MaterialInstance::writeShaderBindingParameterValue(
    StringID bindingName, StringID memberName,
    const MaterialParameterValue &value) {
  switch (value.type) {
  case MaterialParameterValueType::Float:
    writeShaderBindingParameter(bindingName, memberName, value.floatValue);
    return;
  case MaterialParameterValueType::Int:
    writeShaderBindingParameter(bindingName, memberName, value.intValue);
    return;
  case MaterialParameterValueType::Vec3:
    writeShaderBindingParameter(
        bindingName, memberName,
        Vec3f{value.vectorValue.x, value.vectorValue.y, value.vectorValue.z});
    return;
  case MaterialParameterValueType::Vec4:
    writeShaderBindingParameter(bindingName, memberName, value.vectorValue);
    return;
  }
}

std::optional<std::reference_wrapper<const StructMemberInfo>>
MaterialInstance::findShaderBindingParameterMember(StringID bindingName,
                                                   StringID memberName) const {
  if (m_usesEnvelopeStorage) {
    return std::nullopt;
  }
  const auto binding = getShaderBindingBufferLayout(bindingName);
  if (!binding.has_value()) {
    return std::nullopt;
  }
  for (const auto &member : binding->get().members) {
    if (StringID(member.name) == memberName) {
      return std::cref(member);
    }
  }
  return std::nullopt;
}

std::optional<MaterialParameterValue>
MaterialInstance::readShaderBindingParameterValue(StringID bindingName,
                                                  StringID memberName) const {
  if (m_usesEnvelopeStorage) {
    return std::nullopt;
  }
  const auto member = findShaderBindingParameterMember(bindingName, memberName);
  if (!member.has_value()) {
    return std::nullopt;
  }
  const auto &bytes = getShaderBindingBufferBytes(bindingName);
  if (static_cast<usize>(member->get().offset) + member->get().size >
      bytes.size()) {
    return std::nullopt;
  }

  MaterialParameterValue value;
  switch (member->get().type) {
  case ShaderPropertyType::Float:
    value.type = MaterialParameterValueType::Float;
    std::memcpy(&value.floatValue, bytes.data() + member->get().offset,
                sizeof(float));
    return value;
  case ShaderPropertyType::Int:
    value.type = MaterialParameterValueType::Int;
    std::memcpy(&value.intValue, bytes.data() + member->get().offset,
                sizeof(i32));
    return value;
  case ShaderPropertyType::Vec3:
    value.type = MaterialParameterValueType::Vec3;
    std::memcpy(&value.vectorValue, bytes.data() + member->get().offset,
                sizeof(float) * 3);
    return value;
  case ShaderPropertyType::Vec4:
    value.type = MaterialParameterValueType::Vec4;
    std::memcpy(&value.vectorValue, bytes.data() + member->get().offset,
                sizeof(Vec4f));
    return value;
  default:
    return std::nullopt;
  }
}

void MaterialInstance::setTexture(StringID bindingName,
                                  CombinedTextureSamplerSharedPtr tex) {
  auto bindingOpt = m_template->findCanonicalMaterialBinding(bindingName);
  if (bindingOpt) {
    const auto type = bindingOpt->get().type;
    assert((type == ShaderPropertyType::Texture2D ||
            type == ShaderPropertyType::TextureCube) &&
           "setTexture target is not a sampled image binding");
    (void)type;
  } else {
    const auto envelope = getMaterialEnvelope(bindingName);
    assert(envelope.has_value() &&
           envelope->get().kind == MaterialEnvelopeKind::Texture &&
           "texture target is neither a shader binding nor texture envelope");
  }
  m_pendingTextureBindingsByName[bindingName] = std::move(tex);
  m_textureHandlesByName.erase(bindingName);
  markMaterialStateDirty();
}

void MaterialInstance::setTextureHandle(StringID bindingName,
                                        TextureHandle handle) {
  auto bindingOpt = m_template->findCanonicalMaterialBinding(bindingName);
  if (bindingOpt) {
    const auto type = bindingOpt->get().type;
    assert((type == ShaderPropertyType::Texture2D ||
            type == ShaderPropertyType::TextureCube) &&
           "setTextureHandle target is not a sampled image binding");
    (void)type;
  } else {
    const auto envelope = getMaterialEnvelope(bindingName);
    assert(envelope.has_value() &&
           envelope->get().kind == MaterialEnvelopeKind::Texture &&
           "texture handle target is neither a shader binding nor texture "
           "envelope");
  }
  m_textureHandlesByName[bindingName] = handle;
  m_pendingTextureBindingsByName.erase(bindingName);
  markMaterialStateDirty();
}

TextureHandle MaterialInstance::getTextureHandle(StringID bindingName) const {
  const auto it = m_textureHandlesByName.find(bindingName);
  if (it == m_textureHandlesByName.end()) {
    return TextureHandle{};
  }
  return it->second;
}

CombinedTextureSamplerSharedPtr
MaterialInstance::getTexture(StringID bindingName) const {
  const auto it = m_pendingTextureBindingsByName.find(bindingName);
  if (it == m_pendingTextureBindingsByName.end()) {
    return nullptr;
  }
  return it->second;
}

void MaterialInstance::forEachPendingTextureBinding(
    const std::function<void(StringID, const CombinedTextureSamplerSharedPtr &)>
        &callback) const {
  std::vector<std::pair<StringID, CombinedTextureSamplerSharedPtr>> bindings;
  bindings.reserve(m_pendingTextureBindingsByName.size());
  for (const auto &[bindingName, texture] : m_pendingTextureBindingsByName) {
    bindings.emplace_back(bindingName, texture);
  }
  for (const auto &[bindingName, texture] : bindings) {
    callback(bindingName, texture);
  }
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

MaterialInstance::SharedPtr MaterialInstance::cloneInstanceData() const {
  auto clone = MaterialInstance::create(m_template);
  auto uniqueClone = cloneInstanceDataUnique();
  clone->m_pendingTextureBindingsByName =
      uniqueClone->m_pendingTextureBindingsByName;
  clone->m_textureHandlesByName = uniqueClone->m_textureHandlesByName;
  clone->m_enabledPasses = uniqueClone->m_enabledPasses;
  clone->m_bsdfType = uniqueClone->m_bsdfType;
  clone->m_materialSourceUri = uniqueClone->m_materialSourceUri;
  clone->m_materialSourceSignature = uniqueClone->m_materialSourceSignature;
  clone->m_materialSourceReflectionHash =
      uniqueClone->m_materialSourceReflectionHash;
  clone->m_materialContractReflection =
      uniqueClone->m_materialContractReflection;
  clone->m_renderClass = uniqueClone->m_renderClass;
  clone->m_tags = uniqueClone->m_tags;
  clone->m_authoringMetadata = uniqueClone->m_authoringMetadata;
  clone->m_materialEnvelopesByName = uniqueClone->m_materialEnvelopesByName;
  clone->m_materialDependencies = uniqueClone->m_materialDependencies;
  clone->m_materialStateVersion = uniqueClone->m_materialStateVersion;
  clone->m_materialStateDirty = uniqueClone->m_materialStateDirty;
  clone->m_usesEnvelopeStorage = uniqueClone->m_usesEnvelopeStorage;
  if (clone->m_usesEnvelopeStorage) {
    clone->m_parameterBuffersByName.clear();
  }
  for (const auto &[bindingId, parameterBuffer] :
       uniqueClone->m_parameterBuffersByName) {
    if (!parameterBuffer) {
      continue;
    }
    const auto &binding = parameterBuffer->getBinding();
    for (const auto &member : binding.members) {
      const auto value = uniqueClone->readShaderBindingParameterValue(
          bindingId, StringID(member.name));
      if (value.has_value()) {
        clone->writeShaderBindingParameterValue(bindingId,
                                                StringID(member.name), *value);
      }
    }
  }
  clone->syncGpuData();
  clone->m_materialStateVersion = uniqueClone->m_materialStateVersion;
  clone->m_materialStateDirty = uniqueClone->m_materialStateDirty;
  return clone;
}

MaterialInstance::UniquePtr MaterialInstance::cloneInstanceDataUnique() const {
  auto clone = MaterialInstance::createUnique(m_template);
  clone->m_pendingTextureBindingsByName = m_pendingTextureBindingsByName;
  clone->m_textureHandlesByName = m_textureHandlesByName;
  clone->m_enabledPasses = m_enabledPasses;
  clone->m_bsdfType = m_bsdfType;
  clone->m_materialSourceUri = m_materialSourceUri;
  clone->m_materialSourceSignature = m_materialSourceSignature;
  clone->m_materialSourceReflectionHash = m_materialSourceReflectionHash;
  clone->m_materialContractReflection = m_materialContractReflection;
  clone->m_renderClass = m_renderClass;
  clone->m_tags = m_tags;
  clone->m_authoringMetadata = m_authoringMetadata;
  clone->m_materialEnvelopesByName = m_materialEnvelopesByName;
  clone->m_materialDependencies = m_materialDependencies;
  clone->m_materialStateVersion = m_materialStateVersion;
  clone->m_materialStateDirty = m_materialStateDirty;
  clone->m_usesEnvelopeStorage = m_usesEnvelopeStorage;
  if (clone->m_usesEnvelopeStorage) {
    clone->m_parameterBuffersByName.clear();
  }
  for (const auto &[bindingId, parameterBuffer] : m_parameterBuffersByName) {
    if (!parameterBuffer) {
      continue;
    }
    const auto &binding = parameterBuffer->getBinding();
    for (const auto &member : binding.members) {
      const auto value =
          readShaderBindingParameterValue(bindingId, StringID(member.name));
      if (value.has_value()) {
        clone->writeShaderBindingParameterValue(bindingId,
                                                StringID(member.name), *value);
      }
    }
  }
  clone->syncGpuData();
  clone->m_materialStateVersion = m_materialStateVersion;
  clone->m_materialStateDirty = m_materialStateDirty;
  return clone;
}

/*****************************************************************
 * Accessors
 *****************************************************************/

GpuResourceRef
MaterialInstance::getShaderBindingResource(StringID bindingName) const {
  if (m_usesEnvelopeStorage) {
    return {};
  }
  auto it = m_parameterBuffersByName.find(bindingName);
  if (it == m_parameterBuffersByName.end() || !it->second ||
      it->second->getBuffer().empty()) {
    return {};
  }
  return GpuResourceRef{*it->second};
}

const std::vector<u8> &
MaterialInstance::getShaderBindingBufferBytes(StringID bindingName) const {
  if (m_usesEnvelopeStorage) {
    return kEmptyBuffer;
  }
  if (auto parameterBuffer = findParameterBuffer(bindingName))
    return parameterBuffer->get().getBuffer();
  return kEmptyBuffer;
}

std::optional<std::reference_wrapper<const ShaderResourceBinding>>
MaterialInstance::getShaderBindingBufferLayout(StringID bindingName) const {
  if (m_usesEnvelopeStorage) {
    return std::nullopt;
  }
  if (auto parameterBuffer = findParameterBuffer(bindingName))
    return std::cref(parameterBuffer->get().getBinding());
  return std::nullopt;
}

const std::vector<u8> &MaterialInstance::getShaderBindingBufferBytes() const {
  if (m_usesEnvelopeStorage) {
    static const std::vector<u8> kEmpty;
    return kEmpty;
  }
  assert(m_parameterBuffersByName.size() <= 1 &&
         "getShaderBindingBufferBytes(): multiple parameter buffers; use "
         "getShaderBindingBufferBytes(bindingName) instead");
  if (m_parameterBuffersByName.empty()) {
    static const std::vector<u8> kEmpty;
    return kEmpty;
  }
  return m_parameterBuffersByName.begin()->second->getBuffer();
}

std::optional<std::reference_wrapper<const ShaderResourceBinding>>
MaterialInstance::getShaderBindingBufferLayout() const {
  if (m_usesEnvelopeStorage) {
    return std::nullopt;
  }
  assert(m_parameterBuffersByName.size() <= 1 &&
         "getShaderBindingBufferLayout(): multiple parameter buffers; use "
         "getShaderBindingBufferLayout(bindingName) instead");
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
    fatalUndefinedPass(
        m_template ? m_template->getName() : std::string("<null>"), pass);
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

u64 MaterialInstance::addPassStateListener(std::function<void()> callback) {
  const u64 id = m_nextListenerId++;
  m_passStateListeners.emplace(id, std::move(callback));
  return id;
}

void MaterialInstance::removePassStateListener(u64 listenerId) {
  m_passStateListeners.erase(listenerId);
}

void MaterialInstance::setBsdfType(std::string bsdfType) {
  if (m_bsdfType == bsdfType) {
    return;
  }
  activateEnvelopeStorage();
  m_bsdfType = std::move(bsdfType);
  markMaterialStateDirty();
}

const std::string &MaterialInstance::getBsdfType() const { return m_bsdfType; }

void MaterialInstance::setMaterialSourceUri(ResourceUri sourceUri) {
  if (m_materialSourceUri == sourceUri) {
    return;
  }
  m_materialSourceUri = std::move(sourceUri);
  markMaterialStateDirty();
}

const ResourceUri &MaterialInstance::getMaterialSourceUri() const {
  return m_materialSourceUri;
}

void MaterialInstance::setMaterialSourceSignature(StringID signature) {
  if (m_materialSourceSignature == signature) {
    return;
  }
  m_materialSourceSignature = signature;
  markMaterialStateDirty();
}

StringID MaterialInstance::getMaterialSourceSignature() const {
  return m_materialSourceSignature;
}

void MaterialInstance::setMaterialSourceReflectionHash(std::string hash) {
  if (m_materialSourceReflectionHash == hash) {
    return;
  }
  m_materialSourceReflectionHash = std::move(hash);
  markMaterialStateDirty();
}

const std::string &MaterialInstance::getMaterialSourceReflectionHash() const {
  return m_materialSourceReflectionHash;
}

void MaterialInstance::setMaterialContractReflection(
    MaterialContractReflection reflection) {
  m_materialContractReflection = std::move(reflection);
  markMaterialStateDirty();
}

std::optional<std::reference_wrapper<const MaterialContractReflection>>
MaterialInstance::getMaterialContractReflection() const {
  if (!m_materialContractReflection.has_value()) {
    return std::nullopt;
  }
  return std::cref(*m_materialContractReflection);
}

void MaterialInstance::setRenderClass(std::string renderClass) {
  if (m_renderClass == renderClass) {
    return;
  }
  activateEnvelopeStorage();
  m_renderClass = std::move(renderClass);
  markMaterialStateDirty();
}

const std::string &MaterialInstance::getRenderClass() const {
  return m_renderClass;
}

void MaterialInstance::setMaterialTags(std::vector<std::string> tags) {
  if (m_tags == tags) {
    return;
  }
  activateEnvelopeStorage();
  m_tags = std::move(tags);
  markMaterialStateDirty();
}

const std::vector<std::string> &MaterialInstance::getMaterialTags() const {
  return m_tags;
}

void MaterialInstance::setAuthoringMetadata(
    std::unordered_map<std::string, std::string> metadata) {
  if (m_authoringMetadata == metadata) {
    return;
  }
  activateEnvelopeStorage();
  m_authoringMetadata = std::move(metadata);
  markMaterialStateDirty();
}

const std::unordered_map<std::string, std::string> &
MaterialInstance::getAuthoringMetadata() const {
  return m_authoringMetadata;
}

void MaterialInstance::setMaterialEnvelope(StringID parameterName,
                                           MaterialParameterEnvelope envelope) {
  activateEnvelopeStorage();
  m_materialEnvelopesByName[parameterName] = std::move(envelope);
  markMaterialStateDirty();
}

std::optional<std::reference_wrapper<const MaterialParameterEnvelope>>
MaterialInstance::getMaterialEnvelope(StringID parameterName) const {
  auto it = m_materialEnvelopesByName.find(parameterName);
  if (it == m_materialEnvelopesByName.end()) {
    return std::nullopt;
  }
  return std::cref(it->second);
}

usize MaterialInstance::getMaterialEnvelopeCount() const {
  return m_materialEnvelopesByName.size();
}

void MaterialInstance::addMaterialDependency(
    MaterialResourceDependency dependency) {
  m_materialDependencies.push_back(std::move(dependency));
  markMaterialStateDirty();
}

const std::vector<MaterialResourceDependency> &
MaterialInstance::getMaterialDependencies() const {
  return m_materialDependencies;
}

bool MaterialInstance::hasDefinedPass(StringID pass) const {
  return m_template && m_template->getPassDefinition(pass).has_value();
}

void MaterialInstance::markMaterialStateDirty() {
  ++m_materialStateVersion;
  m_materialStateDirty = true;
}

void MaterialInstance::activateEnvelopeStorage() {
  if (m_usesEnvelopeStorage) {
    return;
  }
  m_usesEnvelopeStorage = true;
  m_parameterBuffersByName.clear();
}

} // namespace LX_core
