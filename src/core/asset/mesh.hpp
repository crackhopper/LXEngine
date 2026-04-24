#pragma once
#include "core/math/bounds.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/asset/shader.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/utils/hash.hpp"
#include <cassert>
#include <cstdint>
#include <memory>

namespace LX_core {

/*
@source_analysis.section Mesh：把几何资源收束成渲染路径可复用的薄边界
`Mesh` 故意不是“顶点数组 + 索引数组 + 一堆绘制状态”的大对象，而是一个很薄的
聚合边界：只把 `IVertexBuffer`、`IndexBuffer` 和包围盒绑定在一起。

这条边界回答的是“一个可绘制几何体最少需要什么结构事实”：

- 顶点布局是什么
- 图元怎样组装
- CPU / backend 都要面对的原始 buffer 在哪里

材质、shader variant、pass enable 这些都不属于 `Mesh`。这样 mesh 才能被多个材质、
多个 scene node 复用，而不会把几何身份和材质身份混成一个缓存键。
*/
class Mesh {
  struct Token {};

public:
  using SharedPtr = std::shared_ptr<Mesh>;

  VertexBufferSharedPtr vertexBuffer;
  IndexBufferSharedPtr indexBuffer;

  static SharedPtr create(VertexBufferSharedPtr vb, IndexBufferSharedPtr ib) {
    assert(vb && ib);
    return SharedPtr(new Mesh(Token{}, std::move(vb), std::move(ib)));
  }

  uint32_t getVertexCount() const { return vertexBuffer->getVertexCount(); }
  uint32_t getIndexCount() const {
    return static_cast<uint32_t>(indexBuffer->indexCount());
  }

/*
@source_analysis.section 几何签名：mesh 只输出 pipeline 真正关心的结构信息
`getLayoutHash()` 和 `getRenderSignature()` 都只组合两类东西：

- 顶点输入布局
- 索引拓扑

它们不看顶点个数、索引范围、包围盒，也不看具体字节内容。原因是 pipeline 身份只关心
“这个 draw 需要怎样的 vertex input / primitive assembly 约束”，而不关心这次画了多少个点。

因此 `MeshRender` 更像“几何接口形状”，不是几何数据内容的内容哈希。
*/
  size_t getLayoutHash() const {
    size_t hash = vertexBuffer->getLayoutHash();
    hash_combine(hash, indexBuffer->getLayoutHash());
    return hash;
  }

  /// Pass 参数保留以统一接口，当前实现忽略。
  /// 未来可用于"同一 mesh 在不同 pass 剔除属性"。
  StringID getRenderSignature(StringID /*pass*/) const {
    StringID fields[] = {
        vertexBuffer->getLayout().getRenderSignature(),
        topologySignature(indexBuffer->getTopology()),
    };
    return GlobalStringTable::get().compose(TypeTag::MeshRender, fields);
  }

  const VertexLayout &getVertexLayout() const {
    return vertexBuffer->getLayout();
  }
  PrimitiveTopology getPrimitiveTopology() const {
    return indexBuffer->getTopology();
  }

  void setBounds(const BoundingBox &box) { m_bounds = box; }
  const BoundingBox &getBounds() const { return m_bounds; }

private:
  Mesh(Token, VertexBufferSharedPtr vb, IndexBufferSharedPtr ib)
      : vertexBuffer(std::move(vb)), indexBuffer(std::move(ib)) {}

  BoundingBox m_bounds;
};

using MeshSharedPtr = std::shared_ptr<Mesh>;

} // namespace LX_core
