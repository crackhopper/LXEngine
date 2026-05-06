#pragma once

#include <cstddef>
#include <functional>
#include <optional>

namespace LX_core {

class SceneNode;

using ComponentTypeId = std::size_t;

ComponentTypeId nextComponentTypeId();

template <typename T>
ComponentTypeId componentTypeId() {
  static const ComponentTypeId id = nextComponentTypeId();
  return id;
}

class IComponent {
public:
  virtual ~IComponent() = default;

  IComponent(const IComponent &) = delete;
  IComponent &operator=(const IComponent &) = delete;
  IComponent(IComponent &&) = delete;
  IComponent &operator=(IComponent &&) = delete;

  virtual ComponentTypeId getTypeId() const = 0;
  virtual bool affectsRenderableStructure() const { return false; }

  void attachTo(SceneNode &owner);
  void detachFromOwner();

  std::optional<std::reference_wrapper<SceneNode>> owner();
  std::optional<std::reference_wrapper<const SceneNode>> owner() const;

protected:
  IComponent() = default;

  void notifyOwnerStructuralChange() const;
  virtual void onAttached() {}
  virtual void onDetaching() {}

private:
  std::optional<std::reference_wrapper<SceneNode>> m_owner;
};

} // namespace LX_core
