# Scene Runtime And Editor Events Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a scene-owned runtime event hub that emits semantic scene-node change events from real mutation points, fixes stale inspector UI after node changes, and mirrors runtime node-change events into the lxe_editor API event stream.

**Architecture:** Add a new `src/core/scene/scene_events.hpp/.cpp` subsystem with a synchronous RAII subscription hub owned by `Scene`. `SceneNode` and `Scene` emit compact runtime events such as `SceneNodeChanged`, `SceneNodeAdded`, and `SceneNodeRemoved`; `InspectorPanel` subscribes to mark drafts stale, and `LxeEditorApiService` subscribes to mirror runtime changes into its buffered API event queue.

**Tech Stack:** C++20, CMake/Ninja, existing LXEngine `Scene`/`SceneNode`/ImGui editor code, existing headless integration-test executables under `src/test/integration/`

---

## File Structure

### New files

- `src/core/scene/scene_events.hpp`
  - Define runtime/editor event domains, event types, node aspects, event payload struct, RAII subscription type, and synchronous event hub interface.
- `src/core/scene/scene_events.cpp`
  - Implement subscription bookkeeping, sequence assignment, synchronous dispatch, and safe listener removal during dispatch.
- `src/test/integration/test_scene_events.cpp`
  - Cover direct `SceneNode` / `Scene` mutation event emission semantics independently of editor UI and API transport layers.

### Modified files

- `src/core/scene/scene.hpp`
  - Include scene events, add a `SceneEventHub` member plus `events()` accessors, and declare helper emit methods if needed.
- `src/core/scene/object.hpp`
  - Include the event header where needed, extend `SceneNode` constructor surface only if required, and add small private helpers for runtime event emission.
- `src/core/scene/object.cpp`
  - Emit runtime events from write points: transform, name, parent, visibility.
- `src/core/editor/inspector_panel.hpp`
  - Add a `SceneEventSubscription`, stale-draft flag(s), and small accessors/helpers for testable resync behavior.
- `src/core/editor/inspector_panel.cpp`
  - Subscribe to the selected node’s attached scene, mark state stale on relevant runtime events, and resync safely during `draw()`.
- `src/demos/lxe_editor/lxe_editor_api_protocol.hpp`
  - Extend `ApiEventType` and API payload types to describe runtime scene-node events.
- `src/demos/lxe_editor/lxe_editor_api_protocol.cpp`
  - Serialize the new runtime scene-node API event payloads.
- `src/demos/lxe_editor/lxe_editor_api_service.hpp`
  - Add scene-event subscription state and helper methods for runtime-event observation.
- `src/demos/lxe_editor/lxe_editor_api_service.cpp`
  - Subscribe to `Scene::events()`, append mirrored runtime node-change events, and preserve existing editor-state diff events.
- `src/test/integration/test_inspector_panel.cpp`
  - Add a regression that proves external node mutation invalidates stale inspector drafts.
- `src/test/integration/test_lxe_editor_api_service.cpp`
  - Add coverage for mirrored runtime scene-node events in the API event stream.
- `src/test/CMakeLists.txt`
  - Register `test_scene_events` in `TEST_INTEGRATION_EXE_LIST`.

### Existing files to read before editing

- `src/core/scene/object.hpp`
- `src/core/scene/object.cpp`
- `src/core/scene/scene.hpp`
- `src/core/editor/inspector_panel.hpp`
- `src/core/editor/inspector_panel.cpp`
- `src/demos/lxe_editor/lxe_editor_api_protocol.hpp`
- `src/demos/lxe_editor/lxe_editor_api_protocol.cpp`
- `src/demos/lxe_editor/lxe_editor_api_service.hpp`
- `src/demos/lxe_editor/lxe_editor_api_service.cpp`
- `src/test/integration/test_inspector_panel.cpp`
- `src/test/integration/test_lxe_editor_api_service.cpp`
- `docs/superpowers/specs/2026-05-11-scene-runtime-editor-event-design.md`

## Task 1: Add Scene Event Core Types And Hub

**Files:**
- Create: `src/core/scene/scene_events.hpp`
- Create: `src/core/scene/scene_events.cpp`
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/test/CMakeLists.txt`
- Test: `src/test/integration/test_scene_events.cpp`

- [ ] **Step 1: Write the failing test for direct event hub subscription and sequencing**

Create `src/test/integration/test_scene_events.cpp` with this skeleton:

```cpp
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <iostream>
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
  LX_core::u64 sequence = 0;
};

