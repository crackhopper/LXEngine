#pragma once
#include "core/math/bounds.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/asset/shader.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace LX_core {

/*
@source_analysis.section GeometryStorage：收束底层 vertex/index 数据
`GeometryStorage` 是场景资源表未来管理几何底层存储的入口。首版仍然复用现有
`IVertexBuffer` 和 `IndexBuffer`，但把“底层存储”和“某个 mesh 使用哪段范围”
拆开，让 realtime renderer、offline renderer 和后续 bindless/packed geometry
可以共享同一份 vertex/index 数据。

它不是新的顶点/索引模型；它只包住现有 buffer 类型。
*/
class GeometryStorage final {
  struct Token {};

public:
  using SharedPtr = std::shared_ptr<GeometryStorage>;

  static SharedPtr create(VertexBufferSharedPtr vb, IndexBufferSharedPtr ib) {
    assert(vb && ib);
    return SharedPtr(new GeometryStorage(Token{}, std::move(vb),
                                         std::move(ib)));
  }

  [[nodiscard]] const VertexBufferSharedPtr &getVertexBuffer() const {
    return m_vertexBuffer;
  }
  [[nodiscard]] const IndexBufferSharedPtr &getIndexBuffer() const {
    return m_indexBuffer;
  }
  [[nodiscard]] const VertexLayout &getVertexLayout() const {
    return m_vertexBuffer->getLayout();
  }
  [[nodiscard]] PrimitiveTopology getTopology() const {
    return m_indexBuffer->getTopology();
  }
  [[nodiscard]] StringID getPipelineSignature() const {
    StringID fields[] = {
        m_vertexBuffer->getPipelineSignature(),
        m_indexBuffer->getPipelineSignature(),
    };
    return GlobalStringTable::get().compose(TypeTag::MeshRender, fields);
  }

private:
  GeometryStorage(Token, VertexBufferSharedPtr vb, IndexBufferSharedPtr ib)
      : m_vertexBuffer(std::move(vb)), m_indexBuffer(std::move(ib)) {}

  VertexBufferSharedPtr m_vertexBuffer;
  IndexBufferSharedPtr m_indexBuffer;
};

using GeometryStorageSharedPtr = GeometryStorage::SharedPtr;

/*
@source_analysis.section MeshBuffer：把 mesh 表达为 GeometryStorage 的切片
`MeshBuffer` 故意不是“顶点数组 + 索引数组 + 一堆绘制状态”的大对象，而是一个很薄的
聚合边界：它引用 `GeometryStorage`，再记录当前 mesh 使用的 vertex/index 范围和
包围盒。

这条边界回答的是“一个可绘制几何体最少需要什么结构事实”：

- 顶点布局是什么
- 图元怎样组装
- CPU / backend 都要面对的原始 buffer 在哪个 `GeometryStorage`
- 这个 mesh 是否是有内部体积的封闭体

材质、shader variant、pass enable 这些都不属于 `Mesh`。这样 mesh 才能被多个材质、
多个 scene node 复用，而不会把几何身份和材质身份混成一个缓存键。
`closedVolume` 也保持在几何层，因为它描述的是 mesh 拓扑语义，不是某个材质参数。
*/
class MeshBuffer {
  struct Token {};

public:
  using SharedPtr = std::shared_ptr<MeshBuffer>;

  static SharedPtr create(VertexBufferSharedPtr vb, IndexBufferSharedPtr ib,
                          BoundingBox bounds = {},
                          bool closedVolume = true) {
    auto storage = GeometryStorage::create(std::move(vb), std::move(ib));
    return create(std::move(storage), 0, 0, std::nullopt, std::nullopt,
                  std::move(bounds), closedVolume);
  }

