#pragma once
#include "core/scene/components/base.hpp"
#include "core/math/mat.hpp"
#include "core/resources/index_buffer.hpp"
#include "core/resources/vertex_buffer.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace LX_core {

template <typename VType> class Mesh : public IComponent {
  struct Token {};

public:
  using Ptr = std::shared_ptr<Mesh<VType>>;

  Mesh(Token, std::shared_ptr<VertexBuffer<VType>> vertexBuffer,
       std::shared_ptr<IndexBuffer> indexBuffer)
      : vertexBuffer(vertexBuffer), indexBuffer(indexBuffer) {}

  static auto create(std::shared_ptr<VertexBuffer<VType>> vertexBuffer,
                    std::shared_ptr<IndexBuffer> indexBuffer) {
    return std::make_shared<Mesh>(Token(), vertexBuffer, indexBuffer);
  }

  std::shared_ptr<VertexBuffer<VType>> vertexBuffer;
  std::shared_ptr<IndexBuffer> indexBuffer;

  virtual std::vector<IRenderResourcePtr> getRenderResources() const override {
    return {std::dynamic_pointer_cast<IRenderResource>(vertexBuffer),
            std::dynamic_pointer_cast<IRenderResource>(indexBuffer)};
  }
};
template <typename VType> using MeshPtr = std::shared_ptr<Mesh<VType>>;

} // namespace LX_core