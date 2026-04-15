#pragma once
#include "core/rhi/render_resource.hpp"
#include "core/math/vec.hpp"
#include "core/asset/shader.hpp"
#include "core/asset/texture.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/utils/string_table.hpp"
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <vector>

namespace LX_core {

enum class CullMode : uint8_t { None, Front, Back };
enum class CompareOp : uint8_t { Less, LessEqual, Greater, Equal, Always };
enum class BlendFactor : uint8_t { Zero, One, SrcAlpha, OneMinusSrcAlpha };

inline const char *toString(CullMode m) {
  switch (m) {
  case CullMode::None:
    return "CullNone";
  case CullMode::Front:
    return "CullFront";
  case CullMode::Back:
    return "CullBack";
  }
  return "CullUnknown";
}

inline const char *toString(CompareOp op) {
  switch (op) {
  case CompareOp::Less:
    return "Less";
  case CompareOp::LessEqual:
    return "LessEqual";
  case CompareOp::Greater:
    return "Greater";
  case CompareOp::Equal:
    return "Equal";
  case CompareOp::Always:
    return "Always";
  }
  return "CmpUnknown";
}

inline const char *toString(BlendFactor f) {
  switch (f) {
  case BlendFactor::Zero:
    return "Zero";
  case BlendFactor::One:
    return "One";
  case BlendFactor::SrcAlpha:
    return "SrcAlpha";
  case BlendFactor::OneMinusSrcAlpha:
    return "OneMinusSrcAlpha";
  }
  return "BlendUnknown";
}

/*****************************************************************
 * RenderState
 *****************************************************************/
struct RenderState {
  CullMode cullMode = CullMode::Back;
  bool depthTestEnable = true;
  bool depthWriteEnable = true;
  CompareOp depthOp = CompareOp::LessEqual;
  bool blendEnable = false;
  BlendFactor srcBlend = BlendFactor::One;
  BlendFactor dstBlend = BlendFactor::Zero;

  bool operator==(const RenderState &rhs) const {
    return cullMode == rhs.cullMode && depthTestEnable == rhs.depthTestEnable &&
           depthWriteEnable == rhs.depthWriteEnable && depthOp == rhs.depthOp &&
           blendEnable == rhs.blendEnable && srcBlend == rhs.srcBlend &&
           dstBlend == rhs.dstBlend;
  }

  size_t getHash() const {
    size_t h = 0;
    hash_combine(h, static_cast<uint32_t>(cullMode));
    hash_combine(h, depthTestEnable);
    hash_combine(h, depthWriteEnable);
    hash_combine(h, static_cast<uint32_t>(depthOp));
    hash_combine(h, blendEnable);
    hash_combine(h, static_cast<uint32_t>(srcBlend));
    hash_combine(h, static_cast<uint32_t>(dstBlend));
    return h;
  }

  StringID getRenderSignature() const {
    auto &tbl = GlobalStringTable::get();
    StringID fields[] = {
        tbl.Intern(toString(cullMode)),
        tbl.Intern(depthTestEnable ? "DepthTest" : "NoDepthTest"),
        tbl.Intern(depthWriteEnable ? "DepthWrite" : "NoDepthWrite"),
        tbl.Intern(toString(depthOp)),
        tbl.Intern(blendEnable ? "Blend" : "NoBlend"),
        tbl.Intern(toString(srcBlend)),
        tbl.Intern(toString(dstBlend)),
    };
    return tbl.compose(TypeTag::RenderState, fields);
  }
};

/*****************************************************************
 * RenderPassEntry
 *****************************************************************/
struct RenderPassEntry {
  RenderState renderState;
  ShaderProgramSet shaderSet;
  std::unordered_map<std::string, ShaderResourceBinding> bindingCache;

  size_t getHash() const {
    size_t h = renderState.getHash();
    hash_combine(h, shaderSet.getHash());
    return h;
  }