void testSceneEventHubEmitsSubscribedEvent() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription = scene->events().subscribe(
      [&](const LX_core::SceneEvent& event) {
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
             events.front().aspects.front() == LX_core::SceneNodeAspect::Transform,
         "manual emit should preserve Transform aspect");
  EXPECT(events.front().sequence >= 1,
         "event should receive a monotonic sequence");
}

int main() {
  testSceneEventHubEmitsSubscribedEvent();

  if (failures != 0) {
    std::cerr << failures << " scene_events test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] scene_events tests passed.\n";
  return 0;
}
```

- [ ] **Step 2: Register the new test target and verify the test fails**

Modify `src/test/CMakeLists.txt` by inserting `test_scene_events` into `TEST_INTEGRATION_EXE_LIST` near the other scene/editor tests:

```cmake
  test_scene_runtime
  test_scene_events
  test_debug_ui_smoke
```

Run: `cmake --build build --target test_scene_events -j4`

Expected: FAIL at compile time with errors such as `SceneEventDomain` / `SceneEventType` / `events()` not declared.

- [ ] **Step 3: Add the new scene event header**

Create `src/core/scene/scene_events.hpp`:

```cpp
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

class SceneEventSubscription final {
public:
  SceneEventSubscription() = default;
  SceneEventSubscription(const SceneEventSubscription&) = delete;
  SceneEventSubscription& operator=(const SceneEventSubscription&) = delete;
  SceneEventSubscription(SceneEventSubscription&& other) noexcept;
  SceneEventSubscription& operator=(SceneEventSubscription&& other) noexcept;
  ~SceneEventSubscription();

  [[nodiscard]] bool isActive() const { return m_hub != nullptr; }
  void reset();

private:
  friend class SceneEventHub;
  SceneEventSubscription(SceneEventHub& hub, u64 subscriptionId);

  SceneEventHub* m_hub = nullptr;
  u64 m_subscriptionId = 0;
};

class SceneEventHub final {
public:
  using Listener = std::function<void(const SceneEvent&)>;

  [[nodiscard]] SceneEventSubscription subscribe(Listener listener);
  void emit(SceneEvent event);

private:
  friend class SceneEventSubscription;
  void unsubscribe(u64 subscriptionId);

  struct ListenerEntry final {
    u64 id = 0;
    Listener callback;
    bool removed = false;
  };

  std::vector<ListenerEntry> m_listeners;
  u64 m_nextSubscriptionId = 1;
  u64 m_nextSequence = 1;
  usize m_dispatchDepth = 0;
};

} // namespace LX_core
```

- [ ] **Step 4: Implement the event hub**

Create `src/core/scene/scene_events.cpp`:

```cpp
#include "core/scene/scene_events.hpp"

#include <algorithm>
#include <utility>

namespace LX_core {

SceneEventSubscription::SceneEventSubscription(SceneEventHub& hub,
                                               const u64 subscriptionId)
    : m_hub(&hub), m_subscriptionId(subscriptionId) {}

SceneEventSubscription::SceneEventSubscription(
    SceneEventSubscription&& other) noexcept
    : m_hub(other.m_hub), m_subscriptionId(other.m_subscriptionId) {
  other.m_hub = nullptr;
  other.m_subscriptionId = 0;
}

SceneEventSubscription& SceneEventSubscription::operator=(
    SceneEventSubscription&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  reset();
  m_hub = other.m_hub;
  m_subscriptionId = other.m_subscriptionId;
  other.m_hub = nullptr;
  other.m_subscriptionId = 0;
  return *this;
}

SceneEventSubscription::~SceneEventSubscription() { reset(); }

void SceneEventSubscription::reset() {
  if (!m_hub) {
    return;
  }
  m_hub->unsubscribe(m_subscriptionId);
  m_hub = nullptr;
  m_subscriptionId = 0;
}

SceneEventSubscription SceneEventHub::subscribe(Listener listener) {
  const u64 id = m_nextSubscriptionId++;
  m_listeners.push_back(ListenerEntry{
      .id = id,
      .callback = std::move(listener),
      .removed = false,
  });
  return SceneEventSubscription(*this, id);
}

void SceneEventHub::emit(SceneEvent event) {
  event.sequence = m_nextSequence++;

  ++m_dispatchDepth;
  for (auto& entry : m_listeners) {
    if (!entry.removed && entry.callback) {
      entry.callback(event);
    }
  }
  --m_dispatchDepth;

  if (m_dispatchDepth == 0) {
    m_listeners.erase(
        std::remove_if(m_listeners.begin(), m_listeners.end(),
                       [](const ListenerEntry& entry) { return entry.removed; }),
        m_listeners.end());
  }
}

void SceneEventHub::unsubscribe(const u64 subscriptionId) {
  for (auto& entry : m_listeners) {
    if (entry.id != subscriptionId) {
      continue;
    }
    if (m_dispatchDepth == 0) {
      entry.removed = true;
      m_listeners.erase(
          std::remove_if(m_listeners.begin(), m_listeners.end(),
                         [](const ListenerEntry& candidate) {
                           return candidate.removed;
                         }),
          m_listeners.end());
    } else {
      entry.removed = true;
    }
    return;
  }
}

} // namespace LX_core
```

- [ ] **Step 5: Expose the hub from `Scene`**

Modify `src/core/scene/scene.hpp`:

```cpp
#include "core/scene/scene_events.hpp"
```

Add these accessors inside `class Scene`:

```cpp
  [[nodiscard]] SceneEventHub& events() { return m_eventHub; }
  [[nodiscard]] const SceneEventHub& events() const { return m_eventHub; }
