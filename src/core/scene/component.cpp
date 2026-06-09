#include "component.hpp"

#include "object.hpp"
#include "scene.hpp"
#include "scene_events.hpp"

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
  m_owner->get().emitRuntimeNodeChanged(SceneNodeAspect::RenderableStructure);
}

void IComponent::notifyOwnerRuntimeAspectChange(const SceneNodeAspect aspect) const {
  const auto ownerNode = owner();
  if (!ownerNode.has_value()) {
    return;
  }

  if (const auto scene = ownerNode->get().getAttachedScene()) {
    scene->events().emit(SceneEvent{
        .domain = SceneEventDomain::Runtime,
        .type = SceneEventType::SceneNodeChanged,
        .path = ownerNode->get().getPath(),
        .stableNodeName = ownerNode->get().getNodeName(),
        .aspects = {aspect},
    });
  }
}

} // namespace LX_core
