#pragma once

#include "core/asset/material_contract.hpp"
#include "core/asset/material_parameter_envelope.hpp"
#include "core/asset/material_template.hpp"
#include "core/asset/parameter_buffer.hpp"
#include "core/asset/texture.hpp"
#include "core/math/vec.hpp"
#include "core/resource/resource_handle.hpp"
#include "core/resource/resource_uri.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/scene/scene_resource_handles.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LX_core {

struct MaterialResourceDependency final {
  MaterialEnvelopeKind kind = MaterialEnvelopeKind::Texture;
  ResourceUri uri;
  ResourceIdentityHandle resourceHandle;
  std::string parameterName;
  TextureContent textureContent = TextureContent::Unknown;
};

enum class MaterialParameterValueType {
  Float,
  Int,
  Vec3,
  Vec4,
};

struct MaterialParameterValue final {
  MaterialParameterValueType type = MaterialParameterValueType::Float;
  float floatValue = 0.0f;
  i32 intValue = 0;
  Vec4f vectorValue{0.0f, 0.0f, 0.0f, 0.0f};
};

/*
@source_analysis.section MaterialInstance：surface envelope 的运行时账本
`MaterialInstance` 不再只是 `MaterialTemplate` 的参数副本。071/073 之后，它同时承担
两条边界：

1. surface material 主线：保存 BSDF type、material source URI/signature、contract
   reflection、参数 envelope 和资源依赖。
2. 过渡/非 surface helper：保留 shader-binding buffer/texture API，供 post-process、
   procedural 或旧测试路径使用。

这条分层很重要：envelope-backed surface instance 会忽略旧 shader-binding 参数写入，
防止旧 material UBO 重新成为默认 surface truth。真正会影响 pipeline identity 的是
material type/source variant；普通 envelope 参数值只改变材质数据和上传版本。
*/
class MaterialInstance {
  struct Token {};

public:
  using SharedPtr = std::shared_ptr<MaterialInstance>;
  using UniquePtr = std::unique_ptr<MaterialInstance>;

  MaterialInstance(Token, MaterialTemplateSharedPtr tmpl);

  static SharedPtr create(MaterialTemplateSharedPtr tmpl) {
    return std::make_shared<MaterialInstance>(Token{}, std::move(tmpl));
  }

  static UniquePtr createUnique(MaterialTemplateSharedPtr tmpl) {
    return UniquePtr(new MaterialInstance(Token{}, std::move(tmpl)));
  }

  MaterialInstance(const MaterialInstance &) = delete;
  MaterialInstance &operator=(const MaterialInstance &) = delete;
  MaterialInstance(MaterialInstance &&) = delete;
  MaterialInstance &operator=(MaterialInstance &&) = delete;

  IShaderSharedPtr getPassShader(StringID pass) const;
  [[nodiscard]] std::optional<std::reference_wrapper<const ShaderProgramSet>>
  getPassShaderProgram(StringID pass) const;
  RenderState getPassRenderState(StringID pass) const;
  StringID getPipelineSignature(StringID pass) const;
  [[nodiscard]] StringID
  getMaterialTypeVariantSignature(const ShaderProgramSet &resolvedShader) const;

  // Non-surface helper API for shader-owned buffer bindings such as
  // post-process/procedural materials. Material v2 surface truth uses
  // PBRT envelopes below instead.
  // Envelope-backed surface instances intentionally ignore these writes so
  // remaining non-surface/probe call sites cannot recreate default material
  // truth through shader-binding buffers.
  void writeShaderBindingParameter(StringID bindingName, StringID memberName,
                                   float value);
  void writeShaderBindingParameter(StringID bindingName, StringID memberName,
                                   i32 value);
  void writeShaderBindingParameter(StringID bindingName, StringID memberName,
                                   const Vec3f &value);
  void writeShaderBindingParameter(StringID bindingName, StringID memberName,
                                   const Vec4f &value);
  void writeShaderBindingParameterValue(StringID bindingName,
                                        StringID memberName,
                                        const MaterialParameterValue &value);
  [[nodiscard]] std::optional<MaterialParameterValue>
  readShaderBindingParameterValue(StringID bindingName,
                                  StringID memberName) const;
  [[nodiscard]] std::optional<std::reference_wrapper<const StructMemberInfo>>
  findShaderBindingParameterMember(StringID bindingName,
                                   StringID memberName) const;

  void setTexture(StringID bindingName, CombinedTextureSamplerSharedPtr tex);
  void setTextureHandle(StringID bindingName, TextureHandle handle);
  [[nodiscard]] TextureHandle getTextureHandle(StringID bindingName) const;
  [[nodiscard]] CombinedTextureSamplerSharedPtr
  getTexture(StringID bindingName) const;
  void addOwnedTextureHandle(TextureHandle handle);
  void forEachOwnedTextureHandle(
      const std::function<void(TextureHandle)> &callback) const;
  void forEachPendingTextureBinding(
      const std::function<void(
          StringID, const CombinedTextureSamplerSharedPtr &)> &callback) const;