```

Add this member near the bottom of the class:

```cpp
  SceneEventHub m_eventHub;
```

- [ ] **Step 6: Run the new test to verify the event hub baseline passes**

Run: `cmake --build build --target test_scene_events -j4 && ./build/src/test/test_scene_events`

Expected:

```text
[PASS] scene_events tests passed.
```

- [ ] **Step 7: Commit the event core**

Run:

```bash
git add src/core/scene/scene_events.hpp src/core/scene/scene_events.cpp src/core/scene/scene.hpp src/test/CMakeLists.txt src/test/integration/test_scene_events.cpp
git commit -m "feat: add scene runtime event hub"
```

## Task 2: Emit Runtime Events From Scene And SceneNode Write Points

**Files:**
- Modify: `src/core/scene/object.hpp`
- Modify: `src/core/scene/object.cpp`
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/scene/scene.cpp`
- Test: `src/test/integration/test_scene_events.cpp`

- [ ] **Step 1: Extend the passing baseline test to cover transform, identity, hierarchy, and add/remove events**

Append these tests to `src/test/integration/test_scene_events.cpp`:

```cpp
[[nodiscard]] bool containsAspect(
    const std::vector<LX_core::SceneNodeAspect>& aspects,
    const LX_core::SceneNodeAspect expected) {
  for (const auto aspect : aspects) {
    if (aspect == expected) {
      return true;
    }
  }
  return false;
}

void testRenameEmitsIdentityAspect() {
  auto scene = LX_core::Scene::create(nullptr);
  auto node = LX_core::SceneNode::create("helmet");
  node->setName("helmet");
  scene->addRenderable(node);

  std::vector<LX_core::SceneEvent> events;
  auto subscription = scene->events().subscribe(
      [&](const LX_core::SceneEvent& event) { events.push_back(event); });

  node->setName("helmet_renamed");

  EXPECT(events.size() == 1, "rename should emit one event");
  EXPECT(containsAspect(events.front().aspects,
                        LX_core::SceneNodeAspect::Identity),
         "rename should carry Identity aspect");
}

void testReparentEmitsHierarchyAspect() {
  auto scene = LX_core::Scene::create(nullptr);
  auto parent = LX_core::SceneNode::create("parent");
  parent->setName("parent");
  auto child = LX_core::SceneNode::create("child");
  child->setName("child");
  scene->addRenderable(parent);
  scene->addRenderable(child);

  std::vector<LX_core::SceneEvent> events;
  auto subscription = scene->events().subscribe(
      [&](const LX_core::SceneEvent& event) { events.push_back(event); });

  child->setParent(parent);

  EXPECT(events.size() == 1, "reparent should emit one event");
  EXPECT(containsAspect(events.front().aspects,
                        LX_core::SceneNodeAspect::Hierarchy),
         "reparent should carry Hierarchy aspect");
  EXPECT(events.front().path == "/parent/child",
         "reparent event should report new path");
}

void testAddAndRemoveRenderableEmitLifecycleEvents() {
  auto scene = LX_core::Scene::create(nullptr);
  auto node = LX_core::SceneNode::create("helmet");
  node->setName("helmet");

  std::vector<LX_core::SceneEvent> events;
  auto subscription = scene->events().subscribe(
      [&](const LX_core::SceneEvent& event) { events.push_back(event); });

  scene->addRenderable(node);
  scene->removeRenderable(node);

  EXPECT(events.size() == 2, "add/remove should emit two events");
  EXPECT(events[0].type == LX_core::SceneEventType::SceneNodeAdded,
         "addRenderable should emit SceneNodeAdded");
  EXPECT(events[1].type == LX_core::SceneEventType::SceneNodeRemoved,
         "removeRenderable should emit SceneNodeRemoved");
}
```

