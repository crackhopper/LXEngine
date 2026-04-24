#pragma once
#include "core/rhi/gpu_resource.hpp"
#include "core/math/vec.hpp"
#include "core/utils/hash.hpp"
#include "core/utils/string_table.hpp"
#include <any>
#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace LX_core {

/*****************************************************************
 * Layout
 *****************************************************************/
enum class DataType { Float1, Float2, Float3, Float4, Int4 };
enum class VertexInputRate { Vertex = 0, Instance = 1 };

inline const char *toString(DataType t) {
  switch (t) {
  case DataType::Float1:
    return "Float1";
  case DataType::Float2:
    return "Float2";
  case DataType::Float3:
    return "Float3";
  case DataType::Float4:
    return "Float4";
  case DataType::Int4:
    return "Int4";
  }
  return "DataUnknown";
}

inline const char *toString(VertexInputRate r) {
  switch (r) {
  case VertexInputRate::Vertex:
    return "Vertex";
  case VertexInputRate::Instance:
    return "Instance";
  }
  return "RateUnknown";
}

struct VertexLayoutItem {
  std::string name;
  uint32_t location = 0;
  DataType type;
  uint32_t size;
  uint32_t offset;
  VertexInputRate inputRate = VertexInputRate::Vertex;

  size_t hash() const {
    size_t h = 0;
    hash_combine(h, name);
    hash_combine(h, location);
    hash_combine(h, static_cast<uint32_t>(type));
    hash_combine(h, offset);
    // 必须包含这个，否则 PSO 缓存会把实例化布局和普通布局混淆
    hash_combine(h, static_cast<uint32_t>(inputRate));
    return h;
  }

  /// "{location}_{name}_{type}_{inputRate}_{offset}" 格式的叶子 StringID
  StringID getRenderSignature() const {
    std::string tag;
    tag.reserve(name.size() + 32);
    tag += std::to_string(location);
    tag += '_';
    tag += name;
    tag += '_';
    tag += toString(type);
    tag += '_';
    tag += toString(inputRate);
    tag += '_';
    tag += std::to_string(offset);
    return GlobalStringTable::get().Intern(tag);
  }

  bool operator==(const VertexLayoutItem &o) const {
    return name == o.name && location == o.location && type == o.type &&
           size == o.size && offset == o.offset && inputRate == o.inputRate;
  }
};

/*
@source_analysis.section VertexLayout：把 shader 关心的顶点输入契约显式带出来
`VertexLayout` 不是单纯给 backend 上传用的元数据；它承担的是“mesh 如何向 shader
暴露顶点输入接口”的结构契约。这里明确记录：

- attribute 名字和 location
- 数据类型
- stride / offset
- vertex 还是 instance 频率

后面的 `Mesh::getRenderSignature()`、`PipelineBuildDesc`、`SceneNode` 校验都会消费它，
因为这些流程真正关心的是顶点输入形状，而不是 `VertexPosUv` 这类 C++ 顶点类型名。
*/
class VertexLayout {
public:
  VertexLayout() = default;

  VertexLayout(std::vector<VertexLayoutItem> items, uint32_t stride)
      : m_items(std::move(items)), m_stride(stride) {
    updateHash();
  }

  const std::vector<VertexLayoutItem> &getItems() const { return m_items; }
  uint32_t getStride() const { return m_stride; }
  size_t getHash() const { return m_hash; }

  StringID getRenderSignature() const {
    auto &tbl = GlobalStringTable::get();
    std::vector<StringID> parts;
    parts.reserve(m_items.size() + 1);
    for (const auto &item : m_items)
      parts.push_back(item.getRenderSignature());
    parts.push_back(tbl.Intern(std::to_string(m_stride)));
    return tbl.compose(TypeTag::VertexLayout, parts);
  }

  bool operator==(const VertexLayout &o) const {
    return m_hash == o.m_hash && m_items == o.m_items && m_stride == o.m_stride;
  }

private:
  void updateHash() {
    m_hash = 0;
    for (const auto &item : m_items)
      hash_combine(m_hash, item.hash());
    hash_combine(m_hash, m_stride);
  }

private:
  std::vector<VertexLayoutItem> m_items;
  uint32_t m_stride = 0;
  size_t m_hash = 0;
};

} // namespace LX_core

namespace std {
template <>
struct hash<LX_core::VertexLayout> {
  size_t operator()(const LX_core::VertexLayout &l) const {
    return l.getHash();
  }
};
} // namespace std

namespace LX_core {

/*****************************************************************
 * VertexBuffer
 *****************************************************************/
// IVertexBuffer extends the generic GPU resource contract with vertex-layout
// metadata. Backend upload only needs IGpuResource; pipeline creation also
// needs the layout/hash carried here.
/*
@source_analysis.section IVertexBuffer：上传契约之外，再补一层“布局可见性”
`IGpuResource` 已经足够表达“这是一块要上传到 GPU 的字节”，但对 vertex buffer 来说
还差一件关键事实：shader 该如何解释这些字节。

所以 `IVertexBuffer` 在通用资源契约之上补了 `getLayout()` / `getLayoutHash()`。
这让同一份顶点数据既能走统一的资源上传路径，又能在 pipeline 构建时把顶点输入布局带出来。
*/
class IVertexBuffer : public IGpuResource {
public:
  virtual ~IVertexBuffer() = default;

