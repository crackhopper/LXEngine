#include "core/editor/command_bus.hpp"
#include "core/editor/editor_state.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/api_token_state.hpp"
#include "demos/lxe_editor/lxe_editor_api_protocol.hpp"
#include "demos/lxe_editor/lxe_editor_api_service.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace LX_core;
using namespace LX_demo::lxe_editor;

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

struct MutableHookState final {
  ApiSceneSummary scene;
  ApiCameraSnapshot cameras;
  ApiToolbarSnapshot toolbar;
  std::optional<LX_core::Vec3f> lastHitPoint;
};

struct Fixture final {
  SceneSharedPtr scene = Scene::create(nullptr);
  EditorState editorState;
  CommandBus bus;
  MutableHookState hookState;
  LxeEditorApiService::Hooks hooks;
  std::unique_ptr<LxeEditorApiService> service;

  Fixture()
  {
    hookState.scene.sceneName = "Scene";
    hookState.scene.currentDocumentPath = "data/scenes/test.scene.yaml";
    hookState.scene.sourceKind = ApiSceneSourceKind::Local;
    hookState.scene.permission = ApiPermissionLevel::User;
    hookState.scene.dirty = false;

    hookState.cameras.activeCameraPath = "/editor_cam";
    hookState.cameras.editor.path = "/editor_cam";
    hookState.cameras.editor.eye = {1.0f, 2.0f, 3.0f};
    hookState.cameras.editor.target = {1.0f, 2.0f, 2.0f};
    hookState.cameras.editor.up = {0.0f, 1.0f, 0.0f};
    hookState.cameras.game.path = "/game_cam";
    hookState.cameras.game.eye = {4.0f, 5.0f, 6.0f};
    hookState.cameras.game.target = {4.0f, 5.0f, 5.0f};
    hookState.cameras.game.up = {0.0f, 1.0f, 0.0f};

    hookState.toolbar.editMode = ApiEditMode::Orbit;
    hookState.toolbar.previewEnabled = false;

    hooks.sceneSummary = [this]() { return hookState.scene; };
    hooks.cameraSnapshot = [this]() { return hookState.cameras; };
    hooks.toolbarSnapshot = [this]() { return hookState.toolbar; };
    hooks.lastHitPoint = [this]() { return hookState.lastHitPoint; };
    service =
        std::make_unique<LxeEditorApiService>(bus, editorState, *scene, hooks);
  }
};

void testExecuteCommandMirrorsCommandBusAndEmitsCommandEvent() {
  Fixture fixture;
  fixture.bus.registerHandler(
      "echo", "echo <value>", [](std::vector<std::string> args) {
        return CommandResult{true, args.empty() ? std::string{} : args.front(),
                             "{\"kind\":\"echo\"}"};
      });

  const ApiEventCursor cursor = fixture.service->currentCursor();
  const ApiCommandResponse response = fixture.service->executeCommand(
      ApiCommandRequest{.line = "echo hello"});
  EXPECT(response.ok, "command execution should succeed");
  EXPECT(response.line == "echo hello", "response should preserve command line");
  EXPECT(response.message == "hello",
         "response should mirror command bus message");
  EXPECT(response.structuredJson == "{\"kind\":\"echo\"}",
         "response should mirror structured payload");
  EXPECT(fixture.bus.history().size() == 1,
         "command execution should go through command bus history");

  const ApiEventBatch batch =
      fixture.service->collectEventsSince(cursor);
  EXPECT(batch.events.size() == 1,
         "command execution should emit one command event");
  EXPECT(batch.events.front().type == ApiEventType::CommandExecuted,
         "command event type should be command.executed");
  EXPECT(batch.events.front().command.has_value(),
         "command event should carry command payload");
  if (batch.events.front().command.has_value()) {
    EXPECT(batch.events.front().command->line == "echo hello",
           "event command payload should preserve line");
  }
}

void testCaptureStateUsesHooksAndEditorSelection() {
  Fixture fixture;
  const auto node = SceneNode::create("node_target");
  node->setName("target");
  fixture.scene->addRenderable(node);
  fixture.editorState.select({node});
  fixture.hookState.lastHitPoint = LX_core::Vec3f{9.0f, 8.0f, 7.0f};

  const ApiStateSnapshot snapshot = fixture.service->captureState();
  EXPECT(snapshot.scene.sceneName == "Scene",
         "scene summary should come from hook provider");
  EXPECT(snapshot.scene.currentDocumentPath == "data/scenes/test.scene.yaml",
         "scene path should come from hook provider");
  EXPECT(snapshot.toolbar.editMode == ApiEditMode::Orbit,
         "toolbar snapshot should come from hook provider");
  EXPECT(snapshot.selection.selectedPaths.size() == 1,
         "selection snapshot should include EditorState selection");
  EXPECT(snapshot.selection.primaryPath == node->getPath(),
         "primary selection path should match selected node");
  EXPECT(snapshot.selection.lastHitPoint.has_value(),
         "selection snapshot should include optional hit point");
  if (snapshot.selection.lastHitPoint.has_value()) {
    const LX_core::Vec3f expectedHitPoint{9.0f, 8.0f, 7.0f};
    EXPECT(*snapshot.selection.lastHitPoint == expectedHitPoint,
           "selection hit point should come from hook provider");
  }
}

