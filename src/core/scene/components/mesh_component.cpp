#include "mesh_component.hpp"

namespace LX_core {

void MeshComponent::setMesh(MeshSharedPtr mesh) {
  m_pendingMesh = std::move(mesh);
  notifyOwnerStructuralChange();
}

} // namespace LX_core
