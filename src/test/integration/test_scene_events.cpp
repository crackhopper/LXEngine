#include "core/scene/object.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/scene.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
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

CapturedEvent captureEvent(const LX_core::SceneEvent &event) {
  return CapturedEvent{
      .domain = event.domain,
      .type = event.type,
      .path = event.path,
      .stableNodeName = event.stableNodeName,
      .aspects = event.aspects,
      .sequence = event.sequence,
  };
}

usize countEventsWithType(const std::vector<CapturedEvent> &events,
                          const LX_core::SceneEventType type) {
  usize count = 0;
  for (const auto &event : events) {
    if (event.type == type) {
      ++count;
    }
  }
  return count;
}

usize countChangedEventsWithAspect(const std::vector<CapturedEvent> &events,
                                   const LX_core::SceneNodeAspect aspect) {
  usize count = 0;
  for (const auto &event : events) {
    if (event.type != LX_core::SceneEventType::SceneNodeChanged) {
      continue;
    }
    if (event.aspects.size() == 1 && event.aspects.front() == aspect) {
      ++count;
    }
  }
  return count;
}

const CapturedEvent *findFirstEventWithType(const std::vector<CapturedEvent> &events,
                                            const LX_core::SceneEventType type) {
  for (const auto &event : events) {
    if (event.type == type) {
      return &event;
    }
  }
  return nullptr;
}

void expectRuntimeChangedEvent(const CapturedEvent &event,
                               const std::string &expectedPath,
                               const std::string &expectedStableNodeName,
                               const LX_core::SceneNodeAspect expectedAspect) {
  EXPECT(event.domain == LX_core::SceneEventDomain::Runtime,
         "node mutation should emit runtime-domain events");
  EXPECT(event.type == LX_core::SceneEventType::SceneNodeChanged,
         "node mutation should emit SceneNodeChanged");
  EXPECT(event.path == expectedPath, "node mutation should emit current path");
  EXPECT(event.stableNodeName == expectedStableNodeName,
         "node mutation should emit stable nodeName");
  EXPECT(event.aspects.size() == 1 && event.aspects.front() == expectedAspect,
         "node mutation should emit the expected single aspect");
  EXPECT(event.sequence >= 1, "node mutation event should receive a sequence");
}

void testSceneEventHubEmitsSubscribedEvent() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(captureEvent(event));
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

void testDetachedNodeMutationDoesNotEmitRuntimeSceneEvents() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(captureEvent(event));
      });

  auto detachedNode = LX_core::SceneNode::create("detached_node");
  detachedNode->setName("detached");
  detachedNode->setTranslation({1.0f, 2.0f, 3.0f});
  detachedNode->setRotation(LX_core::Quatf{});
  detachedNode->setScale({2.0f, 2.0f, 2.0f});
  detachedNode->setVisibilityLayerMask(0x2u);

  EXPECT(events.empty(),
         "detached nodes should remain usable without emitting scene events");
}

void testAttachedNodeTransformMutationsEmitRuntimeSceneNodeChangedEvents() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(captureEvent(event));
      });

  auto node = LX_core::SceneNode::create("helmet_node");
  node->setName("helmet");
  scene->addRenderable(node);
  events.clear();

  node->setLocalTransform(LX_core::Transform{
      .translation = {1.0f, 2.0f, 3.0f},
      .rotation = LX_core::Quatf{},
      .scale = {1.0f, 1.0f, 1.0f},
  });
  node->setTranslation({4.0f, 5.0f, 6.0f});
  node->setRotation(LX_core::Quatf{});
  node->setScale({2.0f, 3.0f, 4.0f});

  EXPECT(events.size() == 4,
         "each transform write point should emit one runtime node-changed event");
  EXPECT(events[0].sequence < events[1].sequence &&
             events[1].sequence < events[2].sequence &&
             events[2].sequence < events[3].sequence,
         "transform events should keep hub sequencing monotonic");
  expectRuntimeChangedEvent(events[0], "/helmet", "helmet_node",
                            LX_core::SceneNodeAspect::Transform);
  expectRuntimeChangedEvent(events[1], "/helmet", "helmet_node",
                            LX_core::SceneNodeAspect::Transform);
  expectRuntimeChangedEvent(events[2], "/helmet", "helmet_node",
                            LX_core::SceneNodeAspect::Transform);
  expectRuntimeChangedEvent(events[3], "/helmet", "helmet_node",
                            LX_core::SceneNodeAspect::Transform);
}

