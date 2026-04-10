#pragma once
#include "core/gpu/render_resource.hpp"
#include "core/math/vec.hpp"
#include "core/resources/shader.hpp"
#include "core/resources/texture.hpp"
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
 * MaterialTemplate（无裸指针）
 *****************************************************************/
class MaterialTemplate : public std::enable_shared_from_this<MaterialTemplate> {
public:
  using Ptr = std::shared_ptr<MaterialTemplate>;

  MaterialTemplate(std::string name) : m_name(std::move(name)) {}

  void setPass(const std::string &passName, const RenderPassEntry &entry) {
    m_passes[passName] = entry;
    m_passHashCache.erase(passName);
  }

  std::optional<std::reference_wrapper<const RenderPassEntry>>
  getEntry(const std::string &passName) const {
    auto it = m_passes.find(passName);
    if (it != m_passes.end())
      return it->second;
    return std::nullopt;
  }

  size_t getPipelineHash(const std::string &passName) const {
    auto it = m_passHashCache.find(passName);
    if (it != m_passHashCache.end())
      return it->second;

    auto entryOpt = getEntry(passName);
    if (!entryOpt)
      return 0;

    size_t h = entryOpt->get().getHash();
    m_passHashCache[passName] = h;
    return h;
  }

  /// 构建 binding cache
  void buildBindingCache() {
    m_bindingCache.clear();

    for (auto &[_, entry] : m_passes) {
      auto shader = entry.shaderSet.getShader();
      for (auto &b : shader->getReflectionBindings()) {
        StringID id = MakeStringID(b.name);
        m_bindingCache[id] = b;
      }
    }
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(StringID id) const {
    auto it = m_bindingCache.find(id);
    if (it != m_bindingCache.end())
      return it->second;
    return std::nullopt;
  }

private:
  std::string m_name;

  std::unordered_map<std::string, RenderPassEntry> m_passes;

  mutable std::unordered_map<std::string, size_t> m_passHashCache;
  std::unordered_map<StringID, ShaderResourceBinding> m_bindingCache;
};

/*****************************************************************
 * MaterialInstance（无裸指针）
 *****************************************************************/
class MaterialInstance {
public:
  using Ptr = std::shared_ptr<MaterialInstance>;

  MaterialInstance(MaterialTemplate::Ptr tmpl) : m_template(std::move(tmpl)) {}

  void setVec4(StringID id, const Vec4f &value) {
    auto binding = m_template->findBinding(id);
    assert(binding && binding->get().type == ShaderPropertyType::Vec4);
    m_vec4s[id] = value;
  }

  void setFloat(StringID id, float value) {
    auto binding = m_template->findBinding(id);
    assert(binding && binding->get().type == ShaderPropertyType::Float);
    m_floats[id] = value;
  }

  void setTexture(StringID id, TexturePtr tex) {
    auto binding = m_template->findBinding(id);
    assert(binding && binding->get().type == ShaderPropertyType::Texture2D);
    m_textures[id] = tex;
  }

  MaterialTemplate::Ptr getTemplate() const { return m_template; }

private:
  MaterialTemplate::Ptr m_template;

  std::unordered_map<StringID, Vec4f> m_vec4s;
  std::unordered_map<StringID, float> m_floats;
  std::unordered_map<StringID, TexturePtr> m_textures;
};

} // namespace LX_core