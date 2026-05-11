#include "core/editor/command_bus.hpp"
#include "core/editor/editor_state.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/automation_token_state.hpp"
#include "demos/lxe_editor/editor_automation_protocol.hpp"
#include "demos/lxe_editor/editor_automation_service.hpp"

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
  AutomationSceneSummary scene;
  AutomationCameraSnapshot cameras;
  AutomationToolbarSnapshot toolbar;
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
    hookState.scene.sourceKind = AutomationSceneSourceKind::Local;
    hookState.scene.permission = AutomationPermissionLevel::User;
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

    hookState.toolbar.editMode = AutomationEditMode::Orbit;
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

  const AutomationEventCursor cursor = fixture.service->currentCursor();
  const AutomationCommandResponse response = fixture.service->executeCommand(
      AutomationCommandRequest{.line = "echo hello"});
  EXPECT(response.ok, "command execution should succeed");
  EXPECT(response.line == "echo hello", "response should preserve command line");
  EXPECT(response.message == "hello",
         "response should mirror command bus message");
  EXPECT(response.structuredJson == "{\"kind\":\"echo\"}",
         "response should mirror structured payload");
  EXPECT(fixture.bus.history().size() == 1,
         "command execution should go through command bus history");

  const AutomationEventBatch batch =
      fixture.service->collectEventsSince(cursor);
  EXPECT(batch.events.size() == 1,
         "command execution should emit one command event");
  EXPECT(batch.events.front().type == AutomationEventType::CommandExecuted,
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

  const AutomationStateSnapshot snapshot = fixture.service->captureState();
  EXPECT(snapshot.scene.sceneName == "Scene",
         "scene summary should come from hook provider");
  EXPECT(snapshot.scene.currentDocumentPath == "data/scenes/test.scene.yaml",
         "scene path should come from hook provider");
  EXPECT(snapshot.toolbar.editMode == AutomationEditMode::Orbit,
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
  const AutomationEventCursor cursor = fixture.service->currentCursor();

  fixture.hookState.scene.dirty = true;
  fixture.hookState.toolbar.previewEnabled = true;
  fixture.hookState.toolbar.editMode = AutomationEditMode::Selection;
  fixture.service->refresh();

  const AutomationEventBatch batch =
      fixture.service->collectEventsSince(cursor);
  bool sawDirty = false;
  bool sawPreview = false;
  bool sawMode = false;
  for (const auto& event : batch.events) {
    sawDirty = sawDirty || event.type == AutomationEventType::DirtyChanged;
    sawPreview = sawPreview || event.type == AutomationEventType::PreviewChanged;
    sawMode = sawMode || event.type == AutomationEventType::ModeChanged;
  }
  EXPECT(sawDirty, "refresh should emit dirty.changed when dirty flips");
  EXPECT(sawPreview,
         "refresh should emit preview.changed when preview flips");
  EXPECT(sawMode, "refresh should emit mode.changed when edit mode flips");
}

void testAutomationTokenStatePersistsSingleGeneratedToken() {
  const auto rootDir = std::filesystem::temp_directory_path() /
                       "lxengine_automation_token_state_test";
  std::error_code ec;
  std::filesystem::remove_all(rootDir, ec);

  const AutomationTokenState state(rootDir);
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
  testAutomationTokenStatePersistsSingleGeneratedToken();

  if (failures != 0) {
    std::cerr << failures << " API service test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] lxe_editor API service tests\n";
  return 0;
}
