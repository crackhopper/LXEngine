#pragma once
#include "core/rhi/gpu_resource.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/utils/hash.hpp"
#include "core/utils/string_table.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace LX_core {

/*****************************************************************
 * Shader enums
 *****************************************************************/
enum class ShaderPropertyType {
  Float,
  Vec2,
  Vec3,
  Vec4,
  Mat4,
  Int,

  UniformBuffer,
  StorageBuffer,

  Texture2D,
  TextureCube,
  Sampler
};

/*****************************************************************
 * StructMemberInfo — std140 layout of a single UBO struct member
 *****************************************************************/
struct StructMemberInfo {
  std::string name;        // GLSL member name (e.g. "baseColor")
  ShaderPropertyType type; // Float / Int / Vec2 / Vec3 / Vec4 / Mat4
  uint32_t offset = 0;     // std140 byte offset within the block
  uint32_t size = 0;       // std140 declared byte size of this member

  bool operator==(const StructMemberInfo &rhs) const {
    return name == rhs.name && type == rhs.type && offset == rhs.offset &&
           size == rhs.size;
  }
};

/*****************************************************************
 * ShaderStage（改为 bitmask）
 *****************************************************************/
enum class ShaderStage : uint32_t {
  None = 0,
  Vertex = 1 << 0,
  Fragment = 1 << 1,
  Compute = 1 << 2,
  Geometry = 1 << 3,
  TessControl = 1 << 4,
  TessEval = 1 << 5,
};

inline ShaderStage operator|(ShaderStage a, ShaderStage b) {
  return static_cast<ShaderStage>(static_cast<uint32_t>(a) |
                                  static_cast<uint32_t>(b));
}

/*****************************************************************
 * Reflection Binding
 *****************************************************************/
struct ShaderResourceBinding {
  std::string name;

  uint32_t set = 0;
  uint32_t binding = 0;

  ShaderPropertyType type;

  uint32_t descriptorCount = 1;
  uint32_t size = 0;
  uint32_t offset = 0;

  ShaderStage stageFlags = ShaderStage::None;

  /// std140 layout of the UBO block's top-level members.
  /// Populated only when `type == ShaderPropertyType::UniformBuffer` and the
  /// block shape is flat (no nested structs / arrays-of-struct). Empty
  /// otherwise. Members are kept in spirv-cross's declared order.
  std::vector<StructMemberInfo> members;

  bool operator==(const ShaderResourceBinding &rhs) const {
    return set == rhs.set && binding == rhs.binding && type == rhs.type &&
           descriptorCount == rhs.descriptorCount;
  }
};

/*****************************************************************
 * Shader Stage Code
 *****************************************************************/
struct ShaderStageCode {
  ShaderStage stage;
  std::vector<uint32_t> bytecode;
};

struct VertexInputAttribute {
  std::string name;
  uint32_t location = 0;
  DataType type = DataType::Float1;

  bool operator==(const VertexInputAttribute &rhs) const {
    return name == rhs.name && location == rhs.location && type == rhs.type;
  }
};

/*****************************************************************
 * IShader
 *****************************************************************/
class IShader {
public:
  virtual ~IShader() = default;

  virtual const std::vector<ShaderStageCode> &getAllStages() const = 0;

  virtual const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const = 0;

  virtual const std::vector<VertexInputAttribute> &getVertexInputs() const {
    static const std::vector<VertexInputAttribute> kEmpty;
    return kEmpty;
  }

  /// ⭐ 快速查找（推荐）
  virtual std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(uint32_t set, uint32_t binding) const = 0;

  /// fallback
  virtual std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const = 0;

  virtual std::optional<std::reference_wrapper<const VertexInputAttribute>>
  findVertexInput(uint32_t location) const {
    (void)location;
    return std::nullopt;
  }

  virtual size_t getProgramHash() const = 0;

  /// Logical shader basename for file-based pipelines (e.g. `blinnphong_0`).
  /// Default empty: render path may fall back to a fixed pipeline key.
  virtual std::string getShaderName() const { return {}; }
};

using IShaderPtr = std::shared_ptr<IShader>;

/*****************************************************************
 * Shader Variant
 *****************************************************************/
struct ShaderVariant {
  std::string macroName;
  bool enabled = false;

  bool operator==(const ShaderVariant &rhs) const {
    return enabled == rhs.enabled && macroName == rhs.macroName;
  }
};

/*****************************************************************
 * Shader Program Set
 *****************************************************************/
struct ShaderProgramSet {
  std::string shaderName;
  std::vector<ShaderVariant> variants;
  IShaderPtr shader;

  size_t getHash() const {
    if (!m_dirty)
      return m_cachedHash;

    recomputeHash();
    return m_cachedHash;
  }

  StringID getRenderSignature() const {
    auto &tbl = GlobalStringTable::get();
    std::vector<std::string> enabled;
    enabled.reserve(variants.size());
    for (const auto &v : variants) {
      if (v.enabled)
        enabled.push_back(v.macroName);
    }
    std::sort(enabled.begin(), enabled.end());

    std::vector<StringID> parts;
    parts.reserve(1 + enabled.size());
    parts.push_back(tbl.Intern(shaderName));
    for (const auto &m : enabled)
      parts.push_back(tbl.Intern(m));

    return tbl.compose(TypeTag::ShaderProgram, parts);
  }

  void markDirty() { m_dirty = true; }

  bool operator==(const ShaderProgramSet &rhs) const {
    return getHash() == rhs.getHash();
  }

  IShaderPtr getShader() const { return shader; }

  bool hasEnabledVariant(const std::string &macroName) const {
    for (const auto &variant : variants) {
      if (variant.macroName == macroName)
        return variant.enabled;
    }
    return false;
  }

private:
  void recomputeHash() const {
    size_t h = 0;
    hash_combine(h, shaderName);

    // 收集 enabled
    std::vector<std::string> enabled;
    enabled.reserve(variants.size());

    for (const auto &v : variants) {
      if (v.enabled)
        enabled.push_back(v.macroName);
    }

    std::sort(enabled.begin(), enabled.end());

    for (const auto &m : enabled)
      hash_combine(h, m);

    m_cachedHash = h;
    m_dirty = false;
  }
  mutable size_t m_cachedHash = 0;
  mutable bool m_dirty = true;
};

} // namespace LX_core

namespace std {
template <>
struct hash<LX_core::ShaderResourceBinding> {
  size_t operator()(const LX_core::ShaderResourceBinding &b) const {
    size_t h = 0;
    LX_core::hash_combine(h, b.set);
    LX_core::hash_combine(h, b.binding);
    LX_core::hash_combine(h, static_cast<uint32_t>(b.type));
    LX_core::hash_combine(h, b.descriptorCount);
    return h;
  }
};
} // namespace std