Update `main()` to call all new tests.

- [ ] **Step 2: Run the test and verify the new scenarios fail**

Run: `cmake --build build --target test_scene_events -j4 && ./build/src/test/test_scene_events`

Expected: FAIL because runtime mutations do not emit any semantic events yet.

- [ ] **Step 3: Add a small helper on `SceneNode` to emit runtime node-change events**

Modify `src/core/scene/object.hpp` by adding these private declarations:

```cpp
  void emitRuntimeNodeChanged(std::vector<SceneNodeAspect> aspects) const;
  void emitRuntimeNodeChanged(SceneNodeAspect aspect) const;
```

- [ ] **Step 4: Implement `SceneNode` event emission helpers and wire write points**

Modify `src/core/scene/object.cpp` with these helpers:

```cpp
void SceneNode::emitRuntimeNodeChanged(
    std::vector<SceneNodeAspect> aspects) const {
  const auto scene = m_scene.lock();
  if (!scene) {
    return;
  }

  scene->events().emit(SceneEvent{
      .domain = SceneEventDomain::Runtime,
      .type = SceneEventType::SceneNodeChanged,
      .path = getPath(),
      .stableNodeName = m_nodeName,
      .aspects = std::move(aspects),
  });
}

void SceneNode::emitRuntimeNodeChanged(const SceneNodeAspect aspect) const {
  emitRuntimeNodeChanged(std::vector<SceneNodeAspect>{aspect});
}
```

Then update the write points:

```cpp
void SceneNode::setLocalTransform(const Transform& transform) {
  m_localTransform = transform.normalized();
  markWorldTransformDirty();
  emitRuntimeNodeChanged(SceneNodeAspect::Transform);
}

void SceneNode::setTranslation(const Vec3f& translation) {
  m_localTransform.translation = translation;
  markWorldTransformDirty();
  emitRuntimeNodeChanged(SceneNodeAspect::Transform);
}

void SceneNode::setRotation(const Quatf& rotation) {
  m_localTransform.rotation = rotation.normalized();
  markWorldTransformDirty();
  emitRuntimeNodeChanged(SceneNodeAspect::Transform);
}

void SceneNode::setScale(const Vec3f& scale) {
  m_localTransform.scale = scale;
  markWorldTransformDirty();
  emitRuntimeNodeChanged(SceneNodeAspect::Transform);
}

void SceneNode::setName(std::string name) {
  m_name = sanitizeName(std::move(name));
  warnIfSiblingNameIsDuplicated();
  emitRuntimeNodeChanged(SceneNodeAspect::Identity);
}

void SceneNode::setParent(const SharedPtr& parent) {
  // keep existing validation/body
  warnIfSiblingNameIsDuplicated();
  markWorldTransformDirty();
  emitRuntimeNodeChanged(SceneNodeAspect::Hierarchy);
}

void SceneNode::clearParent() {
  // keep existing logic/body
  markWorldTransformDirty();
  emitRuntimeNodeChanged(SceneNodeAspect::Hierarchy);
}
```

Change the inline setter in `src/core/scene/object.hpp` into a declaration:

```cpp
  void setVisibilityLayerMask(VisibilityLayerMask mask);
```

Implement it in `src/core/scene/object.cpp`:

```cpp
void SceneNode::setVisibilityLayerMask(const VisibilityLayerMask mask) {
  m_visibilityLayerMask = mask;
  emitRuntimeNodeChanged(SceneNodeAspect::Visibility);
}
```

- [ ] **Step 5: Emit add/remove lifecycle events from `Scene`**

Modify `src/core/scene/scene.hpp` inside `addRenderable` after successful attachment/name setup:

```cpp
      if (auto node = std::dynamic_pointer_cast<SceneNode>(r)) {
        node->attachToScene(weak_from_this());
        if (!node->getParent()) {
          node->setParent(m_rootNode);
        }
        node->setSceneDebugId(
            StringID(m_sceneName + "/" + node->getNodeName()));
        node->warnIfSiblingNameIsDuplicated();
        m_eventHub.emit(SceneEvent{
            .domain = SceneEventDomain::Runtime,
            .type = SceneEventType::SceneNodeAdded,
            .path = node->getPath(),
            .stableNodeName = node->getNodeName(),
            .aspects = {},
        });
      }
```

