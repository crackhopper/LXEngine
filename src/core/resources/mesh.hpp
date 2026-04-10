#pragma once
#include "core/math/bounds.hpp"
#include "core/resources/index_buffer.hpp"
#include "core/resources/vertex_buffer.hpp"
#include <cassert>
#include <memory>

namespace LX_core {

/**
 * Type-erased mesh: holds interface-level vertex + index buffers only.
 */
class Mesh {
  struct Token {};

public:
  using Ptr = std::shared_ptr<Mesh>;

  VertexBufferPtr vertexBuffer;
  std::shared_ptr<IndexBuffer> indexBuffer;

  static Ptr create(VertexBufferPtr vb, std::shared_ptr<IndexBuffer> ib) {
    assert(vb && ib);
    return Ptr(new Mesh(Token{}, std::move(vb), std::move(ib)));
  }

  uint32_t getVertexCount() const { return vertexBuffer->getVertexCount(); }
  uint32_t getIndexCount() const {
    return static_cast<uint32_t>(indexBuffer->indexCount());
  }

  size_t getLayoutHash() const {
    size_t hash = vertexBuffer->getLayoutHash();
    hash ^=
        indexBuffer->getLayoutHash() + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    return hash;
  }

  size_t getPipelineHash() const { return getLayoutHash(); }

  const VertexLayout &getVertexLayout() const {
    return vertexBuffer->getLayout();
  }
  PrimitiveTopology getPrimitiveTopology() const {
    return indexBuffer->getTopology();
  }

  void setBounds(const BoundingBox &box) { m_bounds = box; }
  const BoundingBox &getBounds() const { return m_bounds; }

private:
  Mesh(Token, VertexBufferPtr vb, std::shared_ptr<IndexBuffer> ib)
      : vertexBuffer(std::move(vb)), indexBuffer(std::move(ib)) {}

  BoundingBox m_bounds;
};

using MeshPtr = std::shared_ptr<Mesh>;

} // namespace LX_core
