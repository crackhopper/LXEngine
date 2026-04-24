#pragma once
#include "core/rhi/gpu_resource.hpp"
#include "core/utils/hash.hpp"
#include "core/utils/string_table.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace LX_core {

/*
@source_analysis.section PrimitiveTopology：索引 buffer 不只是字节，还携带装配语义
索引本身只是整数序列，但一旦进入渲染路径，backend 还必须知道这些整数要按什么方式
组装成图元。因此 `PrimitiveTopology` 被放在 `IndexBuffer` 一侧，而不是藏进 draw call 临时参数：

- 它和索引数据一起定义了“几何如何被解释”
- 它直接参与 `Mesh` 的 render signature / layout hash
- 改拓扑会改变 pipeline 需求，即使索引字节完全不变
*/
enum class PrimitiveTopology : u32 {
  PointList = 0,
  LineList = 1,
  LineStrip = 2,
  TriangleList = 3,
  TriangleStrip = 4,
  TriangleFan = 5
};

/*
@source_analysis.section topologySignature：把拓扑收束成可组合的结构叶子
`Mesh::getRenderSignature()` 需要把“顶点布局 + 图元拓扑”一起收束进 `StringID` 组合树。
`topologySignature()` 的角色就是把枚举值变成稳定的叶子签名，让更外层不用关心底层 enum 编码。
*/
inline StringID topologySignature(PrimitiveTopology t) {
  auto &tbl = GlobalStringTable::get();
  switch (t) {
  case PrimitiveTopology::PointList:
    return tbl.Intern("point");
  case PrimitiveTopology::LineList:
    return tbl.Intern("line");
  case PrimitiveTopology::LineStrip:
    return tbl.Intern("lineStrip");
  case PrimitiveTopology::TriangleList:
    return tbl.Intern("tri");
  case PrimitiveTopology::TriangleStrip:
    return tbl.Intern("triStrip");
  case PrimitiveTopology::TriangleFan:
    return tbl.Intern("triFan");
  }
  return tbl.Intern("topoUnknown");
}

/**
 * @brief 索引数据位宽
 */
enum class IndexType : u32 { Uint16 = 0, Uint32 = 1 };

class IndexBuffer : public IGpuResource {
public:
  using SharedPtr = std::shared_ptr<IndexBuffer>;

  IndexBuffer(std::vector<MeshIndex32> &&indices,
              PrimitiveTopology topology = PrimitiveTopology::TriangleList)
      : m_indices(std::move(indices)), m_topology(topology) {
    calculateRange();
  }

  static SharedPtr
  create(std::vector<MeshIndex32> &&indices,
         PrimitiveTopology topology = PrimitiveTopology::TriangleList) {
    return std::make_shared<IndexBuffer>(std::move(indices), topology);
  }

/*
@source_analysis.section IndexBuffer：索引数据与 pipeline 装配约束的共同载体
`IndexBuffer` 在这里承担两类职责：

- 作为 `IGpuResource` 暴露原始索引字节，供 backend 上传和绑定
- 作为几何结构的一部分，暴露 topology / indexType / layout hash

所以它不是“纯数据容器”。只要 topology 变化，即使索引值没变，pipeline 侧看到的几何接口
也已经变了，这正是 `getLayoutHash()` 把 topology 编进去的原因。
*/
  // --- PSO 关键属性 ---

  PrimitiveTopology getTopology() const { return m_topology; }
  void setTopology(PrimitiveTopology topo) { m_topology = topo; }

  // 目前内部统一使用 u32 存储，返回 Uint32
  IndexType getIndexType() const { return IndexType::Uint32; }

  /**
   * @brief 获取用于 PSO 缓存查找的哈希值
   * 拓扑结构是管线状态的一部分，必须参与哈希
   */
  size_t getLayoutHash() const {
    size_t h = 0;
    hash_combine(h, static_cast<u32>(m_topology));
    hash_combine(h, static_cast<u32>(getIndexType()));
    return h;
  }

  // --- 数据操作 ---

  void update(const std::vector<MeshIndex32> &indices) {
    m_indices = indices;
    calculateRange();
    setDirty();
  }

  IndexCount indexCount() const { return m_indices.size(); }
  ResourceType getType() const override { return ResourceType::IndexBuffer; }
  const void *getRawData() const override { return m_indices.data(); }
  ResourceByteSize32 getByteSize() const override {
    return static_cast<ResourceByteSize32>(m_indices.size() *
                                           sizeof(MeshIndex32));
  }

  MeshIndex32 maxIndex() const { return m_maxIndex; }
  MeshIndex32 minIndex() const { return m_minIndex; }

  /**
   * @brief 偏移所有索引值 (常见于多 Mesh 合并或 Batching)
   */
  void offset(MeshIndex32 offsetValue) {
    for (auto &index : m_indices) {
      index += offsetValue;
    }
    m_maxIndex += offsetValue;
    m_minIndex += offsetValue;
    setDirty();
  }

  /**
   * @brief 将索引重置回从 0 开始的状态
   */
  void resetOffset() {
    if (m_indices.empty())
      return;
    MeshIndex32 currentMin = m_minIndex;
    for (auto &index : m_indices) {
      index -= currentMin;
    }
    m_maxIndex -= currentMin;
    m_minIndex = 0;
    setDirty();
  }

private:
  void calculateRange() {
    if (!m_indices.empty()) {
      auto [minIt, maxIt] =
          std::minmax_element(m_indices.begin(), m_indices.end());
      m_minIndex = *minIt;
      m_maxIndex = *maxIt;
    } else {
      m_minIndex = m_maxIndex = 0;
    }
  }

private:
  std::vector<MeshIndex32> m_indices;
  PrimitiveTopology m_topology;
  MeshIndex32 m_maxIndex = 0;
  MeshIndex32 m_minIndex = 0;
};

using IndexBufferSharedPtr = std::shared_ptr<IndexBuffer>;

} // namespace LX_core