Modify `removeRenderable(const SceneNodeSharedPtr& node)` in `src/core/scene/scene.cpp` to emit:

```cpp
  m_eventHub.emit(SceneEvent{
      .domain = SceneEventDomain::Runtime,
      .type = SceneEventType::SceneNodeRemoved,
      .path = node->getPath(),
      .stableNodeName = node->getNodeName(),
      .aspects = {},
  });
```

Preserve existing removal logic, but make sure:

- `SceneNodeRemoved` comes from explicit scene remove lifecycle rather than `SceneNode::detachFromScene()`
- remove lifecycle does not leak extra `Hierarchy` events just because scene internals are clearing parent links
- remove events report the last attached path before detaching / reparents mutate it
- scene teardown (`Scene::~Scene`) does not emit explicit remove-lifecycle events unless the design is intentionally expanded later

- [ ] **Step 6: Run the event tests and verify all runtime semantics pass**

Run: `cmake --build build --target test_scene_events -j4 && ./build/src/test/test_scene_events`

Expected:

```text
[PASS] scene_events tests passed.
```

- [ ] **Step 7: Commit runtime emission wiring**

Run:

```bash
git add src/core/scene/object.hpp src/core/scene/object.cpp src/core/scene/scene.hpp src/test/integration/test_scene_events.cpp
git commit -m "feat: emit runtime scene node events"
```

## Task 3: Fix Inspector Staleness By Subscribing To Runtime Scene Events

**Files:**
- Modify: `src/core/editor/inspector_panel.hpp`
- Modify: `src/core/editor/inspector_panel.cpp`
- Test: `src/test/integration/test_inspector_panel.cpp`

- [ ] **Step 1: Add a failing regression test for stale inspector snapshots after external mutation**

Append this regression to `src/test/integration/test_inspector_panel.cpp`:

```cpp
void testSnapshotTracksExternalNodeMutationAfterSelection() {
  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select({fixture.cube});

  const auto before = panel.makeSnapshot();
  EXPECT(nearlyEqual(before.translation.x, 1.0f),
         "precondition: initial snapshot should expose original translation");

  fixture.cube->setTranslation({9.0f, 8.0f, 7.0f});

  const auto after = panel.makeSnapshot();
  EXPECT(nearlyEqual(after.translation.x, 9.0f) &&
             nearlyEqual(after.translation.y, 8.0f) &&
             nearlyEqual(after.translation.z, 7.0f),
         "snapshot should reflect external runtime mutation for selected node");
}
```

Update `main()` to call the new test.

- [ ] **Step 2: Add a failing draw-path regression that exercises stale draft invalidation**

Append this test to the same file:

```cpp
void testDrawResyncsInspectorDraftAfterExternalMutation() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] inspector stale-draft draw regression\n";
    ImGui::DestroyContext();
    return;
  }

  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select({fixture.cube});

  ImGui::NewFrame();
  panel.draw();
  ImGui::EndFrame();

  fixture.cube->setTranslation({4.0f, 5.0f, 6.0f});

  try {
    ImGui::NewFrame();
    panel.draw();
    ImGui::EndFrame();
  } catch (...) {
    EXPECT(false, "draw should survive external mutation-triggered resync");
  }

  const auto snapshot = panel.makeSnapshot();
  EXPECT(nearlyEqual(snapshot.translation.x, 4.0f) &&
             nearlyEqual(snapshot.translation.y, 5.0f) &&
             nearlyEqual(snapshot.translation.z, 6.0f),
         "draw path should leave inspector synced to latest node transform");

  ImGui::DestroyContext();
}
```

Update `main()` to call the new test.

- [ ] **Step 3: Run the test target to verify the new regression fails**

Run: `cmake --build build --target test_inspector_panel -j4 && ./build/src/test/test_inspector_panel`

Expected: PASS for `makeSnapshot()` but FAIL for stale-draft behavior during draw because selection-path-only syncing misses external mutation.

- [ ] **Step 4: Add subscription state and invalidation helpers to `InspectorPanel`**

Modify `src/core/editor/inspector_panel.hpp`:

```cpp
#include "core/scene/scene_events.hpp"
```

Add these private members:

```cpp
  void refreshSceneSubscription();
  void handleSceneEvent(const SceneEvent& event);
  [[nodiscard]] bool shouldInvalidateForEvent(const SceneEvent& event) const;

  SceneEventSubscription m_sceneSubscription;
  const Scene* m_subscribedScene = nullptr;
  bool m_snapshotDirty = true;
```

- [ ] **Step 5: Implement subscription refresh and event filtering**

