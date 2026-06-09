#pragma once

#include "core/asset/mesh.hpp"
#include "core/scene/component.hpp"
#include "core/scene/scene_resource_table.hpp"

namespace LX_core {

class MeshComponent final : public IRenderableComponent {
public:
  explicit MeshComponent(MeshSharedPtr mesh)
      : m_pendingMesh(std::move(mesh)) {}

  ComponentTypeId getTypeId() const override {
    return componentTypeId<MeshComponent>();
  }

  const MeshSharedPtr &getPendingMesh() const { return m_pendingMesh; }
  void setMesh(MeshSharedPtr mesh);
  void clearPendingMesh() { m_pendingMesh.reset(); }
  [[nodiscard]] GeometryStorageHandle getGeometryStorageHandle() const {
    return m_geometryStorageHandle;
  }
  void setGeometryStorageHandle(GeometryStorageHandle handle) {
    m_geometryStorageHandle = handle;
  }
  [[nodiscard]] MeshHandle getMeshHandle() const { return m_meshHandle; }
  void setMeshHandle(MeshHandle handle) { m_meshHandle = handle; }
  [[nodiscard]] ObjectHandle getObjectHandle() const override {
    return m_objectHandle;
  }
  void setObjectHandle(ObjectHandle handle) override {
    m_objectHandle = handle;
  }

private:
  MeshSharedPtr m_pendingMesh;
  GeometryStorageHandle m_geometryStorageHandle;
  MeshHandle m_meshHandle;
  ObjectHandle m_objectHandle;
};

} // namespace LX_core
