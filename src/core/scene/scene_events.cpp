#include "core/scene/scene_events.hpp"

#include <algorithm>
#include <utility>

namespace LX_core {

struct SceneEventHubState final {
  using Listener = SceneEventHub::Listener;

  struct ListenerEntry final {
    u64 id = 0;
    Listener callback;
    bool removed = false;
  };

  void compactRemovedListeners() {
    listeners.erase(
        std::remove_if(
            listeners.begin(), listeners.end(),
            [](const ListenerEntry &candidate) { return candidate.removed; }),
        listeners.end());
  }

  void flushPendingListeners() {
    if (pendingListeners.empty()) {
      return;
    }

    for (auto &entry : pendingListeners) {
      if (!entry.removed) {
        listeners.push_back(std::move(entry));
      }
    }
    pendingListeners.clear();
  }

  void unsubscribe(const u64 subscriptionId) {
    for (auto &entry : listeners) {
      if (entry.id != subscriptionId) {
        continue;
      }

      entry.removed = true;
      if (dispatchDepth == 0) {
        compactRemovedListeners();
      }
      return;
    }

    for (auto &entry : pendingListeners) {
      if (entry.id == subscriptionId) {
        entry.removed = true;
        return;
      }
    }
  }

  std::vector<ListenerEntry> listeners;
  std::vector<ListenerEntry> pendingListeners;
  u64 nextSubscriptionId = 1;
  u64 nextSequence = 1;
  usize dispatchDepth = 0;
};

SceneEventSubscription::SceneEventSubscription(std::weak_ptr<SceneEventHubState> state,
                                               const u64 subscriptionId)
    : m_state(std::move(state)), m_subscriptionId(subscriptionId) {}

SceneEventSubscription::SceneEventSubscription(
    SceneEventSubscription &&other) noexcept
    : m_state(std::move(other.m_state)), m_subscriptionId(other.m_subscriptionId) {
  other.m_subscriptionId = 0;
}

SceneEventSubscription &
SceneEventSubscription::operator=(SceneEventSubscription &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  reset();
  m_state = std::move(other.m_state);
  m_subscriptionId = other.m_subscriptionId;
  other.m_subscriptionId = 0;
  return *this;
}

SceneEventSubscription::~SceneEventSubscription() { reset(); }

void SceneEventSubscription::reset() {
  if (const auto state = m_state.lock()) {
    state->unsubscribe(m_subscriptionId);
  }
  m_state.reset();
  m_subscriptionId = 0;
}

SceneEventHub::SceneEventHub() : m_state(std::make_shared<SceneEventHubState>()) {}

SceneEventHub::~SceneEventHub() = default;

SceneEventSubscription SceneEventHub::subscribe(Listener listener) {
  const u64 subscriptionId = m_state->nextSubscriptionId++;
  auto entry = SceneEventHubState::ListenerEntry{
      .id = subscriptionId,
      .callback = std::move(listener),
      .removed = false,
  };
  if (m_state->dispatchDepth == 0) {
    m_state->listeners.push_back(std::move(entry));
  } else {
    m_state->pendingListeners.push_back(std::move(entry));
  }
  return SceneEventSubscription(m_state, subscriptionId);
}

void SceneEventHub::emit(SceneEvent event) {
  event.sequence = m_state->nextSequence++;

  ++m_state->dispatchDepth;
  for (auto &entry : m_state->listeners) {
    if (!entry.removed && entry.callback) {
      entry.callback(event);
    }
  }
  --m_state->dispatchDepth;

  if (m_state->dispatchDepth == 0) {
    m_state->compactRemovedListeners();
    m_state->flushPendingListeners();
  }
}

} // namespace LX_core