void testRefreshEmitsDirtyPreviewAndModeChangeEvents() {
  Fixture fixture;
  const ApiEventCursor cursor = fixture.service->currentCursor();

  fixture.hookState.scene.dirty = true;
  fixture.hookState.toolbar.previewEnabled = true;
  fixture.hookState.toolbar.editMode = ApiEditMode::Selection;
  fixture.service->refresh();

  const ApiEventBatch batch =
      fixture.service->collectEventsSince(cursor);
  bool sawDirty = false;
  bool sawPreview = false;
  bool sawMode = false;
  for (const auto& event : batch.events) {
    sawDirty = sawDirty || event.type == ApiEventType::DirtyChanged;
    sawPreview = sawPreview || event.type == ApiEventType::PreviewChanged;
    sawMode = sawMode || event.type == ApiEventType::ModeChanged;
  }
  EXPECT(sawDirty, "refresh should emit dirty.changed when dirty flips");
  EXPECT(sawPreview,
         "refresh should emit preview.changed when preview flips");
  EXPECT(sawMode, "refresh should emit mode.changed when edit mode flips");
}

void testRuntimeSceneNodeMutationEmitsApiSceneNodeChangedEvent() {
  Fixture fixture;
  const auto parent = SceneNode::create("parent");
  parent->setName("parent");
  fixture.scene->addRenderable(parent);
  const auto node = SceneNode::create("helmet_node");
  node->setName("helmet");
  node->setParent(parent);
  fixture.scene->addRenderable(node);

  const ApiEventCursor cursor = fixture.service->currentCursor();

  node->setTranslation({0.0f, 1.0f, 0.0f});
  const ApiEventBatch beforeRefresh =
      fixture.service->collectEventsSince(cursor);
  EXPECT(beforeRefresh.events.empty(),
         "runtime scene-node events should stay queued until refresh");

  fixture.service->refresh();

  const ApiEventBatch batch =
      fixture.service->collectEventsSince(cursor);
  bool sawRuntimeNodeChanged = false;
  for (const auto& event : batch.events) {
    if (event.type != ApiEventType::SceneNodeChanged) {
      continue;
    }

    sawRuntimeNodeChanged = true;
    EXPECT(event.sceneNode.has_value(),
           "scene_node.changed event should carry scene-node payload");
    if (event.sceneNode.has_value()) {
      EXPECT(event.sceneNode->path == node->getPath(),
             "scene-node payload should preserve mutated node path");
      EXPECT(event.sceneNode->path == "/parent/helmet",
             "scene-node payload path should preserve full hierarchy path");
      EXPECT(event.sceneNode->stableNodeName == node->getNodeName(),
             "scene-node payload should preserve stable node name");
      EXPECT(event.sceneNode->stableNodeName == "helmet_node",
             "scene-node payload stable name should preserve constructor identity");
      EXPECT(event.sceneNode->stableNodeName != event.sceneNode->path,
             "stable node name should remain distinct from full path");
      EXPECT(event.sceneNode->aspects.size() == 1,
             "scene-node payload should keep a minimal aspect list");
      if (event.sceneNode->aspects.size() == 1) {
        EXPECT(event.sceneNode->aspects.front() == "transform",
               "scene-node payload should report transform mutations");
      }
      EXPECT(event.payloadJson == toJson(*event.sceneNode),
             "scene-node event payloadJson should match serialized payload");
    }
  }
  EXPECT(sawRuntimeNodeChanged,
         "runtime scene-node mutation should be mirrored into API events");
}

void testCommandDrivenSceneMutationKeepsCommandEventBeforeSceneNodeChanged() {
  Fixture fixture;
  const auto node = SceneNode::create("helmet_node");
  node->setName("helmet");
  fixture.scene->addRenderable(node);
  fixture.bus.registerHandler(
      "move_node", "move_node", [node](std::vector<std::string>) {
        node->setTranslation({2.0f, 3.0f, 4.0f});
        return CommandResult{true, "moved", "{\"kind\":\"move_node\"}"};
      });

  const ApiEventCursor cursor = fixture.service->currentCursor();

  const ApiCommandResponse response = fixture.service->executeCommand(
      ApiCommandRequest{.line = "move_node"});
  EXPECT(response.ok, "command-driven node mutation should succeed");

  const ApiEventBatch batch =
      fixture.service->collectEventsSince(cursor);
  EXPECT(batch.events.size() >= 2,
         "command-driven node mutation should emit command and runtime events");

  std::optional<ApiEvent> commandEvent;
  std::optional<ApiEvent> sceneNodeEvent;
  for (const auto& event : batch.events) {
    if (event.type == ApiEventType::CommandExecuted && !commandEvent.has_value()) {
      commandEvent = event;
    }
    if (event.type == ApiEventType::SceneNodeChanged && !sceneNodeEvent.has_value()) {
      sceneNodeEvent = event;
    }
  }

  EXPECT(commandEvent.has_value(),
         "command-driven node mutation should emit command.executed");
  EXPECT(sceneNodeEvent.has_value(),
         "command-driven node mutation should emit scene_node.changed");
  if (commandEvent.has_value() && sceneNodeEvent.has_value()) {
    EXPECT(commandEvent->sequence < sceneNodeEvent->sequence,
           "command.executed should keep an earlier API sequence than its runtime side effect");
    EXPECT(sceneNodeEvent->sceneNode.has_value(),
           "scene_node.changed should carry scene-node payload");
    if (sceneNodeEvent->sceneNode.has_value()) {
      EXPECT(sceneNodeEvent->sceneNode->aspects.size() == 1 &&
                 sceneNodeEvent->sceneNode->aspects.front() == "transform",
             "command-driven scene-node payload should report transform aspect");
    }
  }
}