  StringID getRenderSignature() const {
    StringID fields[] = {
        shaderSet.getRenderSignature(),
        renderState.getRenderSignature(),
    };
    return GlobalStringTable::get().compose(TypeTag::RenderPassEntry, fields);
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const {
    auto it = bindingCache.find(name);
    if (it != bindingCache.end())
      return it->second;
    return std::nullopt;
  }

  // 辅助方法：从该 Pass 的 Shader 反射信息中构建缓存
  void buildCache() {
    bindingCache.clear();
    auto shader = shaderSet.getShader();
    if (!shader)
      return;

    for (const auto &b : shader->getReflectionBindings()) {
      bindingCache[b.name] = b;
    }
  }
};

/*****************************************************************
 * IMaterial — render-path contract consumed by Scene / backend
 *****************************************************************/
class IMaterial {
public:
  virtual ~IMaterial() = default;
  virtual std::vector<IRenderResourcePtr> getDescriptorResources() const = 0;
  virtual IShaderPtr getShaderInfo() const = 0;
  virtual IShaderPtr getShaderInfo(StringID pass) const { return getShaderInfo(); }
  virtual ResourcePassFlag getPassFlag() const = 0;
  virtual RenderState getRenderState(StringID pass) const = 0;
  virtual RenderState getRenderState() const { return getRenderState(Pass_Forward); }

  /// Structured per-pass signature used to build PipelineKey via
  /// `GlobalStringTable::compose(TypeTag::MaterialRender, ...)`.
  virtual StringID getRenderSignature(StringID pass) const = 0;
};

using MaterialPtr = std::shared_ptr<IMaterial>;

/*****************************************************************
 * MaterialTemplate — shader + per-pass entries + name→binding cache
 *****************************************************************/
class MaterialTemplate : public std::enable_shared_from_this<MaterialTemplate> {
  struct Token {};

public:
  using Ptr = std::shared_ptr<MaterialTemplate>;

  MaterialTemplate(Token, std::string name, IShaderPtr shader)
      : m_name(std::move(name)), m_shader(std::move(shader)) {}

  static Ptr create(std::string name, IShaderPtr shader) {
    return std::make_shared<MaterialTemplate>(Token{}, std::move(name),
                                              std::move(shader));
  }

  IShaderPtr getShader() const { return m_shader; }
  const std::string &getName() const { return m_name; }

  void setPass(StringID pass, RenderPassEntry entry) {
    m_passes[pass] = std::move(entry);
  }

  std::optional<std::reference_wrapper<const RenderPassEntry>>
  getEntry(StringID pass) const {
    auto it = m_passes.find(pass);
    if (it != m_passes.end())
      return it->second;
    return std::nullopt;
  }

  StringID getRenderPassSignature(StringID pass) const {
    auto it = m_passes.find(pass);
    if (it == m_passes.end())
      return StringID{};
    return it->second.getRenderSignature();
  }

  /// Populate `m_bindingCache` from the template shader's reflection bindings.
  /// Each entry is keyed by `StringID(binding.name)` so MaterialInstance
  /// setters can resolve a member name directly.
  void buildBindingCache() {
    m_bindingCache.clear();
    const auto addBindings = [&](const IShaderPtr &shader) {
      if (!shader)
        return;
      for (const auto &b : shader->getReflectionBindings()) {
        m_bindingCache[StringID(b.name)] = b;
      }
    };

    addBindings(m_shader);
    for (const auto &[_, entry] : m_passes) {
      addBindings(entry.shaderSet.getShader());
    }
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(StringID id) const {
    auto it = m_bindingCache.find(id);
    if (it != m_bindingCache.end())
      return it->second;
    return std::nullopt;
  }

  const std::unordered_map<StringID, RenderPassEntry, StringID::Hash> &
  getPasses() const {
    return m_passes;
  }

private:
  std::string m_name;
  IShaderPtr m_shader;
  std::unordered_map<StringID, RenderPassEntry, StringID::Hash> m_passes;
  std::unordered_map<StringID, ShaderResourceBinding> m_bindingCache;
};

/*****************************************************************
 * UboByteBufferResource — IRenderResource over a non-owning byte vector
 *
 * Used by `MaterialInstance` to expose its std140 CPU buffer to the
 * descriptor-sync pipeline without copying. Lifetime contract: the source
 * vector MUST outlive every copy of the resulting shared_ptr. Because
 * `MaterialInstance` disables copy/move, its `m_uboBuffer` address is
 * stable for the instance's full lifetime.
 *****************************************************************/
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

/*****************************************************************
 * MaterialInstance — concrete IMaterial backed by shader reflection
 *****************************************************************/
class MaterialInstance : public IMaterial {
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

