#pragma once
#include "../../resources/vertex_buffer.hpp"
#include "../../resources/index_buffer.hpp"
#include "../math/mat.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace LX_core {

template<typename VType>
class Mesh : public IComponent {
public:
  Mesh() = default;

  VertexBufferPtr<VType> vertexBuffer;
  IndexBufferPtr indexBuffer;

  virtual std::vector<IRenderResourcePtr> getRenderResources() const override {
    return {
      std::dynamic_pointer_cast<IRenderResource>(vertexBuffer), 
      std::dynamic_pointer_cast<IRenderResource>(indexBuffer)
    };
  }
};
template<typename VType>
using MeshPtr = std::shared_ptr<Mesh<VType>>;

} // namespace LX_core