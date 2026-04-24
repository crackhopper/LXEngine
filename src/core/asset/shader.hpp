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

/*
@source_analysis.section StructMemberInfo：把 UBO 成员布局显式带出 shader 反射边界
`StructMemberInfo` 描述的不是“材质参数当前值”，而是 shader 反射出来的
结构布局事实：成员名、类型、偏移和声明尺寸。它存在的意义是让
`ShaderResourceBinding` 不只知道“这是一个 UniformBuffer”，还知道
这个 block 里面有哪些顶层成员、每个成员落在什么偏移。

这一层信息会继续影响材质系统：

- `MaterialTemplate` 会把它们收束进 canonical material binding 表
- `MaterialTemplate` 做跨 pass 一致性检查时，会比较 `members`
- `MaterialInstance` 写参数时，会按成员偏移把值写进 canonical buffer
*/
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

/*
@source_analysis.section ShaderResourceBinding：shader 反射结果在引擎里的统一描述
`ShaderResourceBinding` 是 shader 反射穿过 core/asset 边界之后的统一载体。
它把一个 descriptor binding 需要的结构性信息放到同一个对象里：

- 名字、set、binding：让上层能按语义名或 descriptor 槽位定位资源
- `type` / `descriptorCount`：让上层知道这是什么资源形状
- `size` / `members`：让 buffer 类 binding 保留布局事实，而不是只剩一个名字
- `stageFlags`：保留这个 binding 被哪些 stage 使用

因此，材质系统里提到的“material binding interface”并不是另一种独立格式，
本质上就是把这些 `ShaderResourceBinding` 对象按不同视角重新索引：

- `MaterialTemplate` 做 material-owned 过滤、canonical 化和跨 pass 一致性检查
- `MaterialInstance` 再按 canonical binding id 组织运行时资源
*/
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

/*
@source_analysis.section IShader：把编译产物和反射视图一起交给上层
对材质系统来说，`IShader` 重要的不只是字节码，还包括
`getReflectionBindings()` 这条读路径。`MaterialTemplate::rebuildMaterialInterface()`
依赖它，把 shader 的反射结果
转成材质侧可查询、可校验的 binding 视图。

也就是说，材质模板并不自己理解 GLSL 或 SPIR-V；它只消费 `IShader`
已经整理好的反射结果，并在更高一层决定 ownership、缓存视角和一致性约束。
*/
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

using IShaderSharedPtr = std::shared_ptr<IShader>;

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

/*
@source_analysis.section ShaderProgramSet：把“哪份 shader 变体参与这个 pass”收束成一项配置
`ShaderProgramSet` 既保存逻辑层的 shader 标识（`shaderName` + variants），
也保存已经解析好的 `IShaderSharedPtr`。这样 `MaterialPassDefinition` 就可以同时回答：

- 这个 pass 的结构签名应该由哪份 shader 名称和哪些 variant 组成
- 运行时如果需要反射 binding，该去拿哪一个 `IShader`

因此它是 pass 结构的一部分，而不是单纯的运行时缓存句柄。
*/
struct ShaderProgramSet {
  std::string shaderName;
  std::vector<ShaderVariant> variants;
  IShaderSharedPtr shader;

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

  IShaderSharedPtr getShader() const { return shader; }

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
