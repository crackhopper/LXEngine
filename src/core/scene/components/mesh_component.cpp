#include "mesh_component.hpp"

namespace LX_core {

void MeshComponent::setMesh(MeshSharedPtr mesh) {
  m_pendingMesh = std::move(mesh);
  m_geometryStorageHandle = {};
  m_meshHandle = {};
  m_objectHandle = {};
  notifyOwnerStructuralChange();
}

} // namespace LX_core