  void syncGpuData();
  [[nodiscard]] u64 getMaterialStateVersion() const {
    return m_materialStateVersion;
  }
  [[nodiscard]] bool hasPendingMaterialStateSync() const {
    return m_materialStateDirty;
  }
  void clearPendingMaterialStateSync() { m_materialStateDirty = false; }

  MaterialTemplateSharedPtr getTemplate() const { return m_template; }

  // Shader-binding buffer accessors.
  usize getShaderBindingBufferCount() const {
    return m_parameterBuffersByName.size();
  }
  [[nodiscard]] GpuResourceRef
  getShaderBindingResource(StringID bindingName) const;
  const std::vector<u8> &
  getShaderBindingBufferBytes(StringID bindingName) const;
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  getShaderBindingBufferLayout(StringID bindingName) const;
  // Single-binding shortcuts (assert if multiple buffer bindings exist).
  const std::vector<u8> &getShaderBindingBufferBytes() const;
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  getShaderBindingBufferLayout() const;

  bool isPassEnabled(StringID pass) const;
  void setPassEnabled(StringID pass, bool enabled);
  std::vector<StringID> getEnabledPasses() const;
  u64 addPassStateListener(std::function<void()> callback);
  void removePassStateListener(u64 listenerId);
  [[nodiscard]] SharedPtr cloneInstanceData() const;
  [[nodiscard]] UniquePtr cloneInstanceDataUnique() const;

  void setBsdfType(std::string bsdfType);
  [[nodiscard]] const std::string &getBsdfType() const;
  void setMaterialSourceUri(ResourceUri sourceUri);
  [[nodiscard]] const ResourceUri &getMaterialSourceUri() const;
  void setMaterialSourceSignature(StringID signature);
  [[nodiscard]] StringID getMaterialSourceSignature() const;
  void setMaterialSourceReflectionHash(std::string hash);
  [[nodiscard]] const std::string &getMaterialSourceReflectionHash() const;
  void setMaterialContractReflection(MaterialContractReflection reflection);
  [[nodiscard]] std::optional<
      std::reference_wrapper<const MaterialContractReflection>>
  getMaterialContractReflection() const;
  void setRenderClass(std::string renderClass);
  [[nodiscard]] const std::string &getRenderClass() const;
  void setMaterialTags(std::vector<std::string> tags);
  [[nodiscard]] const std::vector<std::string> &getMaterialTags() const;
  void
  setAuthoringMetadata(std::unordered_map<std::string, std::string> metadata);
  [[nodiscard]] const std::unordered_map<std::string, std::string> &
  getAuthoringMetadata() const;
  void setMaterialEnvelope(StringID parameterName,
                           MaterialParameterEnvelope envelope);
  [[nodiscard]] std::optional<
      std::reference_wrapper<const MaterialParameterEnvelope>>
  getMaterialEnvelope(StringID parameterName) const;
  [[nodiscard]] usize getMaterialEnvelopeCount() const;
  void addMaterialDependency(MaterialResourceDependency dependency);
  [[nodiscard]] const std::vector<MaterialResourceDependency> &
  getMaterialDependencies() const;

private:
  std::optional<std::reference_wrapper<ParameterBuffer>>
  findParameterBuffer(StringID bindingName);
  std::optional<std::reference_wrapper<const ParameterBuffer>>
  findParameterBuffer(StringID bindingName) const;
  bool hasDefinedPass(StringID pass) const;
  void markMaterialStateDirty();
  void activateEnvelopeStorage();

  MaterialTemplateSharedPtr m_template;
  // Runtime resources grouped by the same binding names used by the template's
  // canonical ShaderResourceBinding table.
  std::unordered_map<StringID, std::unique_ptr<ParameterBuffer>, StringID::Hash>
      m_parameterBuffersByName;

  // Runtime sampled-image resources keyed by the same canonical binding names
  // as the template's texture bindings.
  std::unordered_map<StringID, CombinedTextureSamplerSharedPtr, StringID::Hash>
      m_pendingTextureBindingsByName;
  std::unordered_map<StringID, TextureHandle, StringID::Hash>
      m_textureHandlesByName;
  std::vector<TextureHandle> m_ownedTextureHandles;
  // Structural pass participation state. This changes scene validation,
  // unlike ordinary parameter writes.
  std::unordered_set<StringID, StringID::Hash> m_enabledPasses;
  std::unordered_map<u64, std::function<void()>> m_passStateListeners;
  u64 m_nextListenerId = 1;
  std::string m_bsdfType;
  ResourceUri m_materialSourceUri;
  StringID m_materialSourceSignature;
  std::string m_materialSourceReflectionHash;
  std::optional<MaterialContractReflection> m_materialContractReflection;
  std::string m_renderClass;
  std::vector<std::string> m_tags;
  std::unordered_map<std::string, std::string> m_authoringMetadata;
  std::unordered_map<StringID, MaterialParameterEnvelope, StringID::Hash>
      m_materialEnvelopesByName;
  std::vector<MaterialResourceDependency> m_materialDependencies;
  u64 m_materialStateVersion = 0;
  bool m_materialStateDirty = false;
  bool m_usesEnvelopeStorage = false;
};

using MaterialInstanceSharedPtr = MaterialInstance::SharedPtr;

} // namespace LX_core
