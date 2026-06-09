#include "material_component.hpp"

#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <stdexcept>

namespace LX_core {

MaterialComponent::MaterialComponent(std::string tag,
                                     MaterialInstanceSharedPtr material) {
  if (tag.empty()) {
    throw std::logic_error("MaterialComponent tag must not be empty");
  }
  m_pendingMaterial = std::move(material);
  m_activeMaterialTag = std::move(tag);
  m_pendingMaterialsByTag.emplace(m_activeMaterialTag, m_pendingMaterial);
}

MaterialComponent::~MaterialComponent() {
  unregisterPassStateListener();
}

void MaterialComponent::setMaterialInstance(MaterialInstanceSharedPtr material) {
  unregisterPassStateListener();
  m_pendingMaterial = std::move(material);
  m_pendingMaterialsByTag.clear();
  m_materialHandlesByTag.clear();
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
  const bool shouldActivate =
      m_activeMaterialTag.empty() ||
      (!m_pendingMaterial && !m_materialHandle.isValid());
  m_pendingMaterialsByTag[std::move(tag)] = std::move(material);
  m_materialHandlesByTag.erase(insertedTag);
  if (shouldActivate) {
    (void)setActiveMaterialTag(insertedTag);
    return;
  }
  notifyOwnerStructuralChange();
}

void MaterialComponent::setTaggedMaterialHandle(std::string tag,
                                                MaterialHandle handle) {
  if (tag.empty()) {
    throw std::logic_error("MaterialComponent tag must not be empty");
  }
  const bool shouldActivate = m_activeMaterialTag == tag;
  m_materialHandlesByTag[std::move(tag)] = handle;
  if (shouldActivate) {
    m_materialHandle = handle;
  }
}

void MaterialComponent::clearPendingMaterials() {
  m_pendingMaterial.reset();
  m_pendingMaterialsByTag.clear();
  unregisterPassStateListener();
}

bool MaterialComponent::setActiveMaterialTag(const std::string &tag) {
  const auto pendingIt = m_pendingMaterialsByTag.find(tag);
  const auto handleIt = m_materialHandlesByTag.find(tag);
  unregisterPassStateListener();
  m_activeMaterialTag = tag;
  m_pendingMaterial = pendingIt == m_pendingMaterialsByTag.end()
                          ? MaterialInstanceSharedPtr{}
                          : pendingIt->second;
  m_materialHandle = handleIt == m_materialHandlesByTag.end()
                         ? MaterialHandle{}
                         : handleIt->second;
  registerPassStateListener();
  notifyOwnerStructuralChange();
  return pendingIt != m_pendingMaterialsByTag.end() ||
         handleIt != m_materialHandlesByTag.end();
}

bool MaterialComponent::hasMaterialTag(const std::string &tag) const {
  return m_pendingMaterialsByTag.find(tag) != m_pendingMaterialsByTag.end() ||
         m_materialHandlesByTag.find(tag) != m_materialHandlesByTag.end();
}

MaterialHandle
MaterialComponent::getMaterialHandleForTag(const std::string &tag) const {
  const auto it = m_materialHandlesByTag.find(tag);
  return it == m_materialHandlesByTag.end() ? MaterialHandle{} : it->second;
}

void MaterialComponent::forEachPendingMaterial(
    const std::function<void(const std::string &,
                             const MaterialInstanceSharedPtr &)> &callback)
    const {
  if (!callback) {
    return;
  }
  if (m_pendingMaterialsByTag.empty()) {
    callback(std::string{}, m_pendingMaterial);
    return;
  }
  for (const auto &[tag, material] : m_pendingMaterialsByTag) {
    callback(tag, material);
  }
}

void MaterialComponent::forEachMaterialHandle(
    const std::function<void(const std::string &, MaterialHandle)> &callback)
    const {
  if (!callback) {
    return;
  }
  if (m_materialHandlesByTag.empty()) {
    callback(std::string{}, m_materialHandle);
    return;
  }
  for (const auto &[tag, handle] : m_materialHandlesByTag) {
    callback(tag, handle);
  }
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