void testAttachedNodeIdentityAndVisibilityMutationsEmitRuntimeEvents() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(captureEvent(event));
      });

  auto node = LX_core::SceneNode::create("helmet_node");
  node->setName("helmet");
  scene->addRenderable(node);
  events.clear();

  node->setName("helmet_01");
  node->setVisibilityLayerMask(0x4u);

  EXPECT(events.size() == 2,
         "identity and visibility writes should each emit one event");
  expectRuntimeChangedEvent(events[0], "/helmet_01", "helmet_node",
                            LX_core::SceneNodeAspect::Identity);
  expectRuntimeChangedEvent(events[1], "/helmet_01", "helmet_node",
                            LX_core::SceneNodeAspect::Visibility);
}

void testAttachedNodeHierarchyMutationsEmitRuntimeEvents() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(captureEvent(event));
      });

  auto parent = LX_core::SceneNode::create("parent_node");
  parent->setName("parent");
  scene->addRenderable(parent);
  auto child = LX_core::SceneNode::create("child_node");
  child->setName("child");
  scene->addRenderable(child);
  events.clear();

  child->setParent(parent);
  child->clearParent();

  EXPECT(events.size() == 2,
         "setParent and clearParent should each emit one hierarchy event");
  expectRuntimeChangedEvent(events[0], "/parent/child", "child_node",
                            LX_core::SceneNodeAspect::Hierarchy);
  expectRuntimeChangedEvent(events[1], "/child", "child_node",
                            LX_core::SceneNodeAspect::Hierarchy);
}

void testCameraComponentPropertySettersEmitRuntimeEvents() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(captureEvent(event));
      });

  auto cameraNode = LX_core::SceneNode::create("camera_node");
  cameraNode->setName("camera");
  auto camera = cameraNode->addComponent<LX_core::CameraComponent>();
  EXPECT(camera.has_value(), "camera component should exist");
  if (!camera.has_value()) {
    return;
  }

  scene->addCamera(cameraNode);
  events.clear();

  camera->get().setFovY(75.0f);
  camera->get().setNearPlane(0.5f);
  camera->get().setFarPlane(250.0f);
  camera->get().setProjectionType(LX_core::CameraType::Orthographic);
  camera->get().setCullingMask(0x0Fu);

  EXPECT(countChangedEventsWithAspect(events,
                                      LX_core::SceneNodeAspect::CameraProperties) == 5,
         "camera property setters should each emit CameraProperties events");
}

void testDirectionalLightPropertySettersEmitRuntimeEvents() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(captureEvent(event));
      });

  auto lightNode = LX_core::SceneNode::create("light_node");
  lightNode->setName("sun");
  scene->addRenderable(lightNode);

  auto light = std::make_shared<LX_core::DirectionalLight>();
  scene->attachLight(lightNode, light);
  events.clear();

  const auto sceneLight = scene->getLight(*lightNode);
  EXPECT(sceneLight.has_value(),
         "attached light should be resolved through scene resource table");
  if (!sceneLight.has_value()) {
    return;
  }
  auto &directionalLight =
      static_cast<LX_core::DirectionalLight &>(sceneLight->get());
  directionalLight.setDirection({0.0f, -1.0f, 0.0f});
  directionalLight.setColor({0.2f, 0.4f, 0.6f});
  directionalLight.setIntensity(3.5f);

  const auto &directionalUbo = directionalLight.getDirectionalUBO();
  EXPECT(directionalUbo.param.dir.x == 0.0f &&
             directionalUbo.param.dir.y == -1.0f &&
             directionalUbo.param.dir.z == 0.0f,
         "typed directional light UBO accessor should expose latest direction");
  EXPECT(directionalUbo.param.color.x == 0.2f &&
             directionalUbo.param.color.y == 0.4f &&
             directionalUbo.param.color.z == 0.6f &&
             directionalUbo.param.color.w == 3.5f,
         "typed directional light UBO accessor should expose latest color and intensity");

  EXPECT(countChangedEventsWithAspect(events,
                                      LX_core::SceneNodeAspect::LightProperties) == 3,
         "light property setters should each emit LightProperties events");
}

