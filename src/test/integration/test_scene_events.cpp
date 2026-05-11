#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

struct CapturedEvent final {
  LX_core::SceneEventDomain domain;
  LX_core::SceneEventType type;
  std::string path;
  std::string stableNodeName;
  std::vector<LX_core::SceneNodeAspect> aspects;
  u64 sequence = 0;
};

void testSceneEventHubEmitsSubscribedEvent() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(CapturedEvent{
            .domain = event.domain,
            .type = event.type,
            .path = event.path,
            .stableNodeName = event.stableNodeName,
            .aspects = event.aspects,
            .sequence = event.sequence,
        });
      });

  scene->events().emit(LX_core::SceneEvent{
      .domain = LX_core::SceneEventDomain::Runtime,
      .type = LX_core::SceneEventType::SceneNodeChanged,
      .path = "/helmet",
      .stableNodeName = "helmet",
      .aspects = {LX_core::SceneNodeAspect::Transform},
  });

  EXPECT(events.size() == 1, "manual hub emit should notify one subscriber");
  EXPECT(events.front().domain == LX_core::SceneEventDomain::Runtime,
         "manual emit should preserve runtime-domain event");
  EXPECT(events.front().type == LX_core::SceneEventType::SceneNodeChanged,
         "manual emit should preserve SceneNodeChanged type");
  EXPECT(events.front().path == "/helmet",
         "event should carry current scene path");
  EXPECT(events.front().stableNodeName == "helmet",
         "event should carry stable nodeName");
  EXPECT(events.front().aspects.size() == 1 &&
             events.front().aspects.front() ==
                 LX_core::SceneNodeAspect::Transform,
         "manual emit should preserve Transform aspect");
  EXPECT(events.front().sequence >= 1,
         "event should receive a monotonic sequence");
}

void testSceneEventSubscriptionExpiresWhenSceneDies() {
  LX_core::SceneEventSubscription subscription;
  {
    auto scene = LX_core::Scene::create(nullptr);
    subscription = scene->events().subscribe([](const LX_core::SceneEvent &) {});
    EXPECT(subscription.isActive(),
           "subscription should be active while scene owns the hub");
  }

  EXPECT(!subscription.isActive(),
         "subscription should deactivate after owning scene is destroyed");
  subscription.reset();
  EXPECT(!subscription.isActive(),
         "reset after scene destruction should remain safe and inactive");
}

void testSceneEventSubscribeDuringCallbackDefersToNextEmit() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<std::string> calls;
  LX_core::SceneEventSubscription lateSubscription;

  auto primarySubscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &) {
        calls.push_back("primary");
        if (!lateSubscription.isActive()) {
          lateSubscription = scene->events().subscribe(
              [&](const LX_core::SceneEvent &) { calls.push_back("late"); });
        }
      });

  scene->events().emit(LX_core::SceneEvent{
      .path = "/first",
      .stableNodeName = "first",
  });

  EXPECT(calls.size() == 1 && calls.front() == "primary",
         "listener subscribed during dispatch should not run in the same emit");

  scene->events().emit(LX_core::SceneEvent{
      .path = "/second",
      .stableNodeName = "second",
  });

  EXPECT(calls.size() == 3, "second emit should notify both listeners");
  EXPECT(calls[1] == "primary",
         "existing listener should still run first on later emits");
  EXPECT(calls[2] == "late",
         "late listener should activate on the next emit");
  EXPECT(lateSubscription.isActive(),
         "subscription created during dispatch should remain active");
  EXPECT(primarySubscription.isActive(),
         "original subscription should remain active");
}

void testSceneEventUnsubscribeDuringCallbackStopsLaterEmits() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<std::string> calls;
  LX_core::SceneEventSubscription selfSubscription;

  selfSubscription = scene->events().subscribe([&](const LX_core::SceneEvent &) {
    calls.push_back("self");
    selfSubscription.reset();
  });
  auto peerSubscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &) {
        calls.push_back("peer");
      });

  scene->events().emit(LX_core::SceneEvent{
      .path = "/first",
      .stableNodeName = "first",
  });

  EXPECT(calls.size() == 2,
         "unsubscribe during callback should not break the current dispatch");
  EXPECT(calls[0] == "self", "self-unsubscribing listener should run first");
  EXPECT(calls[1] == "peer", "other listeners should still run in same emit");
  EXPECT(!selfSubscription.isActive(),
         "self-unsubscribing listener should become inactive immediately");
  EXPECT(peerSubscription.isActive(), "peer subscription should remain active");

  scene->events().emit(LX_core::SceneEvent{
      .path = "/second",
      .stableNodeName = "second",
  });

  EXPECT(calls.size() == 3,
         "self-unsubscribed listener should not run on later emits");
  EXPECT(calls[2] == "peer", "remaining listener should still receive later emits");
}

} // namespace

int main() {
  testSceneEventHubEmitsSubscribedEvent();
  testSceneEventSubscriptionExpiresWhenSceneDies();
  testSceneEventSubscribeDuringCallbackDefersToNextEmit();
  testSceneEventUnsubscribeDuringCallbackStopsLaterEmits();

  if (failures != 0) {
    std::cerr << failures << " scene_events test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] scene_events tests passed.\n";
  return 0;
}