Modify `src/core/editor/inspector_panel.cpp` with these helpers:

```cpp
void InspectorPanel::refreshSceneSubscription() {
  const auto selected = m_editorState.getPrimarySelected();
  const auto scene = selected.has_value()
                         ? selected->get().getAttachedScene().get()
                         : nullptr;
  if (scene == m_subscribedScene) {
    return;
  }

  m_sceneSubscription.reset();
  m_subscribedScene = scene;
  if (!m_subscribedScene) {
    return;
  }

  m_sceneSubscription = m_subscribedScene->events().subscribe(
      [this](const SceneEvent& event) { handleSceneEvent(event); });
}

void InspectorPanel::handleSceneEvent(const SceneEvent& event) {
  if (shouldInvalidateForEvent(event)) {
    m_snapshotDirty = true;
  }
}

bool InspectorPanel::shouldInvalidateForEvent(const SceneEvent& event) const {
  if (event.domain != SceneEventDomain::Runtime ||
      event.type != SceneEventType::SceneNodeChanged) {
    return false;
  }

  const auto selected = m_editorState.getPrimarySelected();
  if (!selected.has_value()) {
    return false;
  }
  if (event.path != selected->get().getPath()) {
    return false;
  }

  for (const auto aspect : event.aspects) {
    if (aspect == SceneNodeAspect::Transform ||
        aspect == SceneNodeAspect::Identity ||
        aspect == SceneNodeAspect::Hierarchy ||
        aspect == SceneNodeAspect::Visibility ||
        aspect == SceneNodeAspect::RenderableStructure) {
      return true;
    }
  }
  return false;
}
```

- [ ] **Step 6: Use the subscription and dirty flag in `draw()`**

At the top of `InspectorPanel::draw()` add:

```cpp
  refreshSceneSubscription();
```

Replace the current path-only sync gate:

```cpp
  if (snapshot.path != m_syncedSelectionPath) {
    syncDraftFromSnapshot(snapshot);
  }
```

With:

```cpp
  if (m_snapshotDirty || snapshot.path != m_syncedSelectionPath) {
    syncDraftFromSnapshot(snapshot);
    m_snapshotDirty = false;
  }
```

Also mark the panel dirty when there is no selection:

```cpp
  if (!snapshot.hasSelection) {
    m_syncedSelectionPath.clear();
    m_snapshotDirty = true;
    ImGui::End();
    return;
  }
```

Inside `syncDraftFromSnapshot(...)`, preserve the existing draft copying logic and add:

```cpp
  m_snapshotDirty = false;
```

- [ ] **Step 7: Run the inspector tests and verify the regression passes**

Run: `cmake --build build --target test_inspector_panel -j4 && ./build/src/test/test_inspector_panel`

Expected:

```text
[PASS] inspector_panel tests passed.
```

- [ ] **Step 8: Commit the inspector fix**

Run:

```bash
git add src/core/editor/inspector_panel.hpp src/core/editor/inspector_panel.cpp src/test/integration/test_inspector_panel.cpp
git commit -m "fix: refresh inspector from runtime scene events"
```

## Task 4: Mirror Runtime Scene Events Into The lxe_editor API Event Stream

**Files:**
- Modify: `src/demos/lxe_editor/lxe_editor_api_protocol.hpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_protocol.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_service.hpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_service.cpp`
- Test: `src/test/integration/test_lxe_editor_api_service.cpp`

- [ ] **Step 1: Add a failing API service regression for runtime scene-node events**

Append this test to `src/test/integration/test_lxe_editor_api_service.cpp`:

```cpp
void testRuntimeSceneNodeMutationEmitsApiSceneNodeChangedEvent() {
  Fixture fixture;
  const auto node = SceneNode::create("helmet");
  node->setName("helmet");
  fixture.scene->addRenderable(node);

  const ApiEventCursor cursor = fixture.service->currentCursor();

  node->setTranslation({0.0f, 1.0f, 0.0f});

  const ApiEventBatch batch =
      fixture.service->collectEventsSince(cursor);
  bool sawRuntimeNodeChanged = false;
  for (const auto& event : batch.events) {
    sawRuntimeNodeChanged =
        sawRuntimeNodeChanged || event.type == ApiEventType::SceneNodeChanged;
  }
  EXPECT(sawRuntimeNodeChanged,
         "runtime scene-node mutation should be mirrored into API events");
}
```

Update `main()` to call the new test.

- [ ] **Step 2: Run the API service test to verify the new event type is missing**