  // Non-copyable, non-movable: `m_uboResource` holds a raw pointer into
  // `m_uboBuffer`. Moving the instance would dangle that pointer.
  MaterialInstance(const MaterialInstance &) = delete;
  MaterialInstance &operator=(const MaterialInstance &) = delete;
  MaterialInstance(MaterialInstance &&) = delete;
  MaterialInstance &operator=(MaterialInstance &&) = delete;

  // ==== IMaterial ====
  std::vector<IRenderResourcePtr> getDescriptorResources() const override;
  IShaderPtr getShaderInfo() const override;
  IShaderPtr getShaderInfo(StringID pass) const override;
  ResourcePassFlag getPassFlag() const override;
  RenderState getRenderState(StringID pass) const override;
  StringID getRenderSignature(StringID pass) const override;

  // ==== Per-instance UBO setters (StringID-keyed) ====
  void setVec4(StringID id, const Vec4f &value);
  void setVec3(StringID id, const Vec3f &value);
  void setFloat(StringID id, float value);
  void setInt(StringID id, int32_t value);

  // ==== Per-instance texture bindings ====
  // Takes a `CombinedTextureSamplerPtr` because that's the concrete
  // `IRenderResource` the backend descriptor path expects for `sampler2D`.
  void setTexture(StringID id, CombinedTextureSamplerPtr tex);

  // ==== GPU sync ====
  void updateUBO();

  // ==== Accessors ====
  MaterialTemplate::Ptr getTemplate() const { return m_template; }
  const std::vector<uint8_t> &getUboBuffer() const { return m_uboBuffer; }
  const ShaderResourceBinding *getUboBinding() const { return m_uboBinding; }
  bool isPassEnabled(StringID pass) const;
  void setPassEnabled(StringID pass, bool enabled);
  std::vector<StringID> getEnabledPasses() const;
  uint64_t addPassStateListener(std::function<void()> callback);
  void removePassStateListener(uint64_t listenerId);

private:
  /// Locate a UBO member by StringID, assert type, memcpy value, mark dirty.
  void writeUboMember(StringID id, const void *src, size_t nbytes,
                      ShaderPropertyType expected);
  bool hasDefinedPass(StringID pass) const;

  MaterialTemplate::Ptr m_template;

  // CPU-side std140 byte buffer, sized from shader reflection.
  std::vector<uint8_t> m_uboBuffer;
  // Non-owning pointer into m_template->getShader()->getReflectionBindings().
  const ShaderResourceBinding *m_uboBinding = nullptr;
  // Cached IRenderResource wrapper over m_uboBuffer. Built in the constructor
  // and handed out by getDescriptorResources(). Stable identity across calls.
  IRenderResourcePtr m_uboResource;
  bool m_uboDirty = false;

  // Per-instance sampler textures, keyed by binding name StringID.
  std::unordered_map<StringID, CombinedTextureSamplerPtr> m_textures;
  std::unordered_set<StringID, StringID::Hash> m_enabledPasses;
  std::unordered_map<uint64_t, std::function<void()>> m_passStateListeners;
  uint64_t m_nextListenerId = 1;
};

} // namespace LX_core