  static SharedPtr create(GeometryStorageSharedPtr storage, u32 vertexOffset,
                          u32 indexOffset, std::optional<u32> vertexCount,
                          std::optional<u32> indexCount,
                          BoundingBox bounds = {},
                          bool closedVolume = true) {
    assert(storage);
    return SharedPtr(new MeshBuffer(Token{}, std::move(storage), vertexOffset,
                                    indexOffset, vertexCount, indexCount,
                                    std::move(bounds), closedVolume));
  }

/*
@source_analysis.section 几何签名：mesh 只输出 pipeline 真正关心的结构信息
`getPipelineSignature()` 只组合两类东西：

- 顶点输入布局
- 索引拓扑

它们不看顶点个数、索引范围、包围盒，也不看具体字节内容。原因是 pipeline 身份只关心
“这个 draw 需要怎样的 vertex input / primitive assembly 约束”，而不关心这次画了多少个点。

因此 `MeshRender` 更像“几何接口形状”，不是几何数据内容的内容哈希。
*/

  /// Pass 参数保留以统一接口，当前实现忽略。
  /// 未来可用于"同一 mesh 在不同 pass 剔除属性"。
  StringID getPipelineSignature(StringID /*pass*/) const {
    return m_storage->getPipelineSignature();
  }

  const VertexLayout &getVertexLayout() const {
    return m_storage->getVertexLayout();
  }

  [[nodiscard]] const GeometryStorageSharedPtr &getGeometryStorage() const {
    return m_storage;
  }
  [[nodiscard]] const VertexBufferSharedPtr &getVertexBuffer() const {
    return m_storage->getVertexBuffer();
  }
  [[nodiscard]] const IndexBufferSharedPtr &getIndexBuffer() const {
    return m_storage->getIndexBuffer();
  }
  [[nodiscard]] const BoundingBox &getBounds() const { return m_bounds; }
  void setBounds(BoundingBox bounds) { m_bounds = std::move(bounds); }
  [[nodiscard]] u32 getVertexOffset() const { return m_vertexOffset; }
  [[nodiscard]] u32 getIndexOffset() const { return m_indexOffset; }
  [[nodiscard]] u32 getVertexCount() const {
    return m_vertexCount.value_or(
        static_cast<u32>(m_storage->getVertexBuffer()->getVertexCount()));
  }
  [[nodiscard]] u32 getIndexCount() const {
    return m_indexCount.value_or(
        static_cast<u32>(m_storage->getIndexBuffer()->indexCount()));
  }
  [[nodiscard]] bool isClosedVolume() const { return m_closedVolume; }

private:
  MeshBuffer(Token, GeometryStorageSharedPtr storage, u32 vertexOffset,
             u32 indexOffset, std::optional<u32> vertexCount,
             std::optional<u32> indexCount, BoundingBox meshBounds,
             bool closedVolume)
      : m_storage(std::move(storage)), m_vertexOffset(vertexOffset),
        m_indexOffset(indexOffset), m_vertexCount(vertexCount),
        m_indexCount(indexCount), m_bounds(std::move(meshBounds)),
        m_closedVolume(closedVolume) {}

  GeometryStorageSharedPtr m_storage;
  u32 m_vertexOffset = 0;
  u32 m_indexOffset = 0;
  std::optional<u32> m_vertexCount;
  std::optional<u32> m_indexCount;
  BoundingBox m_bounds;
  bool m_closedVolume = true;
};

using Mesh = MeshBuffer;
using MeshBufferSharedPtr = MeshBuffer::SharedPtr;
using MeshSharedPtr = MeshBufferSharedPtr;

/// Builds deterministic line-list indices from triangle-list indices.
/// Input size must be a multiple of 3; otherwise throws std::logic_error.
/// Triangle edges are treated as undirected, canonicalized to ascending
/// endpoints, deduplicated when shared, and emitted in first encounter order
/// for deterministic line-list drawing.
std::vector<u32>
makeUniqueTriangleEdgeLineIndices(const std::vector<u32> &triangleIndices);

} // namespace LX_core