Run: `cmake --build build --target test_lxe_editor_api_service -j4 && ./build/src/test/test_lxe_editor_api_service`

Expected: FAIL because `ApiEventType::SceneNodeChanged` and runtime-event mirroring do not exist yet.

- [ ] **Step 3: Extend the API protocol with runtime scene-node event payloads**

Modify `src/demos/lxe_editor/lxe_editor_api_protocol.hpp`:

```cpp
enum class ApiEventType {
  CommandExecuted,
  SceneLoaded,
  SceneSaved,
  SelectionChanged,
  ModeChanged,
  PreviewChanged,
  DirtyChanged,
  SceneNodeChanged,
};

struct ApiSceneNodeEventPayload final {
  std::string path;
  std::string stableNodeName;
  std::vector<std::string> aspects;

  bool operator==(const ApiSceneNodeEventPayload&) const = default;
};
```

Extend `ApiEvent`:

```cpp
  std::optional<ApiSceneNodeEventPayload> sceneNode;
```

Add declarations:

```cpp
[[nodiscard]] std::string toJson(const ApiSceneNodeEventPayload& payload);
```

- [ ] **Step 4: Implement API protocol serialization**

Modify `src/demos/lxe_editor/lxe_editor_api_protocol.cpp`:

```cpp
[[nodiscard]] const char* apiEventTypeName(ApiEventType type) {
  switch (type) {
  case ApiEventType::CommandExecuted:
    return "command.executed";
  case ApiEventType::SceneLoaded:
    return "scene.loaded";
  case ApiEventType::SceneSaved:
    return "scene.saved";
  case ApiEventType::SelectionChanged:
    return "selection.changed";
  case ApiEventType::ModeChanged:
    return "mode.changed";
  case ApiEventType::PreviewChanged:
    return "preview.changed";
  case ApiEventType::DirtyChanged:
    return "dirty.changed";
  case ApiEventType::SceneNodeChanged:
    return "scene_node.changed";
  }
  return "unknown";
}
```

Add:

```cpp
std::string toJson(const ApiSceneNodeEventPayload& payload) {
  std::string out = "{";
  out += "\"path\":\"" + apiJsonEscape(payload.path) + "\"";
  out += ",\"stableNodeName\":\"" + apiJsonEscape(payload.stableNodeName) + "\"";
  out += ",\"aspects\":[";
  for (usize i = 0; i < payload.aspects.size(); ++i) {
    if (i != 0) {
      out += ",";
    }
    out += "\"" + apiJsonEscape(payload.aspects[i]) + "\"";
  }
  out += "]}";
  return out;
}
```

And make `toJson(const ApiEvent& event)` emit the `sceneNode` payload if present.

- [ ] **Step 5: Add runtime scene-event subscription to the API service**

Modify `src/demos/lxe_editor/lxe_editor_api_service.hpp`:

```cpp
#include "core/scene/scene_events.hpp"
```

Add private declarations:

```cpp
  void observeRuntimeSceneEvent(const LX_core::SceneEvent& event);
  [[nodiscard]] static std::string sceneNodeAspectName(
      LX_core::SceneNodeAspect aspect);

  LX_core::SceneEventSubscription m_sceneSubscription;
```

In the constructor initialization path of `src/demos/lxe_editor/lxe_editor_api_service.cpp`, subscribe once:

```cpp
  m_sceneSubscription = m_scene.events().subscribe(
      [this](const LX_core::SceneEvent& event) {
        observeRuntimeSceneEvent(event);
      });
```

- [ ] **Step 6: Mirror runtime `SceneNodeChanged` events into buffered API events**

Add this implementation in `src/demos/lxe_editor/lxe_editor_api_service.cpp`:

```cpp
std::string LxeEditorApiService::sceneNodeAspectName(
    const LX_core::SceneNodeAspect aspect) {
  switch (aspect) {
  case LX_core::SceneNodeAspect::Transform:
    return "transform";
  case LX_core::SceneNodeAspect::Identity:
    return "identity";
  case LX_core::SceneNodeAspect::Hierarchy:
    return "hierarchy";
  case LX_core::SceneNodeAspect::Visibility:
    return "visibility";
  case LX_core::SceneNodeAspect::RenderableStructure:
    return "renderable_structure";
  }
  return "unknown";
}

void LxeEditorApiService::observeRuntimeSceneEvent(
    const LX_core::SceneEvent& event) {
  if (event.domain != LX_core::SceneEventDomain::Runtime ||
      event.type != LX_core::SceneEventType::SceneNodeChanged) {
    return;
  }

  ApiSceneNodeEventPayload payload;
  payload.path = event.path;
  payload.stableNodeName = event.stableNodeName;
  for (const auto aspect : event.aspects) {
    payload.aspects.push_back(sceneNodeAspectName(aspect));
  }

  ApiEvent apiEvent{
      .sequence = m_nextSequence++,
      .type = ApiEventType::SceneNodeChanged,
      .payloadJson = toJson(payload),
  };
  apiEvent.sceneNode = payload;
  appendEvent(std::move(apiEvent));
}
```

