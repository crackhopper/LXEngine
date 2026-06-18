#include "material_component.hpp"

#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

namespace LX_core {

MaterialComponent::~MaterialComponent() {
  unregisterPassStateListener();
}

void MaterialComponent::setMaterialInstance(MaterialInstanceSharedPtr material) {
  unregisterPassStateListener();
  m_pendingMaterial = std::move(material);
  registerPassStateListener();
  notifyOwnerStructuralChange();
}

void MaterialComponent::clearPendingMaterials() {
  m_pendingMaterial.reset();
  unregisterPassStateListener();
}

void MaterialComponent::forEachPendingMaterial(
    const std::function<void(const std::string &,
                             const MaterialInstanceSharedPtr &)> &callback)
    const {
  if (!callback) {
    return;
  }
  callback(std::string{}, m_pendingMaterial);
}

void MaterialComponent::forEachMaterialHandle(
    const std::function<void(const std::string &, MaterialHandle)> &callback)
    const {
  if (!callback) {
    return;
  }
  callback(std::string{}, m_materialHandle);
}

void MaterialComponent::onAttached() {
  registerPassStateListener();
}

void MaterialComponent::onDetaching() {
  unregisterPassStateListener();
}

void MaterialComponent::registerPassStateListener() {
  if (!m_pendingMaterial || m_passListenerId != 0) {
    return;
  }
  m_passListenerId = m_pendingMaterial->addPassStateListener([this]() {
    revalidateOwnerForPassChange();
  });
}

void MaterialComponent::unregisterPassStateListener() {
  if (!m_pendingMaterial || m_passListenerId == 0) {
    return;
  }
  m_pendingMaterial->removePassStateListener(m_passListenerId);
  m_passListenerId = 0;
}

void MaterialComponent::revalidateOwnerForPassChange() const {
  auto ownerNode = owner();
  if (!ownerNode.has_value()) {
    return;
  }
  notifyOwnerStructuralChange();
}

} // namespace LX_core
