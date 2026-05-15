#include "core/editor/command_bus.hpp"
#include "core/editor/editor_state.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/api_token_state.hpp"
#include "demos/lxe_editor/lxe_editor_api_protocol.hpp"
#include "demos/lxe_editor/lxe_editor_api_service.hpp"
#include "demos/lxe_editor/lxe_editor_commands.hpp"
#include "demos/lxe_editor/recording_controller.hpp"
#include "demos/lxe_editor/scene_interaction_controller.hpp"
#include "demos/lxe_editor/scene_view_rect.hpp"

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
  std::string displayListJson =
      "{\"profiles\":[{\"key\":\"display-a\",\"label\":\"Display "
      "A\",\"available\":true,\"active\":true}]}";
  std::string displayActiveJson = "{\"activeDisplay\":\"display-a\"}";
  std::string displayConfigGetJson = "{\"key\":\"display-a\",\"effective\":{"
                                     "\"preferences\":{\"uiFontScale\":1.25}}}";
  std::string displayConfigSetJson =
      "{\"ok\":true,\"key\":\"display-a\",\"saved\":true}";
  std::string displaySelectJson =
      "{\"ok\":true,\"activeDisplay\":\"display-b\",\"restartRequired\":true}";
};

struct Fixture final {
  SceneSharedPtr scene = Scene::create(nullptr);
  EditorState editorState;
  CommandBus bus;
  MutableHookState hookState;
  LxeEditorApiService::Hooks hooks;
  std::unique_ptr<LxeEditorApiService> service;
  std::unique_ptr<RecordingController> recording;