Do not remove existing `observeCommandHistory()` / `observeStateChanges()` behavior.

- [ ] **Step 7: Run the API service tests and verify runtime event mirroring passes**

Run: `cmake --build build --target test_lxe_editor_api_service -j4 && ./build/src/test/test_lxe_editor_api_service`

Expected:

```text
[PASS] lxe_editor API service tests
```

- [ ] **Step 8: Commit the API mirroring layer**

Run:

```bash
git add src/demos/lxe_editor/lxe_editor_api_protocol.hpp src/demos/lxe_editor/lxe_editor_api_protocol.cpp src/demos/lxe_editor/lxe_editor_api_service.hpp src/demos/lxe_editor/lxe_editor_api_service.cpp src/test/integration/test_lxe_editor_api_service.cpp
git commit -m "feat: expose runtime scene events through editor api"
```

## Task 5: Full Verification And Cleanup

**Files:**
- Verify only

- [ ] **Step 1: Rebuild all touched headless test targets**

Run:

```bash
cmake --build build --target test_scene_events test_inspector_panel test_lxe_editor_api_service -j4
```

Expected: build completes without compiler or linker errors.

- [ ] **Step 2: Run the focused tests individually**

Run:

```bash
./build/src/test/test_scene_events
./build/src/test/test_inspector_panel
./build/src/test/test_lxe_editor_api_service
```

Expected:

```text
[PASS] scene_events tests passed.
[PASS] inspector_panel tests passed.
[PASS] lxe_editor API service tests
```

- [ ] **Step 3: Run them through `ctest` for target-level registration coverage**

Run:

```bash
cd build && ctest --output-on-failure -R "test_scene_events|test_inspector_panel|test_lxe_editor_api_service"
```

Expected:

```text
100% tests passed
```

- [ ] **Step 4: Inspect the diff for accidental scope creep**

Run:

```bash
git diff --stat HEAD~3..HEAD
git diff -- src/core/scene src/core/editor/inspector_panel.* src/demos/lxe_editor/lxe_editor_api_* src/test/integration/test_scene_events.cpp src/test/integration/test_inspector_panel.cpp src/test/integration/test_lxe_editor_api_service.cpp src/test/CMakeLists.txt
```

Expected: only the planned event hub, scene/object emission points, inspector/API consumers, and focused tests are changed.

- [ ] **Step 5: Create the final integration commit**

Run:

```bash
git add src/core/scene/scene_events.hpp src/core/scene/scene_events.cpp src/core/scene/scene.hpp src/core/scene/object.hpp src/core/scene/object.cpp src/core/editor/inspector_panel.hpp src/core/editor/inspector_panel.cpp src/demos/lxe_editor/lxe_editor_api_protocol.hpp src/demos/lxe_editor/lxe_editor_api_protocol.cpp src/demos/lxe_editor/lxe_editor_api_service.hpp src/demos/lxe_editor/lxe_editor_api_service.cpp src/test/integration/test_scene_events.cpp src/test/integration/test_inspector_panel.cpp src/test/integration/test_lxe_editor_api_service.cpp src/test/CMakeLists.txt
git commit -m "feat: add runtime scene events for editor sync"
```

## Self-Review Checklist

Spec coverage mapping:

- scene-owned synchronous runtime event hub: Task 1
- emit from real write points: Task 2
- inspector stale UI fix: Task 3
- API queue mirroring of runtime events: Task 4
- focused verification: Task 5

Placeholder scan:

- no `TODO`, `TBD`, or “implement later” placeholders remain
- every test step includes concrete code or command lines
- every implementation step names exact files and exact snippets to add

Type consistency reminders during implementation:

- keep `SceneEventDomain`, `SceneEventType`, `SceneNodeAspect`, `SceneEvent`, `SceneEventHub`, and `SceneEventSubscription` names unchanged across all tasks
- keep the API event name `ApiSceneNodeEventPayload` unchanged across protocol and service code
- keep the new API type enumerator name `ApiEventType::SceneNodeChanged` consistent across protocol, service, and test code
