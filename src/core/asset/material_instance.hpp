#pragma once

#include "core/asset/material_template.hpp"
#include "core/asset/texture.hpp"
#include "core/math/vec.hpp"
#include "core/rhi/render_resource.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LX_core {

class UboByteBufferResource : public IRenderResource {
public:
  UboByteBufferResource(std::vector<uint8_t> &buffer, uint32_t byteSize,
                        std::function<ResourcePassFlag()> passFlagGetter)
      : m_buffer(&buffer), m_byteSize(byteSize),
        m_passFlagGetter(std::move(passFlagGetter)) {}

  ResourcePassFlag getPassFlag() const override {
    return m_passFlagGetter ? m_passFlagGetter() : ResourcePassFlag::Forward;
  }
  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return m_buffer->data(); }
  u32 getByteSize() const override { return m_byteSize; }

  StringID getBindingName() const override {
    static const StringID kName("MaterialUBO");
    return kName;
  }

private:
  std::vector<uint8_t> *m_buffer;
  uint32_t m_byteSize;
  std::function<ResourcePassFlag()> m_passFlagGetter;
};

class MaterialInstance {
  struct Token {};

public:
  using Ptr = std::shared_ptr<MaterialInstance>;

  MaterialInstance(Token, MaterialTemplate::Ptr tmpl,
                   ResourcePassFlag passFlag);

  static Ptr create(MaterialTemplate::Ptr tmpl,
                    ResourcePassFlag passFlag = ResourcePassFlag::Forward) {
    return std::make_shared<MaterialInstance>(Token{}, std::move(tmpl),
                                              passFlag);
  }

  MaterialInstance(const MaterialInstance &) = delete;
  MaterialInstance &operator=(const MaterialInstance &) = delete;
  MaterialInstance(MaterialInstance &&) = delete;
  MaterialInstance &operator=(MaterialInstance &&) = delete;

  std::vector<IRenderResourcePtr> getDescriptorResources() const;
  IShaderPtr getShaderInfo() const;
  IShaderPtr getShaderInfo(StringID pass) const;
  ResourcePassFlag getPassFlag() const;
  RenderState getRenderState(StringID pass) const;
  StringID getRenderSignature(StringID pass) const;

  void setVec4(StringID id, const Vec4f &value);
  void setVec3(StringID id, const Vec3f &value);
  void setFloat(StringID id, float value);
  void setInt(StringID id, int32_t value);

  void setTexture(StringID id, CombinedTextureSamplerPtr tex);

  void updateUBO();

  MaterialTemplate::Ptr getTemplate() const { return m_template; }
  const std::vector<uint8_t> &getUboBuffer() const { return m_uboBuffer; }
  const ShaderResourceBinding *getUboBinding() const { return m_uboBinding; }
  bool isPassEnabled(StringID pass) const;
  void setPassEnabled(StringID pass, bool enabled);
  std::vector<StringID> getEnabledPasses() const;
  uint64_t addPassStateListener(std::function<void()> callback);
  void removePassStateListener(uint64_t listenerId);

private:
  void writeUboMember(StringID id, const void *src, size_t nbytes,
                      ShaderPropertyType expected);
  bool hasDefinedPass(StringID pass) const;

  MaterialTemplate::Ptr m_template;
  std::vector<uint8_t> m_uboBuffer;
  const ShaderResourceBinding *m_uboBinding = nullptr;
  IRenderResourcePtr m_uboResource;
  bool m_uboDirty = false;

  std::unordered_map<StringID, CombinedTextureSamplerPtr> m_textures;
  std::unordered_set<StringID, StringID::Hash> m_enabledPasses;
  std::unordered_map<uint64_t, std::function<void()>> m_passStateListeners;
  uint64_t m_nextListenerId = 1;
};

using MaterialInstancePtr = MaterialInstance::Ptr;

} // namespace LX_core