  Fixture() {
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

    hookState.toolbar.mode = ApiEditorMode::Selection;
    hookState.toolbar.camera = ApiCameraControlMode::FreeFly;
    hookState.toolbar.previewEnabled = false;

    hooks.sceneSummary = [this]() { return hookState.scene; };
    hooks.cameraSnapshot = [this]() { return hookState.cameras; };
    hooks.toolbarSnapshot = [this]() { return hookState.toolbar; };
    hooks.lastHitPoint = [this]() { return hookState.lastHitPoint; };
    hooks.displayListJson = [this]() { return hookState.displayListJson; };
    hooks.displayActiveJson = [this]() { return hookState.displayActiveJson; };
    hooks.displayConfigGetJson = [this](const std::string &key) {
      return "{\"key\":\"" + key + "\",\"source\":\"hook\"}";
    };
    hooks.displayConfigSet = [this](const std::string &key,
                                    const std::string &patch) {
      return "{\"key\":\"" + key + "\",\"patch\":\"" + patch + "\"}";
    };
    hooks.displaySelect = [this](const std::string &key) {
      return "{\"selected\":\"" + key + "\",\"restartRequired\":true}";
    };
    recording = std::make_unique<RecordingController>(
        std::filesystem::temp_directory_path() / "lxe_api_service_recording");
    hooks.recording =
        [this]() -> std::optional<std::reference_wrapper<RecordingController>> {
      return *recording;
    };
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
  const ApiCommandResponse response =
      fixture.service->executeCommand(ApiCommandRequest{.line = "echo hello"});
  EXPECT(response.ok, "command execution should succeed");
  EXPECT(response.line == "echo hello",
         "response should preserve command line");
  EXPECT(response.message == "hello",
         "response should mirror command bus message");
  EXPECT(response.structuredJson == "{\"kind\":\"echo\"}",
         "response should mirror structured payload");
  EXPECT(fixture.bus.history().size() == 1,
         "command execution should go through command bus history");

  const ApiEventBatch batch = fixture.service->collectEventsSince(cursor);
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

void testDisplayCommandsReturnStructuredHookJson() {
  Fixture fixture;
  SceneInteractionController interaction{fixture.bus, fixture.editorState,
                                         *fixture.scene};
  int editMode = 0;
  int cameraControlMode = 0;
  std::string capturedSetKey;
  std::string capturedSetPatch;
  std::string capturedSelectKey;
  registerLxeEditorCommands(
      fixture.bus,
      LxeEditorCommandContext{
          .editorState = fixture.editorState,
          .scene = *fixture.scene,
          .interaction = interaction,
          .getEditMode = [&editMode]() { return editMode; },
          .setEditMode =
              [&editMode](const int modeCode) { editMode = modeCode; },
          .getCameraControlMode =
              [&cameraControlMode]() { return cameraControlMode; },
          .setCameraControlMode =
              [&cameraControlMode](const int modeCode) {
                cameraControlMode = modeCode;
              },
          .sceneViewRect = []() { return SceneViewRect{}; },
          .dirty = []() { return false; },
          .debugEnabled = []() { return false; },
          .setDebugEnabled = [](bool) {},
          .currentDocumentPath = []() { return std::optional<std::string>{}; },
          .persistedHistory = []() { return std::vector<std::string>{}; },
          .appendConsoleDebugLine = [](std::string_view) {},
          .displayListJson =
              [&fixture]() { return fixture.hookState.displayListJson; },
          .displayActiveJson =
              [&fixture]() { return fixture.hookState.displayActiveJson; },
          .displayConfigGetJson =
              [](std::string_view key) {
                return "{\"key\":\"" + std::string(key) +
                       "\",\"source\":\"command-hook\"}";
              },
          .displayConfigSet =
              [&capturedSetKey, &capturedSetPatch](std::string_view key,
                                                   std::string_view patch) {
                capturedSetKey = std::string(key);
                capturedSetPatch = std::string(patch);
                return "{\"ok\":true,\"source\":\"command-set-hook\"}";
              },
          .displaySelect =
              [&capturedSelectKey](std::string_view key) {
                capturedSelectKey = std::string(key);
                return "{\"ok\":true,\"restartRequired\":true,"
                       "\"source\":\"command-select-hook\"}";
              },
      });

  const ApiCommandResponse list = fixture.service->executeCommand(
      ApiCommandRequest{.line = "display list"});
  EXPECT(list.ok, "display list should succeed when hook is present");
  EXPECT(list.structuredJson.find("\"profiles\"") != std::string::npos,
         "display list should return structured profiles JSON");
  EXPECT(list.structuredJson.find("\"display-a\"") != std::string::npos,
         "display list structured JSON should come from hook");

  const ApiCommandResponse active = fixture.service->executeCommand(
      ApiCommandRequest{.line = "display active"});
  EXPECT(active.ok, "display active should succeed through registered command");
  EXPECT(active.structuredJson.find("\"activeDisplay\":\"display-a\"") !=
             std::string::npos,
         "display active should use registered command hook");

  const ApiCommandResponse config = fixture.service->executeCommand(
      ApiCommandRequest{.line = "display config get default"});
  EXPECT(
      config.ok,
      "display config get default should succeed through registered command");
  EXPECT(
      config.structuredJson.find("\"key\":\"default\"") != std::string::npos,
      "display config get default should pass key through registered command");

  const ApiCommandResponse set = fixture.service->executeCommand(
      ApiCommandRequest{.line =
                            "display config set default \"preferences: "
                            "{uiFontScale: 1.4}\""});
  EXPECT(set.ok,
         "display config set should succeed through registered command");
  EXPECT(capturedSetKey == "default",
         "display config set should pass key through registered command");
  EXPECT(capturedSetPatch == "preferences: {uiFontScale: 1.4}",
         "display config set should pass patch through registered command");
  EXPECT(set.structuredJson.find("\"ok\":true") != std::string::npos,
         "display config set should return structured hook JSON");

  const ApiCommandResponse select = fixture.service->executeCommand(
      ApiCommandRequest{.line = "display select display-b"});
  EXPECT(select.ok, "display select should succeed through registered command");
  EXPECT(capturedSelectKey == "display-b",
         "display select should pass key through registered command");
  EXPECT(select.structuredJson.find("\"restartRequired\":true") !=
             std::string::npos,
         "display select should return restartRequired structured JSON");
}

void testRecordingToolsRecordMcpCommand() {
  Fixture fixture;
  fixture.bus.registerHandler(
      "echo", "echo <value>", [](std::vector<std::string> args) {
        return CommandResult{true, args.empty() ? std::string{} : args.front(),
                             "{}"};
      });

  const std::string disabledStatus = fixture.service->recordingStatus();
  EXPECT(disabledStatus.find("\"enabled\":false") != std::string::npos,
         "recording should default disabled");

  (void)fixture.service->recordingEnable();
  (void)fixture.service->recordingStart(RecordingDetailLevel::Basic);
  const ApiCommandResponse response =
      fixture.service->executeCommand(ApiCommandRequest{.line = "echo hello"});
  EXPECT(response.ok, "recorded command should still execute");

  const std::string activeRecording = fixture.service->recordingRead("active");
  EXPECT(activeRecording.find("\"source\":\"mcp\"") != std::string::npos,
         "MCP command should record source=mcp");
  EXPECT(activeRecording.find("echo hello") != std::string::npos,
         "recording should include command line");
  EXPECT(activeRecording.find("\"build\":{") != std::string::npos,
         "recording metadata should include build identity");
  EXPECT(activeRecording.find("\"gitCommit\"") != std::string::npos,
         "recording build identity should include git commit field");
}

void testBuildInfoExposesGitIdentityFields() {
  Fixture fixture;

  const std::string buildInfo = fixture.service->buildInfo();
  EXPECT(buildInfo.find("\"gitCommit\"") != std::string::npos,
         "build info should include gitCommit");
  EXPECT(buildInfo.find("\"gitCommitShort\"") != std::string::npos,
         "build info should include gitCommitShort");
  EXPECT(buildInfo.find("\"gitDirty\"") != std::string::npos,
         "build info should include gitDirty");
  EXPECT(buildInfo.find("\"buildType\"") != std::string::npos,
         "build info should include buildType");
  EXPECT(buildInfo.find("\"builtAt\"") != std::string::npos,
         "build info should include builtAt");
}

void testDisplayApiMethodsUseHooks() {
  Fixture fixture;

  EXPECT(fixture.service->displayList().find("\"profiles\"") !=
             std::string::npos,
         "displayList should return hook JSON");
  EXPECT(fixture.service->displayActive().find(
             "\"activeDisplay\":\"display-a\"") != std::string::npos,
         "displayActive should return hook JSON");
  EXPECT(fixture.service->displayConfigGet("active").find(
             "\"key\":\"active\"") != std::string::npos,
         "displayConfigGet should pass key to hook");
  EXPECT(fixture.service->displayConfigSet("display-a", "preferences: {}")
                 .find("\"patch\":\"preferences: {}\"") != std::string::npos,
         "displayConfigSet should pass patch text to hook");
  EXPECT(fixture.service->displaySelect("display-b")
                 .find("\"restartRequired\":true") != std::string::npos,
         "displaySelect should return hook JSON");
}

void testDisplayApiMethodsReturnErrorsWhenHooksMissing() {
  SceneSharedPtr scene = Scene::create(nullptr);
  EditorState editorState;
  CommandBus bus;
  LxeEditorApiService service(bus, editorState, *scene,
                              LxeEditorApiService::Hooks{});

  EXPECT(service.displayList().find("\"ok\":false") != std::string::npos,
         "displayList should return in-band error without hook");
  EXPECT(service.displayActive().find("display config unavailable") !=
             std::string::npos,
         "displayActive should return clear unavailable error");
  EXPECT(service.displayConfigGet("active").find(
             "display config unavailable") != std::string::npos,
         "displayConfigGet should return config unavailable error without hook");
  EXPECT(service.displayConfigSet("default", "{}")
             .find("display config unavailable") != std::string::npos,
         "displayConfigSet should return config unavailable error without hook");
  EXPECT(service.displaySelect("display-a")
             .find("display config unavailable") != std::string::npos,
         "displaySelect should return config unavailable error without hook");
  EXPECT(service.displayConfigGet("active").find("\"ok\":false") !=
             std::string::npos,
         "displayConfigGet should return in-band error without hook");
  EXPECT(service.displayConfigSet("default", "{}").find("\"ok\":false") !=
             std::string::npos,
         "displayConfigSet should return in-band error without hook");
  EXPECT(service.displaySelect("display-a").find("\"ok\":false") !=
             std::string::npos,
         "displaySelect should return in-band error without hook");
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
  EXPECT(snapshot.toolbar.mode == ApiEditorMode::Selection,
         "toolbar snapshot should expose editor mode from hook provider");
  EXPECT(
      snapshot.toolbar.camera == ApiCameraControlMode::FreeFly,
      "toolbar snapshot should expose camera control mode from hook provider");
  const std::string toolbarJson = LX_demo::lxe_editor::toJson(snapshot.toolbar);
  EXPECT(toolbarJson.find("\"mode\":\"selection\"") != std::string::npos,
         "toolbar JSON should expose editor mode as selection");
  EXPECT(toolbarJson.find("\"camera\":\"freefly\"") != std::string::npos,
         "toolbar JSON should expose camera control mode separately");
  EXPECT(toolbarJson.find("\"editMode\"") == std::string::npos,
         "toolbar JSON should not expose legacy editMode");
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

void testRefreshEmitsDirtyAndPreviewChangeEvents() {
  Fixture fixture;
  const ApiEventCursor cursor = fixture.service->currentCursor();

  fixture.hookState.scene.dirty = true;
  fixture.hookState.toolbar.previewEnabled = true;
  fixture.service->refresh();

  const ApiEventBatch batch = fixture.service->collectEventsSince(cursor);
  bool sawDirty = false;
  bool sawPreview = false;
  for (const auto &event : batch.events) {
    sawDirty = sawDirty || event.type == ApiEventType::DirtyChanged;
    sawPreview = sawPreview || event.type == ApiEventType::PreviewChanged;
  }
  EXPECT(sawDirty, "refresh should emit dirty.changed when dirty flips");
  EXPECT(sawPreview, "refresh should emit preview.changed when preview flips");
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

  const ApiEventBatch batch = fixture.service->collectEventsSince(cursor);
  bool sawRuntimeNodeChanged = false;
  for (const auto &event : batch.events) {
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
             "scene-node payload stable name should preserve constructor "
             "identity");
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

  const ApiCommandResponse response =
      fixture.service->executeCommand(ApiCommandRequest{.line = "move_node"});
  EXPECT(response.ok, "command-driven node mutation should succeed");

  const ApiEventBatch batch = fixture.service->collectEventsSince(cursor);
  EXPECT(batch.events.size() >= 2,
         "command-driven node mutation should emit command and runtime events");

  std::optional<ApiEvent> commandEvent;
  std::optional<ApiEvent> sceneNodeEvent;
  for (const auto &event : batch.events) {
    if (event.type == ApiEventType::CommandExecuted &&
        !commandEvent.has_value()) {
      commandEvent = event;
    }
    if (event.type == ApiEventType::SceneNodeChanged &&
        !sceneNodeEvent.has_value()) {
      sceneNodeEvent = event;
    }
  }

  EXPECT(commandEvent.has_value(),
         "command-driven node mutation should emit command.executed");
  EXPECT(sceneNodeEvent.has_value(),
         "command-driven node mutation should emit scene_node.changed");
  if (commandEvent.has_value() && sceneNodeEvent.has_value()) {
    EXPECT(commandEvent->sequence < sceneNodeEvent->sequence,
           "command.executed should keep an earlier API sequence than its "
           "runtime side effect");
    EXPECT(sceneNodeEvent->sceneNode.has_value(),
           "scene_node.changed should carry scene-node payload");
    if (sceneNodeEvent->sceneNode.has_value()) {
      EXPECT(
          sceneNodeEvent->sceneNode->aspects.size() == 1 &&
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

  const ApiCommandResponse response =
      fixture.service->executeCommand(ApiCommandRequest{.line = "move_newer"});
  EXPECT(response.ok,
         "executeCommand should succeed with pre-queued runtime events");

  const ApiEventBatch batch = fixture.service->collectEventsSince(cursor);
  EXPECT(batch.events.size() >= 3, "pre-queued runtime event plus command side "
                                   "effect should yield at least three events");

  std::vector<ApiEvent> sceneNodeEvents;
  std::optional<ApiEvent> commandEvent;
  for (const auto &event : batch.events) {
    if (event.type == ApiEventType::SceneNodeChanged) {
      sceneNodeEvents.push_back(event);
    }
    if (event.type == ApiEventType::CommandExecuted &&
        !commandEvent.has_value()) {
      commandEvent = event;
    }
  }

  EXPECT(sceneNodeEvents.size() >= 2, "pre-queued runtime event and command "
                                      "side effect should both be mirrored");
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
             "older queued runtime event should flush before dispatching a new "
             "command");
      EXPECT(commandEvent->sequence > sceneNodeEvents[0].sequence,
             "older queued runtime event should get an earlier sequence than "
             "the new command");
      EXPECT(commandEvent->sequence < sceneNodeEvents[1].sequence,
             "command.executed should keep an earlier sequence than its own "
             "runtime side effect");
      EXPECT(sceneNodeEvents[1].sceneNode->stableNodeName == "newer_node",
             "later runtime event should belong to the command-mutated node");
    }
  }
}

void testRuntimeCameraPropertyMutationEmitsApiSceneNodeChangedEvent() {
  Fixture fixture;
  const auto cameraNode = SceneNode::create("camera_node");
  cameraNode->setName("camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  fixture.scene->addCamera(cameraNode);

  const ApiEventCursor cursor = fixture.service->currentCursor();
  camera->get().setFovY(88.0f);
  fixture.service->refresh();

  const ApiEventBatch batch = fixture.service->collectEventsSince(cursor);
  bool sawCameraEvent = false;
  for (const auto &event : batch.events) {
    if (event.type != ApiEventType::SceneNodeChanged ||
        !event.sceneNode.has_value()) {
      continue;
    }

    if (event.sceneNode->stableNodeName != cameraNode->getNodeName()) {
      continue;
    }

    sawCameraEvent = true;
    EXPECT(event.sceneNode->path == cameraNode->getPath(),
           "camera property event should preserve camera node path");
    EXPECT(event.sceneNode->aspects.size() == 1,
           "camera property event should keep a minimal aspect list");
    if (event.sceneNode->aspects.size() == 1) {
      EXPECT(event.sceneNode->aspects.front() == "cameraProperties",
             "API event should serialize camera property aspect");
    }
    EXPECT(event.payloadJson == toJson(*event.sceneNode),
           "camera property event payloadJson should match serialized payload");
  }

  EXPECT(sawCameraEvent,
         "runtime camera property mutation should be mirrored into API events");
}

void testRuntimeLightPropertyMutationEmitsApiSceneNodeChangedEvent() {
  Fixture fixture;
  const auto lightNode = SceneNode::create("light_node");
  lightNode->setName("sun");
  fixture.scene->addRenderable(lightNode);
  const auto light = std::make_shared<DirectionalLight>();
  fixture.scene->attachLight(lightNode, light);
  fixture.service->refresh();
  const ApiEventCursor cursor = fixture.service->currentCursor();

  light->setIntensity(4.5f);
  fixture.service->refresh();

  const ApiEventBatch batch = fixture.service->collectEventsSince(cursor);
  bool sawLightEvent = false;
  for (const auto &event : batch.events) {
    if (event.type != ApiEventType::SceneNodeChanged ||
        !event.sceneNode.has_value()) {
      continue;
    }

    if (event.sceneNode->stableNodeName != lightNode->getNodeName()) {
      continue;
    }

    if (event.sceneNode->aspects.size() != 1 ||
        event.sceneNode->aspects.front() != "lightProperties") {
      continue;
    }

    sawLightEvent = true;
    EXPECT(event.sceneNode->path == lightNode->getPath(),
           "light property event should preserve light node path");
    EXPECT(event.sceneNode->aspects.size() == 1,
           "light property event should keep a minimal aspect list");
    if (event.sceneNode->aspects.size() == 1) {
      EXPECT(event.sceneNode->aspects.front() == "lightProperties",
             "API event should serialize light property aspect");
    }
    EXPECT(event.payloadJson == toJson(*event.sceneNode),
           "light property event payloadJson should match serialized payload");
  }

  EXPECT(sawLightEvent,
         "runtime light property mutation should be mirrored into API events");
}

void testProjectScopedSceneReplacementCommandsEmitSceneLoadedEvents() {
  Fixture fixture;
  fixture.bus.registerHandler("project", "project <args>",
                              [](std::vector<std::string> args) {
                                if (!args.empty() && args[0] == "init") {
                                  return CommandResult{
                                      true, "project initialized",
                                      "{\"projectId\":\"demo\"}"};
                                }
                                return CommandResult{false, "bad project", {}};
                              });
  fixture.bus.registerHandler("scene", "scene <args>",
                              [](std::vector<std::string> args) {
                                if (!args.empty() && args[0] == "open") {
                                  return CommandResult{
                                      true, "scene opened",
                                      "{\"path\":\"scenes/main.scene.yaml\"}"};
                                }
                                if (!args.empty() && args[0] == "save") {
                                  return CommandResult{
                                      true, "scene saved",
                                      "{\"path\":\"scenes/main.scene.yaml\"}"};
                                }
                                return CommandResult{false, "bad scene", {}};
                              });

  ApiEventCursor cursor = fixture.service->currentCursor();
  EXPECT(fixture.service
             ->executeCommand(ApiCommandRequest{.line = "project init empty"})
             .ok,
         "project init command should succeed");
  ApiEventBatch batch = fixture.service->collectEventsSince(cursor);
  bool sawProjectInitLoaded = false;
  for (const auto &event : batch.events) {
    sawProjectInitLoaded =
        sawProjectInitLoaded || event.type == ApiEventType::SceneLoaded;
  }
  EXPECT(sawProjectInitLoaded,
         "project init should emit a scene.loaded API event");

  cursor = fixture.service->currentCursor();
  EXPECT(fixture.service
             ->executeCommand(ApiCommandRequest{.line = "scene open main"})
             .ok,
         "scene open command should succeed");
  batch = fixture.service->collectEventsSince(cursor);
  bool sawSceneOpenLoaded = false;
  for (const auto &event : batch.events) {
    sawSceneOpenLoaded =
        sawSceneOpenLoaded || event.type == ApiEventType::SceneLoaded;
  }
  EXPECT(sawSceneOpenLoaded, "scene open should emit a scene.loaded API event");

  cursor = fixture.service->currentCursor();
  EXPECT(fixture.service->executeCommand(ApiCommandRequest{.line = "scene save"})
             .ok,
         "scene save command should succeed");
  batch = fixture.service->collectEventsSince(cursor);
  bool sawSceneSaved = false;
  for (const auto &event : batch.events) {
    sawSceneSaved = sawSceneSaved || event.type == ApiEventType::SceneSaved;
  }
  EXPECT(sawSceneSaved, "scene save should emit a scene.saved API event");
}

void testApiTokenStatePersistsSingleGeneratedToken() {
  const auto rootDir =
      std::filesystem::temp_directory_path() / "lxengine_api_token_state_test";
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
  testDisplayCommandsReturnStructuredHookJson();
  testCaptureStateUsesHooksAndEditorSelection();
  testRefreshEmitsDirtyAndPreviewChangeEvents();
  testRuntimeSceneNodeMutationEmitsApiSceneNodeChangedEvent();
  testCommandDrivenSceneMutationKeepsCommandEventBeforeSceneNodeChanged();
  testExecuteCommandFlushesOlderQueuedRuntimeEventsBeforeNewCommand();
  testRuntimeCameraPropertyMutationEmitsApiSceneNodeChangedEvent();
  testRuntimeLightPropertyMutationEmitsApiSceneNodeChangedEvent();
  testProjectScopedSceneReplacementCommandsEmitSceneLoadedEvents();
  testRecordingToolsRecordMcpCommand();
  testBuildInfoExposesGitIdentityFields();
  testDisplayApiMethodsUseHooks();
  testDisplayApiMethodsReturnErrorsWhenHooksMissing();
  testApiTokenStatePersistsSingleGeneratedToken();

  if (failures != 0) {
    std::cerr << failures << " API service test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] lxe_editor API service tests\n";
  return 0;
}