  virtual const VertexLayout &getLayout() const = 0;
  virtual size_t getLayoutHash() const { return getLayout().getHash(); }

  virtual uint32_t getVertexCount() const = 0;

  const void *getRawData() const override = 0;
  virtual void *getRawDataMutable() = 0;
  u32 getByteSize() const override = 0;

  ResourceType getType() const override { return ResourceType::VertexBuffer; }
};

template <typename VType>
class VertexBuffer final : public IVertexBuffer {
  static_assert(std::is_standard_layout_v<VType>,
                "Vertex must be standard layout");
  static_assert(std::is_trivially_copyable_v<VType>,
                "Vertex must be trivially copyable");

public:
  static std::shared_ptr<VertexBuffer<VType>>
  create(std::vector<VType> &&vertices) {
    return std::make_shared<VertexBuffer<VType>>(std::move(vertices));
  }

  explicit VertexBuffer(std::vector<VType> &&v) : m_vertices(std::move(v)) {}

  const VertexLayout &getLayout() const override {
    static const VertexLayout layout = VType::getLayout();
    return layout;
  }

  uint32_t getVertexCount() const override {
    return static_cast<uint32_t>(m_vertices.size());
  }

  const void *getRawData() const override { return m_vertices.data(); }
  void *getRawDataMutable() override { return m_vertices.data(); }

  u32 getByteSize() const override {
    return (u32)(m_vertices.size() * sizeof(VType));
  }

private:
  std::vector<VType> m_vertices;
};

/*****************************************************************
 * Factory
 *****************************************************************/
using VertexBufferSharedPtr = std::shared_ptr<IVertexBuffer>;

/*
@source_analysis.section VertexFactory：按布局而不是按模板参数恢复 type-erased 顶点数据
工程里很多边界只想持有 `VertexBufferSharedPtr`，不想把具体 `VertexPos` / `VertexPosUv` 模板类型
一路往外传。`VertexFactory` 的作用，就是在需要“从原始顶点数组重新构造具体 buffer”时，
按 `VertexLayout` 的 hash 找回对应的具体 `VType` 创建逻辑。

也就是说，这里把“顶点类型识别”从编译期模板名，转成了运行时的布局契约。只要布局一致，
外层系统就能继续用 type-erased 的 `IVertexBuffer` 工作。
*/
class VertexFactory {
public:
  using Creator = std::function<VertexBufferSharedPtr(std::any &&rawData)>;

  template <typename VType>
  static void registerType() {
    const auto &layout = VType::getLayout();
    size_t key = layout.getHash();

    getMap()[key] = {
        layout, sizeof(VType),
        [](std::any &&rawData) -> VertexBufferSharedPtr {
          // 核心：通过 any_cast 还原 vector 并利用移动构造函数
          // 此时数据的所有权被从 any 转移到了 v 中，实现零拷贝
          try {
            auto v = std::any_cast<std::vector<VType>>(std::move(rawData));
            return std::make_shared<VertexBuffer<VType>>(std::move(v));
          } catch (const std::bad_any_cast &) {
            // 这里通常不会发生，除非 Factory 逻辑出错
            assert(false && "VertexFactory: Type mismatch in any_cast");
            return nullptr;
          }
        }};
  }

  /**
   * @brief 零拷贝创建方法
   * 接收右值引用，强制所有权转移
   */
  template <typename VType>
  static VertexBufferSharedPtr create(std::vector<VType> &&v) {
    auto &m = getMap();
    size_t key = VType::getLayout().getHash();

    auto it = m.find(key);
    if (it != m.end()) {
      // 将 vector 包装进 any 并移动进去
      return it->second.creator(
          std::make_any<std::vector<VType>>(std::move(v)));
    }

    // 如果没找到，尝试动态注册（可选）
    // registerType<VType>();
    // return create(std::move(v));

    return nullptr;
  }

private:
  struct Entry {
    VertexLayout layout;
    size_t stride;
    Creator creator;
  };

  static std::unordered_map<size_t, Entry> &getMap() {
    static std::unordered_map<size_t, Entry> m;
    return m;
  }
};

/*****************************************************************
 * Vertex定义
 *****************************************************************/
template <typename T>
struct VertexBase {
  bool operator==(const T &o) const {
    return std::memcmp(this, &o, sizeof(T)) == 0;
  }
};

/**************** 常用顶点格式 ****************/

struct VertexPos {
  Vec3f pos;

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {{
                                      {"inPos", 0, DataType::Float3,
                                       sizeof(Vec3f), offsetof(VertexPos, pos)},
                                  },
                                  sizeof(VertexPos)};
    return layout;
  }
};

struct VertexPosColor {
  Vec3f pos;
  Vec4f color;

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {
            {"inPos", 0, DataType::Float3, sizeof(Vec3f),
             offsetof(VertexPosColor, pos)},
            {"inColor", 1, DataType::Float4, sizeof(Vec4f),
             offsetof(VertexPosColor, color)},
        },
        sizeof(VertexPosColor)};
    return layout;
  }
};

