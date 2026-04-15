#pragma once
#include "core/rhi/render_resource.hpp"
#include "core/utils/string_table.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace LX_core {

/**
 * @brief 图元拓扑类型：决定了顶点如何被组装成几何图形
 * 对应 Vulkan 的 VkPrimitiveTopology 或 DX12 的 D3D12_PRIMITIVE_TOPOLOGY_TYPE
 */
enum class PrimitiveTopology : uint32_t {
  PointList = 0,
  LineList = 1,
  LineStrip = 2,
  TriangleList = 3,
  TriangleStrip = 4,
  TriangleFan = 5
};

/// Leaf StringID for a topology. Free function because enum class can't
/// carry methods; used by `Mesh::getRenderSignature`.
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
enum class IndexType : uint32_t { Uint16 = 0, Uint32 = 1 };

class IndexBuffer : public IRenderResource {
public:
  using Ptr = std::shared_ptr<IndexBuffer>;

  IndexBuffer(std::vector<uint32_t> &&indices,
              PrimitiveTopology topology = PrimitiveTopology::TriangleList)
      : m_indices(std::move(indices)), m_topology(topology) {
    calculateRange();
  }

  static Ptr
  create(std::vector<uint32_t> &&indices,
         PrimitiveTopology topology = PrimitiveTopology::TriangleList) {
    return std::make_shared<IndexBuffer>(std::move(indices), topology);
  }

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
    size_t h = std::hash<uint32_t>{}(static_cast<uint32_t>(m_topology));
    // 混合索引位宽信息
    h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(getIndexType())) +
         0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }

  // --- 数据操作 ---

  void update(const std::vector<uint32_t> &indices) {
    m_indices = indices;
    calculateRange();
    setDirty();
  }

  size_t indexCount() const { return m_indices.size(); }
  ResourceType getType() const override { return ResourceType::IndexBuffer; }
  const void *getRawData() const override { return m_indices.data(); }
  u32 getByteSize() const override {
    return static_cast<u32>(m_indices.size() * sizeof(uint32_t));
  }

  u32 maxIndex() const { return m_maxIndex; }
  u32 minIndex() const { return m_minIndex; }

  /**
   * @brief 偏移所有索引值 (常见于多 Mesh 合并或 Batching)
   */
  void offset(uint32_t offsetValue) {
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
    uint32_t currentMin = m_minIndex;
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
  std::vector<uint32_t> m_indices;
  PrimitiveTopology m_topology;
  uint32_t m_maxIndex;
  uint32_t m_minIndex;
};

using IndexBufferPtr = std::shared_ptr<IndexBuffer>;

} // namespace LX_core