#include "component.hpp"

#include "object.hpp"

#include <atomic>
#include <cassert>

namespace LX_core {

namespace {

std::atomic<ComponentTypeId> g_nextComponentTypeId{1};

} // namespace

ComponentTypeId nextComponentTypeId() {
  return g_nextComponentTypeId.fetch_add(1, std::memory_order_relaxed);
}

void IComponent::attachTo(SceneNode &owner) {
  assert(!m_owner.has_value() && "component already attached to a SceneNode");
  m_owner = std::ref(owner);
  onAttached();
}

void IComponent::detachFromOwner() {
  if (!m_owner.has_value()) {
    return;
  }
  onDetaching();
  m_owner.reset();
}

std::optional<std::reference_wrapper<SceneNode>> IComponent::owner() {
  return m_owner;
}

std::optional<std::reference_wrapper<const SceneNode>> IComponent::owner() const {
  if (!m_owner.has_value()) {
    return std::nullopt;
  }
  return std::cref(m_owner->get());
}

void IComponent::notifyOwnerStructuralChange() const {
  if (!m_owner.has_value()) {
    return;
  }
  m_owner->get().rebuildValidatedCache();
}

} // namespace LX_core