void testSceneAddRemoveLifecycleEmitsRuntimeNodeAddedAndRemovedEvents() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  LX_core::SceneNode::SharedPtr node;
  bool removedCallbackObservedDetachedState = false;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        if (event.type == LX_core::SceneEventType::SceneNodeRemoved) {
          removedCallbackObservedDetachedState =
              !node->getAttachedScene() && node->getPath() == "/helmet";
          scene->removeRenderable(node);
        }
        events.push_back(captureEvent(event));
      });

  node = LX_core::SceneNode::create("helmet_node");
  node->setName("helmet");

  scene->addRenderable(node);

  const CapturedEvent *addedEvent = findFirstEventWithType(
      events, LX_core::SceneEventType::SceneNodeAdded);
  EXPECT(addedEvent != nullptr,
         "adding a renderable SceneNode should emit SceneNodeAdded");
  if (addedEvent) {
    EXPECT(addedEvent->domain == LX_core::SceneEventDomain::Runtime,
           "SceneNodeAdded should be a runtime event");
    EXPECT(addedEvent->path == "/helmet",
           "SceneNodeAdded should report the node path");
    EXPECT(addedEvent->stableNodeName == "helmet_node",
           "SceneNodeAdded should report stable nodeName");
    EXPECT(addedEvent->aspects.empty(),
           "SceneNodeAdded should not report change aspects");
  }
  EXPECT(countEventsWithType(events, LX_core::SceneEventType::SceneNodeAdded) == 1,
         "explicit add should emit exactly one SceneNodeAdded event");
  EXPECT(countChangedEventsWithAspect(events, LX_core::SceneNodeAspect::Hierarchy) == 0,
         "explicit add should not leak a hierarchy-changed event from root attachment");

  events.clear();
  scene->removeRenderable(node);

  const CapturedEvent *removedEvent = findFirstEventWithType(
      events, LX_core::SceneEventType::SceneNodeRemoved);
  EXPECT(removedEvent != nullptr,
         "removing a renderable SceneNode should emit SceneNodeRemoved");
  if (removedEvent) {
    EXPECT(removedEvent->domain == LX_core::SceneEventDomain::Runtime,
           "SceneNodeRemoved should be a runtime event");
    EXPECT(removedEvent->path == "/helmet",
           "SceneNodeRemoved should report the last attached node path");
    EXPECT(removedEvent->stableNodeName == "helmet_node",
           "SceneNodeRemoved should report stable nodeName");
    EXPECT(removedEvent->aspects.empty(),
           "SceneNodeRemoved should not report change aspects");
  }

  EXPECT(countEventsWithType(events, LX_core::SceneEventType::SceneNodeRemoved) == 1,
         "explicit removal should emit exactly one SceneNodeRemoved event");
  EXPECT(countChangedEventsWithAspect(events, LX_core::SceneNodeAspect::Hierarchy) == 0,
         "explicit removal should not leak a hierarchy-changed event from internal detach");
  EXPECT(removedCallbackObservedDetachedState,
         "SceneNodeRemoved should fire only after the node is observably detached");
}

