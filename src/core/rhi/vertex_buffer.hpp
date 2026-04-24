#pragma once
#include "core/rhi/gpu_resource.hpp"
#include "core/math/vec.hpp"
#include "core/utils/hash.hpp"
#include "core/utils/string_table.hpp"
#include <cstring>
#include <string>
#include <type_traits>
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
  VertexAttributeLocation32 location = 0;
  DataType type;
  VertexAttributeSize32 size = 0;
  ByteOffset32 offset = 0;
  VertexInputRate inputRate = VertexInputRate::Vertex;

  size_t hash() const {
    size_t h = 0;
    hash_combine(h, name);
    hash_combine(h, location);
    hash_combine(h, static_cast<u32>(type));
    hash_combine(h, offset);
    // 必须包含这个，否则 PSO 缓存会把实例化布局和普通布局混淆
    hash_combine(h, static_cast<u32>(inputRate));
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

  VertexLayout(std::vector<VertexLayoutItem> items, VertexStride32 stride)
      : m_items(std::move(items)), m_stride(stride) {
    updateHash();
  }

  const std::vector<VertexLayoutItem> &getItems() const { return m_items; }
  VertexStride32 getStride() const { return m_stride; }

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
  VertexStride32 m_stride = 0;
  size_t m_hash = 0;
};

} // namespace LX_core

namespace LX_core {

/*****************************************************************
 * VertexBuffer
 *****************************************************************/
// IVertexBuffer extends the generic GPU resource contract with vertex-layout
// metadata. Backend upload only needs IGpuResource; pipeline creation also
// needs the layout carried here.
/*
@source_analysis.section IVertexBuffer：上传契约之外，再补一层“布局可见性”
`IGpuResource` 已经足够表达“这是一块要上传到 GPU 的字节”，但对 vertex buffer 来说
还差一件关键事实：shader 该如何解释这些字节。

所以 `IVertexBuffer` 在通用资源契约之上补了 `getLayout()`。
这让同一份顶点数据既能走统一的资源上传路径，又能在 pipeline 构建时把顶点输入布局带出来。
*/
class IVertexBuffer : public IGpuResource {
public:
  virtual ~IVertexBuffer() = default;

  virtual const VertexLayout &getLayout() const = 0;

  virtual VertexCount getVertexCount() const = 0;

  const void *getRawData() const override = 0;
  ResourceByteSize32 getByteSize() const override = 0;

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

  VertexCount getVertexCount() const override {
    return m_vertices.size();
  }

  const void *getRawData() const override { return m_vertices.data(); }

  ResourceByteSize32 getByteSize() const override {
    return static_cast<ResourceByteSize32>(m_vertices.size() * sizeof(VType));
  }

private:
  std::vector<VType> m_vertices;
};

using VertexBufferSharedPtr = std::shared_ptr<IVertexBuffer>;

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

/// Skinned mesh vertex: position / normal / UV / tangent + 4-bone skinning.
/// Consumed by shaders declared via `.material` YAML assets.
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
