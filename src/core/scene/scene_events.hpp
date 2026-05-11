#pragma once

#include "core/platform/types.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace LX_core {

enum class SceneEventDomain {
  Runtime,
  Editor,
};

enum class SceneEventType {
  SceneNodeChanged,
  SceneNodeAdded,
  SceneNodeRemoved,
};

enum class SceneNodeAspect {
  Transform,
  Identity,
  Hierarchy,
  Visibility,
  RenderableStructure,
  CameraProperties,
  LightProperties,
};

struct SceneEvent final {
  SceneEventDomain domain = SceneEventDomain::Runtime;
  SceneEventType type = SceneEventType::SceneNodeChanged;
  u64 sequence = 0;
  std::string path;
  std::string stableNodeName;
  std::vector<SceneNodeAspect> aspects;
};

class SceneEventHub;
struct SceneEventHubState;

class SceneEventSubscription final {
public:
  SceneEventSubscription() = default;
  SceneEventSubscription(const SceneEventSubscription &) = delete;
  SceneEventSubscription &operator=(const SceneEventSubscription &) = delete;
  SceneEventSubscription(SceneEventSubscription &&other) noexcept;
  SceneEventSubscription &operator=(SceneEventSubscription &&other) noexcept;
  ~SceneEventSubscription();

  [[nodiscard]] bool isActive() const { return !m_state.expired(); }
  void reset();

private:
  friend class SceneEventHub;
  SceneEventSubscription(std::weak_ptr<SceneEventHubState> state,
                         u64 subscriptionId);

  std::weak_ptr<SceneEventHubState> m_state;
  u64 m_subscriptionId = 0;
};

class SceneEventHub final {
public:
  using Listener = std::function<void(const SceneEvent &)>;

  SceneEventHub();
  ~SceneEventHub();
  SceneEventHub(const SceneEventHub &) = delete;
  SceneEventHub &operator=(const SceneEventHub &) = delete;
  SceneEventHub(SceneEventHub &&) = delete;
  SceneEventHub &operator=(SceneEventHub &&) = delete;

  [[nodiscard]] SceneEventSubscription subscribe(Listener listener);
  void emit(SceneEvent event);

private:
  std::shared_ptr<SceneEventHubState> m_state;
};

} // namespace LX_core
