#include "material_component.hpp"

#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <stdexcept>

namespace LX_core {

MaterialComponent::~MaterialComponent() {
  unregisterPassStateListener();
}

void MaterialComponent::setMaterialInstance(MaterialInstanceSharedPtr material) {
  unregisterPassStateListener();
  m_material = std::move(material);
  m_materialsByTag.clear();
  m_activeMaterialTag.clear();
  m_materialHandle = {};
  registerPassStateListener();
  notifyOwnerStructuralChange();
}

void MaterialComponent::setTaggedMaterial(std::string tag,
                                          MaterialInstanceSharedPtr material) {
  if (tag.empty()) {
    throw std::logic_error("MaterialComponent tag must not be empty");
  }
  const std::string insertedTag = tag;
  const bool shouldActivate = m_activeMaterialTag.empty() || !m_material;
  m_materialsByTag[std::move(tag)] = std::move(material);
  if (shouldActivate) {
    (void)setActiveMaterialTag(insertedTag);
    return;
  }
  notifyOwnerStructuralChange();
}

bool MaterialComponent::setActiveMaterialTag(const std::string &tag) {
  const auto it = m_materialsByTag.find(tag);
  unregisterPassStateListener();
  m_activeMaterialTag = tag;
  m_material = it == m_materialsByTag.end() ? MaterialInstanceSharedPtr{}
                                            : it->second;
  m_materialHandle = {};
  registerPassStateListener();
  notifyOwnerStructuralChange();
  return it != m_materialsByTag.end();
}

bool MaterialComponent::hasMaterialTag(const std::string &tag) const {
  return m_materialsByTag.find(tag) != m_materialsByTag.end();
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