void testSceneRemoveChildUsesLastAttachedPathWithoutHierarchyNoise() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(captureEvent(event));
      });

  auto parent = LX_core::SceneNode::create("parent_node");
  parent->setName("parent");
  scene->addRenderable(parent);
  auto child = LX_core::SceneNode::create("child_node");
  child->setName("child");
  scene->addRenderable(child);
  events.clear();

  child->setParent(parent);
  events.clear();

  scene->removeRenderable(child);

  EXPECT(countEventsWithType(events, LX_core::SceneEventType::SceneNodeRemoved) == 1,
         "removing a child node should emit exactly one SceneNodeRemoved event");
  EXPECT(countChangedEventsWithAspect(events, LX_core::SceneNodeAspect::Hierarchy) == 0,
         "removing a child node should not leak hierarchy-changed events");
  const CapturedEvent *removedEvent = findFirstEventWithType(
      events, LX_core::SceneEventType::SceneNodeRemoved);
  EXPECT(removedEvent != nullptr,
         "removing a child node should emit SceneNodeRemoved");
  if (removedEvent) {
    EXPECT(removedEvent->path == "/parent/child",
           "child removal should report the last attached path before clearParent");
    EXPECT(removedEvent->stableNodeName == "child_node",
           "child removal should preserve stable nodeName");
  }
}

void testSceneRemoveSubtreeEmitsRemovalForEachRemovedNodeWithoutHierarchyNoise() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(captureEvent(event));
      });

  auto parent = LX_core::SceneNode::create("parent_node");
  parent->setName("parent");
  scene->addRenderable(parent);

  auto child = LX_core::SceneNode::create("child_node");
  child->setName("child");
  scene->addRenderable(child);
  child->setParent(parent);

  auto grandchild = LX_core::SceneNode::create("grandchild_node");
  grandchild->setName("grandchild");
  scene->addRenderable(grandchild);
  grandchild->setParent(child);

  auto cameraNode = LX_core::SceneNode::create("camera_node");
  cameraNode->setName("subtree_camera");
  cameraNode->addComponent<LX_core::CameraComponent>();
  scene->addCamera(cameraNode);
  cameraNode->setParent(child);
  events.clear();

  scene->removeRenderable(child);

  EXPECT(events.size() == 3,
         "subtree removal should emit only the expected removal lifecycle events");
  EXPECT(countEventsWithType(events, LX_core::SceneEventType::SceneNodeRemoved) == 3,
         "subtree removal should emit one SceneNodeRemoved event per removed node");
  EXPECT(countChangedEventsWithAspect(events, LX_core::SceneNodeAspect::Hierarchy) == 0,
         "subtree removal should not leak hierarchy-changed events");

  bool sawChild = false;
  bool sawGrandchild = false;
  bool sawCamera = false;
  for (const auto &event : events) {
    EXPECT(event.type == LX_core::SceneEventType::SceneNodeRemoved,
           "subtree lifecycle events should all be removals");
    if (event.path == "/parent/child") {
      sawChild = true;
    } else if (event.path == "/parent/child/grandchild") {
      sawGrandchild = true;
    } else if (event.path == "/parent/child/subtree_camera") {
      sawCamera = true;
    }
  }

  EXPECT(sawChild, "subtree removal should report the removed root path");
  EXPECT(sawGrandchild, "subtree removal should report mesh descendant path");
  EXPECT(sawCamera, "subtree removal should report camera descendant path");
}

void testSceneTeardownDoesNotEmitExplicitRemoveLifecycleEvents() {
  std::vector<CapturedEvent> events;
  LX_core::SceneEventSubscription subscription;
  {
    auto scene = LX_core::Scene::create(nullptr);
    subscription = scene->events().subscribe([&](const LX_core::SceneEvent &event) {
      events.push_back(captureEvent(event));
    });

    auto node = LX_core::SceneNode::create("helmet_node");
    node->setName("helmet");
    scene->addRenderable(node);
    events.clear();
  }

  EXPECT(events.empty(),
         "scene teardown should not emit explicit remove-lifecycle events");
}

