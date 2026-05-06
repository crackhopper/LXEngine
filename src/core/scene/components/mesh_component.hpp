#pragma once

#include "core/asset/mesh.hpp"
#include "core/scene/component.hpp"

namespace LX_core {

class MeshComponent final : public IComponent {
public:
  explicit MeshComponent(MeshSharedPtr mesh) : m_mesh(std::move(mesh)) {}

  ComponentTypeId getTypeId() const override {
    return componentTypeId<MeshComponent>();
  }
  bool affectsRenderableStructure() const override { return true; }

  const MeshSharedPtr &getMesh() const { return m_mesh; }
  void setMesh(MeshSharedPtr mesh);

private:
  MeshSharedPtr m_mesh;
};

} // namespace LX_core