void testExecuteCommandFlushesOlderQueuedRuntimeEventsBeforeNewCommand() {
  Fixture fixture;
  const auto olderNode = SceneNode::create("older_node");
  olderNode->setName("older");
  fixture.scene->addRenderable(olderNode);
  const auto newerNode = SceneNode::create("newer_node");
  newerNode->setName("newer");
  fixture.scene->addRenderable(newerNode);
  fixture.bus.registerHandler(
      "move_newer", "move_newer", [newerNode](std::vector<std::string>) {
        newerNode->setTranslation({5.0f, 6.0f, 7.0f});
        return CommandResult{true, "moved", "{\"kind\":\"move_newer\"}"};
      });

  const ApiEventCursor cursor = fixture.service->currentCursor();

  olderNode->setTranslation({1.0f, 2.0f, 3.0f});

  const ApiCommandResponse response = fixture.service->executeCommand(
      ApiCommandRequest{.line = "move_newer"});
  EXPECT(response.ok, "executeCommand should succeed with pre-queued runtime events");

  const ApiEventBatch batch =
      fixture.service->collectEventsSince(cursor);
  EXPECT(batch.events.size() >= 3,
         "pre-queued runtime event plus command side effect should yield at least three events");

  std::vector<ApiEvent> sceneNodeEvents;
  std::optional<ApiEvent> commandEvent;
  for (const auto& event : batch.events) {
    if (event.type == ApiEventType::SceneNodeChanged) {
      sceneNodeEvents.push_back(event);
    }
    if (event.type == ApiEventType::CommandExecuted && !commandEvent.has_value()) {
      commandEvent = event;
    }
  }

  EXPECT(sceneNodeEvents.size() >= 2,
         "pre-queued runtime event and command side effect should both be mirrored");
  EXPECT(commandEvent.has_value(),
         "executeCommand should still emit command.executed");
  if (sceneNodeEvents.size() >= 2 && commandEvent.has_value()) {
    EXPECT(sceneNodeEvents[0].sceneNode.has_value(),
           "older queued runtime event should carry scene-node payload");
    EXPECT(sceneNodeEvents[1].sceneNode.has_value(),
           "command-side-effect runtime event should carry scene-node payload");
    if (sceneNodeEvents[0].sceneNode.has_value() &&
        sceneNodeEvents[1].sceneNode.has_value()) {
      EXPECT(sceneNodeEvents[0].sceneNode->stableNodeName == "older_node",
             "older queued runtime event should flush before dispatching a new command");
      EXPECT(commandEvent->sequence > sceneNodeEvents[0].sequence,
             "older queued runtime event should get an earlier sequence than the new command");
      EXPECT(commandEvent->sequence < sceneNodeEvents[1].sequence,
             "command.executed should keep an earlier sequence than its own runtime side effect");
      EXPECT(sceneNodeEvents[1].sceneNode->stableNodeName == "newer_node",
             "later runtime event should belong to the command-mutated node");
    }
  }
}

void testApiTokenStatePersistsSingleGeneratedToken() {
  const auto rootDir = std::filesystem::temp_directory_path() /
                       "lxengine_api_token_state_test";
  std::error_code ec;
  std::filesystem::remove_all(rootDir, ec);

  const ApiTokenState state(rootDir);
  const std::string first = state.loadOrCreateToken();
  const std::string second = state.loadOrCreateToken();

  EXPECT(!first.empty(), "generated token should not be empty");
  EXPECT(first == second,
         "loadOrCreateToken should reuse previously persisted token");
  EXPECT(std::filesystem::exists(state.tokenPath()),
         "token file should be created on first load");

  std::filesystem::remove_all(rootDir, ec);
}

} // namespace

int main() {
  testExecuteCommandMirrorsCommandBusAndEmitsCommandEvent();
  testCaptureStateUsesHooksAndEditorSelection();
  testRefreshEmitsDirtyPreviewAndModeChangeEvents();
  testRuntimeSceneNodeMutationEmitsApiSceneNodeChangedEvent();
  testCommandDrivenSceneMutationKeepsCommandEventBeforeSceneNodeChanged();
  testExecuteCommandFlushesOlderQueuedRuntimeEventsBeforeNewCommand();
  testApiTokenStatePersistsSingleGeneratedToken();

  if (failures != 0) {
    std::cerr << failures << " API service test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] lxe_editor API service tests\n";
  return 0;
}