void testSceneRemoveRenderableIgnoresForeignAndDetachedNodes() {
  auto sceneA = LX_core::Scene::create("SceneA", nullptr);
  auto sceneB = LX_core::Scene::create("SceneB", nullptr);
  std::vector<CapturedEvent> eventsA;
  std::vector<CapturedEvent> eventsB;
  auto subscriptionA =
      sceneA->events().subscribe([&](const LX_core::SceneEvent &event) {
        eventsA.push_back(captureEvent(event));
      });
  auto subscriptionB =
      sceneB->events().subscribe([&](const LX_core::SceneEvent &event) {
        eventsB.push_back(captureEvent(event));
      });

  auto foreignNode = LX_core::SceneNode::create("foreign_node");
  foreignNode->setName("foreign");
  sceneB->addRenderable(foreignNode);
  eventsA.clear();
  eventsB.clear();

  sceneA->removeRenderable(foreignNode);

  EXPECT(eventsA.empty(),
         "removeRenderable should ignore nodes owned by another scene");
  EXPECT(eventsB.empty(),
         "removeRenderable misuse should not emit events into the foreign scene");
  EXPECT(foreignNode->getAttachedScene() == sceneB,
         "removeRenderable misuse should not detach a foreign node");
  EXPECT(foreignNode->getPath() == "/foreign",
         "removeRenderable misuse should not mutate a foreign node path");

  sceneB->removeRenderable(foreignNode);
  eventsB.clear();

  sceneA->removeRenderable(foreignNode);

  EXPECT(eventsA.empty(),
         "removeRenderable should ignore already-detached nodes");
  EXPECT(eventsB.empty(),
         "removeRenderable on an already-detached node should not emit removal twice");
  EXPECT(!foreignNode->getAttachedScene(),
         "already-detached node should remain detached after misuse");
  EXPECT(foreignNode->getPath() == "/foreign",
         "already-detached remove misuse should not mutate detached node state");
}

void testSceneRemoveCameraEmitsRemovalWithoutHierarchyNoise() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(captureEvent(event));
      });

  auto cameraNode = LX_core::SceneNode::create("camera_node");
  cameraNode->setName("editor_camera");
  cameraNode->addComponent<LX_core::CameraComponent>();
  scene->addCamera(cameraNode);
  events.clear();

  scene->removeCamera(cameraNode);

  EXPECT(countEventsWithType(events, LX_core::SceneEventType::SceneNodeRemoved) == 1,
         "removeCamera should emit exactly one SceneNodeRemoved event");
  EXPECT(countChangedEventsWithAspect(events, LX_core::SceneNodeAspect::Hierarchy) == 0,
         "removeCamera should not leak hierarchy-changed events for root cameras");
  const CapturedEvent *removedEvent = findFirstEventWithType(
      events, LX_core::SceneEventType::SceneNodeRemoved);
  EXPECT(removedEvent != nullptr,
         "removeCamera should emit SceneNodeRemoved for root cameras");
  if (removedEvent) {
    EXPECT(removedEvent->path == "/editor_camera",
           "removeCamera should preserve the last root camera path");
    EXPECT(removedEvent->stableNodeName == "camera_node",
           "removeCamera should preserve the camera stable nodeName");
  }
}

