#pragma once
#include "core/rhi/gpu_resource.hpp"
#include "core/utils/string_table.hpp"
#include <memory>
#include <vector>

namespace LX_core {

/*
@source_analysis.section PrimitiveTopology：索引 buffer 不只是字节，还携带装配语义
索引本身只是整数序列，但一旦进入渲染路径，backend 还必须知道这些整数要按什么方式
组装成图元。因此 `PrimitiveTopology` 被放在 `IndexBuffer` 一侧，而不是藏进 draw call 临时参数：

- 它和索引数据一起定义了“几何如何被解释”
- 它直接参与 `Mesh` 的 render signature
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

class IndexBuffer : public IGpuResource {
public:
  using SharedPtr = std::shared_ptr<IndexBuffer>;

  IndexBuffer(std::vector<u32> &&indices,
              PrimitiveTopology topology = PrimitiveTopology::TriangleList)
      : m_indices(std::move(indices)), m_topology(topology) {}

  static SharedPtr
  create(std::vector<u32> &&indices,
         PrimitiveTopology topology = PrimitiveTopology::TriangleList) {
    return std::make_shared<IndexBuffer>(std::move(indices), topology);
  }

/*
@source_analysis.section IndexBuffer：索引数据与 pipeline 装配约束的共同载体
`IndexBuffer` 在这里承担两类职责：

- 作为 `IGpuResource` 暴露原始索引字节，供 backend 上传和绑定
- 作为几何结构的一部分，暴露 topology

所以它不是“纯数据容器”。只要 topology 变化，即使索引值没变，pipeline 侧看到的几何接口
也已经变了。
*/
  // --- PSO 关键属性 ---

  PrimitiveTopology getTopology() const { return m_topology; }

  // --- 数据操作 ---

  void update(const std::vector<u32> &indices) {
    m_indices = indices;
    setDirty();
  }

  usize indexCount() const { return m_indices.size(); }
  ResourceType getType() const override { return ResourceType::IndexBuffer; }
  const void *getRawData() const override { return m_indices.data(); }
  u32 getByteSize() const override {
    return static_cast<u32>(m_indices.size() * sizeof(u32));
  }

private:
  std::vector<u32> m_indices;
  PrimitiveTopology m_topology;
};

using IndexBufferSharedPtr = std::shared_ptr<IndexBuffer>;

} // namespace LX_core