struct VertexPosUV {
  Vec3f pos;
  Vec2f uv;

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {
            {"inPos", 0, DataType::Float3, sizeof(Vec3f),
             offsetof(VertexPosUV, pos)},
            {"inUV", 1, DataType::Float2, sizeof(Vec2f),
             offsetof(VertexPosUV, uv)},
        },
        sizeof(VertexPosUV)};
    return layout;
  }
};

// PBR 顶点 (Pos + Normal + UV + Tangent)
struct VertexPBR : VertexBase<VertexPBR> {
  Vec3f pos;
  Vec3f normal;
  Vec2f uv;
  Vec4f tangent; // w分量通常用于存储副法线方向(bitangent sign)

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {{"inPos", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexPBR, pos)},
         {"inNormal", 1, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexPBR, normal)},
         {"inUV", 2, DataType::Float2, sizeof(Vec2f), offsetof(VertexPBR, uv)},
         {"inTangent", 3, DataType::Float4, sizeof(Vec4f),
          offsetof(VertexPBR, tangent)}},
        sizeof(VertexPBR)};
    return layout;
  }
};

// 骨骼动画顶点 (Pos + Normal + UV + BoneIDs + Weights)
struct VertexSkinned : VertexBase<VertexSkinned> {
  Vec3f pos;
  Vec3f normal;
  Vec2f uv;
  Vec4i boneIds; // 4个骨骼索引
  Vec4f weights; // 4个权重

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {{"inPos", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexSkinned, pos)},
         {"inNormal", 1, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexSkinned, normal)},
         {"inUV", 2, DataType::Float2, sizeof(Vec2f),
          offsetof(VertexSkinned, uv)},
         {"inBoneIds", 3, DataType::Int4, sizeof(Vec4i),
          offsetof(VertexSkinned, boneIds)},
         {"inWeights", 4, DataType::Float4, sizeof(Vec4f),
          offsetof(VertexSkinned, weights)}},
        sizeof(VertexSkinned)};
    return layout;
  }
};

// UI/2D 顶点 (Pos + UV + Color)
struct VertexUI : VertexBase<VertexUI> {
  Vec2f pos;
  Vec2f uv;
  Vec4f color; // 带有透明度的 UI 颜色

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {{"inPos", 0, DataType::Float2, sizeof(Vec2f), offsetof(VertexUI, pos)},
         {"inUV", 1, DataType::Float2, sizeof(Vec2f), offsetof(VertexUI, uv)},
         {"inColor", 2, DataType::Float4, sizeof(Vec4f),
          offsetof(VertexUI, color)}},
        sizeof(VertexUI)};
    return layout;
  }
};

struct VertexNormalTangent : VertexBase<VertexNormalTangent> {
  Vec3f normal;
  Vec4f tangent;

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {{"inNormal", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexNormalTangent, normal)},
         {"inTangent", 1, DataType::Float4, sizeof(Vec4f),
          offsetof(VertexNormalTangent, tangent)}},
        sizeof(VertexNormalTangent)};
    return layout;
  }
};

struct VertexBoneWeightIndex : VertexBase<VertexBoneWeightIndex> {
  Vec4i boneIds;
  Vec4f weights;

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {{"inBoneIds", 0, DataType::Int4, sizeof(Vec4i),
          offsetof(VertexBoneWeightIndex, boneIds)},
         {"inWeights", 1, DataType::Float4, sizeof(Vec4f),
          offsetof(VertexBoneWeightIndex, weights)}},
        sizeof(VertexBoneWeightIndex)};
    return layout;
  }
};

/// Blinn-Phong skinned mesh vertex (`blinnphong_0` / `pipeline.cpp`).
struct VertexPosNormalUvBone : VertexBase<VertexPosNormalUvBone> {
  Vec3f pos;
  Vec3f normal;
  Vec2f uv;
  Vec4f tangent;
  Vec4i boneIDs;
  Vec4f boneWeights;

  VertexPosNormalUvBone() = default;
  VertexPosNormalUvBone(Vec3f p, Vec3f n, Vec2f u, Vec4f t, Vec4i bid, Vec4f bw)
      : pos(p), normal(n), uv(u), tangent(t), boneIDs(bid), boneWeights(bw) {}

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {{"inPos", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexPosNormalUvBone, pos)},
         {"inNormal", 1, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexPosNormalUvBone, normal)},
         {"inUV", 2, DataType::Float2, sizeof(Vec2f),
          offsetof(VertexPosNormalUvBone, uv)},
         {"inTangent", 3, DataType::Float4, sizeof(Vec4f),
          offsetof(VertexPosNormalUvBone, tangent)},
         {"inBoneIds", 4, DataType::Int4, sizeof(Vec4i),
          offsetof(VertexPosNormalUvBone, boneIDs)},
         {"inWeights", 5, DataType::Float4, sizeof(Vec4f),
          offsetof(VertexPosNormalUvBone, boneWeights)}},
        sizeof(VertexPosNormalUvBone)};
    return layout;
  }
};

} // namespace LX_core