void testSceneRemoveNestedCameraEmitsRemovalWithoutHierarchyNoise() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &event) {
        events.push_back(captureEvent(event));
      });

  auto parent = LX_core::SceneNode::create("parent_node");
  parent->setName("parent");
  scene->addRenderable(parent);

  auto cameraNode = LX_core::SceneNode::create("camera_node");
  cameraNode->setName("child_camera");
  cameraNode->addComponent<LX_core::CameraComponent>();
  scene->addCamera(cameraNode);
  cameraNode->setParent(parent);
  events.clear();

  scene->removeCamera(cameraNode);

  EXPECT(countEventsWithType(events, LX_core::SceneEventType::SceneNodeRemoved) == 1,
         "removeCamera should emit removal for nested camera nodes");
  EXPECT(countChangedEventsWithAspect(events, LX_core::SceneNodeAspect::Hierarchy) == 0,
         "removeCamera should not leak hierarchy events for nested cameras");
  const CapturedEvent *removedEvent = findFirstEventWithType(
      events, LX_core::SceneEventType::SceneNodeRemoved);
  EXPECT(removedEvent != nullptr,
         "removeCamera should emit SceneNodeRemoved for nested cameras");
  if (removedEvent) {
    EXPECT(removedEvent->path == "/parent/child_camera",
           "removeCamera should preserve the last nested camera path");
    EXPECT(removedEvent->stableNodeName == "camera_node",
           "removeCamera should preserve the nested camera stable nodeName");
  }
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

void testSceneEventHubRecoversAfterThrowingListener() {
  auto scene = LX_core::Scene::create(nullptr);
  bool sawThrow = false;
  auto throwingSubscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &) {
        throw std::runtime_error("listener failed");
      });

  try {
    scene->events().emit(LX_core::SceneEvent{
        .path = "/throwing",
        .stableNodeName = "throwing",
    });
  } catch (const std::runtime_error &) {
    sawThrow = true;
  } catch (...) {
    EXPECT(false, "emit should rethrow the original listener exception type");
  }

  EXPECT(sawThrow, "throwing listener should propagate its exception");

  throwingSubscription.reset();

  usize healthyCalls = 0;
  auto healthySubscription =
      scene->events().subscribe([&](const LX_core::SceneEvent &) {
        ++healthyCalls;
      });

  scene->events().emit(LX_core::SceneEvent{
      .path = "/healthy",
      .stableNodeName = "healthy",
  });

  EXPECT(healthyCalls == 1,
         "hub should remain usable after a listener throws");
  EXPECT(!throwingSubscription.isActive(),
         "throwing listener subscription should be reset before the recovery emit");
  EXPECT(healthySubscription.isActive(),
         "new subscriptions should still work after exception recovery");
}

} // namespace

int main() {
  testSceneEventHubEmitsSubscribedEvent();
  testDetachedNodeMutationDoesNotEmitRuntimeSceneEvents();
  testAttachedNodeTransformMutationsEmitRuntimeSceneNodeChangedEvents();
  testAttachedNodeIdentityAndVisibilityMutationsEmitRuntimeEvents();
  testAttachedNodeHierarchyMutationsEmitRuntimeEvents();
  testCameraComponentPropertySettersEmitRuntimeEvents();
  testDirectionalLightPropertySettersEmitRuntimeEvents();
  testSceneAddRemoveLifecycleEmitsRuntimeNodeAddedAndRemovedEvents();
  testSceneRemoveChildUsesLastAttachedPathWithoutHierarchyNoise();
  testSceneRemoveSubtreeEmitsRemovalForEachRemovedNodeWithoutHierarchyNoise();
  testSceneTeardownDoesNotEmitExplicitRemoveLifecycleEvents();
  testSceneRemoveRenderableIgnoresForeignAndDetachedNodes();
  testSceneRemoveCameraEmitsRemovalWithoutHierarchyNoise();
  testSceneRemoveNestedCameraEmitsRemovalWithoutHierarchyNoise();
  testSceneEventSubscriptionExpiresWhenSceneDies();
  testSceneEventSubscribeDuringCallbackDefersToNextEmit();
  testSceneEventUnsubscribeDuringCallbackStopsLaterEmits();
  testSceneEventHubRecoversAfterThrowingListener();

  if (failures != 0) {
    std::cerr << failures << " scene_events test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] scene_events tests passed.\n";
  return 0;
}
