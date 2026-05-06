#include "material_component.hpp"

#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

namespace LX_core {

MaterialComponent::~MaterialComponent() {
  unregisterPassStateListener();
}

void MaterialComponent::setMaterialInstance(MaterialInstanceSharedPtr material) {
  unregisterPassStateListener();
  m_material = std::move(material);
  registerPassStateListener();
  notifyOwnerStructuralChange();
}

void MaterialComponent::onAttached() {
  registerPassStateListener();
}

void MaterialComponent::onDetaching() {
  unregisterPassStateListener();
}

void MaterialComponent::registerPassStateListener() {
  if (!m_material || m_passListenerId != 0) {
    return;
  }
  m_passListenerId = m_material->addPassStateListener([this]() {
    revalidateOwnerForPassChange();
  });
}

void MaterialComponent::unregisterPassStateListener() {
  if (!m_material || m_passListenerId == 0) {
    return;
  }
  m_material->removePassStateListener(m_passListenerId);
  m_passListenerId = 0;
}

void MaterialComponent::revalidateOwnerForPassChange() const {
  auto ownerNode = owner();
  if (!ownerNode.has_value()) {
    return;
  }
  if (auto scene = ownerNode->get().getAttachedScene(); scene && m_material) {
    scene->revalidateNodesUsing(m_material);
    return;
  }
  notifyOwnerStructuralChange();
}

} // namespace LX_core
