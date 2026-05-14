#include "core/asset/mesh.hpp"
#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/console_input_controller.hpp"
#include "core/editor/console_panel.hpp"
#include "core/editor/editor_state.hpp"
#include "core/math/quat.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/scene_interaction_controller.hpp"
#include "demos/lxe_editor/scene_view_rect.hpp"
#include "demos/lxe_editor/lxe_editor_commands.hpp"
#include "demos/lxe_editor/ui_overlay.hpp"

#include <imgui.h>

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace LX_core;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kFloatEps = 1e-4f;

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

[[nodiscard]] bool nearlyEqual(const float a, const float b,
                               const float eps = kFloatEps) {
  return std::fabs(a - b) <= eps;
}

LX_core::MeshSharedPtr makeUnitSquareMesh() {
  auto vb = LX_core::VertexBuffer<LX_core::VertexPos>::create(
      std::vector<LX_core::VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = LX_core::IndexBuffer::create({0, 1, 2});
  return LX_core::Mesh::create(vb, ib,
                               LX_core::BoundingBox{{0, 0, 0}, {1, 1, 0}});
}

struct CommandFixture {
  SceneSharedPtr scene = Scene::create(nullptr);
  EditorState editorState;
  CommandBus bus;
  SceneNodeSharedPtr world = SceneNode::create("node_world");
  SceneNodeSharedPtr cube = SceneNode::create("node_cube");
  SceneNodeSharedPtr cameraNode = SceneNode::create("node_camera");
  SceneNodeSharedPtr lightNode = SceneNode::create("node_light");
  CameraComponent *camera = nullptr;

  CommandFixture() {
    world->setName("world");
    scene->addRenderable(world);

    cube->setName("cube");
    cube->addComponent<LX_core::MeshComponent>(makeUnitSquareMesh());
    cube->setTranslation({-0.5f, -0.5f, -5.0f});
    cube->setParent(world);
    scene->addRenderable(cube);

    cameraNode->setName("camera_main");
    const auto cameraComponent = cameraNode->addComponent<CameraComponent>();
    camera = &cameraComponent->get();
    camera->setFovY(60.0f);
    scene->addCamera(cameraNode);

    lightNode->setName("dir_light");
    scene->addRenderable(lightNode);
    scene->attachLight(
        lightNode,
        std::dynamic_pointer_cast<DirectionalLight>(scene->getLights().front()));

    registerBuiltinCommands(bus, editorState, *scene);
  }
};

struct SceneViewerCommandFixture {
  CommandFixture base;
  LX_demo::lxe_editor::SceneInteractionController interaction{
      base.bus, base.editorState, *base.scene};
  bool dirty = false;
  bool debugEnabled = false;
  int editMode =
      static_cast<int>(LX_demo::lxe_editor::UiOverlay::EditorMode::Selection);
  int cameraControlMode =
      static_cast<int>(LX_demo::lxe_editor::UiOverlay::CameraControlMode::Orbit);
  std::optional<std::string> documentPath = std::string("data/scenes/test.scene.yaml");
  std::optional<std::string> sourceKind = std::string("local");
  LX_demo::lxe_editor::SceneViewRect rect{
      .x = 0.0f, .y = 0.0f, .width = 800.0f, .height = 600.0f};

  SceneViewerCommandFixture() {
    base.editorState.setEditorCamera(base.cameraNode);
    LX_core::registerBuiltinCommands(
        base.bus, base.editorState, *base.scene,
        LX_core::SceneIoContext{
            .cameraControl =
                [this](const std::vector<std::string>& args) {
                  if (args.size() != 2) {
                    return LX_core::CommandResult{
                        false, "usage: cam control (orbit|freefly|status)", {}};
                  }
                  if (args[1] == "status") {
                    const std::string camera =
                        cameraControlMode ==
                                static_cast<int>(
                                    LX_demo::lxe_editor::UiOverlay::
                                        CameraControlMode::FreeFly)
                            ? "freefly"
                            : "orbit";
                    LX_core::CommandResult result{
                        true, "camera " + camera,
                        "{\"camera\":\"" + camera + "\"}"};
                    result.metadata[std::string(
                        LX_core::kCommandResultClearRedoOnSuccessMetadataKey)] =
                        "false";
                    result.metadata[std::string(
                        LX_core::kCommandResultClearUndoOnSuccessMetadataKey)] =
                        "false";
                    return result;
                  }
                  if (args[1] != "orbit" && args[1] != "freefly") {
                    return LX_core::CommandResult{
                        false, "unknown camera control: " + args[1], {}};
                  }
                  const int previous = cameraControlMode;
                  cameraControlMode =
                      args[1] == "orbit"
                          ? static_cast<int>(
                                LX_demo::lxe_editor::UiOverlay::
                                    CameraControlMode::Orbit)
                          : static_cast<int>(
                                LX_demo::lxe_editor::UiOverlay::
                                    CameraControlMode::FreeFly);
                  LX_core::CommandResult result{
                      true, "camera " + args[1],
                      "{\"camera\":\"" + args[1] + "\"}"};
                  result.metadata["inverse.line"] =
                      previous == static_cast<int>(
                                      LX_demo::lxe_editor::UiOverlay::
                                          CameraControlMode::FreeFly)
                          ? "cam control freefly"
                          : "cam control orbit";
                  return result;
                },
        });
    LX_demo::lxe_editor::registerLxeEditorCommands(
        base.bus,
        LX_demo::lxe_editor::LxeEditorCommandContext{
            .editorState = base.editorState,
            .scene = *base.scene,
            .interaction = interaction,
            .getEditMode = [this]() { return editMode; },
            .setEditMode = [this](const int modeCode) { editMode = modeCode; },
            .getCameraControlMode = [this]() { return cameraControlMode; },
            .setCameraControlMode = [this](const int modeCode) {
              cameraControlMode = modeCode;
            },
            .sceneViewRect = [this]() { return rect; },
            .dirty = [this]() { return dirty; },
            .permission = []() { return std::string("user"); },
            .debugEnabled = [this]() { return debugEnabled; },
            .setDebugEnabled = [this](const bool enabled) {
              debugEnabled = enabled;
            },
            .currentDocumentPath = [this]() { return documentPath; },
            .currentSourceKind = [this]() { return sourceKind; },
            .persistedHistory = []() { return std::vector<std::string>{}; },
        });
  }
};

struct SceneViewerPickFixture {
  LX_core::EditorState editorState;
  LX_core::CommandBus bus;
  LX_core::ConsolePanel consolePanel{bus};
  LX_core::SceneSharedPtr scene = LX_core::Scene::create(nullptr);
  LX_core::SceneNodeSharedPtr editorCameraNode =
      LX_core::SceneNode::create("editor_cam_node");
  LX_core::SceneNodeSharedPtr gameCameraNode =
      LX_core::SceneNode::create("game_cam_node");
  LX_core::SceneNodeSharedPtr targetNode = LX_core::SceneNode::create("cube");
  LX_demo::lxe_editor::SceneInteractionController interaction{
      bus, editorState, *scene};
  bool debugEnabled = false;
  int editMode =
      static_cast<int>(LX_demo::lxe_editor::UiOverlay::EditorMode::Selection);
  int cameraControlMode =
      static_cast<int>(LX_demo::lxe_editor::UiOverlay::CameraControlMode::Orbit);
  LX_demo::lxe_editor::SceneViewRect rect{
      .x = 0.0f, .y = 0.0f, .width = 800.0f, .height = 600.0f};

  SceneViewerPickFixture() {
    targetNode->setName("cube");
    targetNode->addComponent<LX_core::MeshComponent>(makeUnitSquareMesh());
    targetNode->setTranslation({-0.5f, -0.5f, -5.0f});
    scene->addRenderable(targetNode);

    editorCameraNode->setName("editor_cam");
    auto editorCamera =
        editorCameraNode->addComponent<LX_core::CameraComponent>();
    editorCamera->get().setAspect(1.0f);
    scene->addCamera(editorCameraNode);

    gameCameraNode->setName("game_cam");
    gameCameraNode->addComponent<LX_core::CameraComponent>();
    scene->addCamera(gameCameraNode);

    editorState.setEditorCamera(editorCameraNode);
    editorState.setPreviewCamera(gameCameraNode);
    (void)editorState.syncActiveCamera(*scene);
    LX_core::registerBuiltinCommands(bus, editorState, *scene);
    LX_demo::lxe_editor::registerLxeEditorCommands(
        bus,
        LX_demo::lxe_editor::LxeEditorCommandContext{
            .editorState = editorState,
            .scene = *scene,
            .interaction = interaction,
            .getEditMode = [this]() { return editMode; },
            .setEditMode = [this](const int modeCode) { editMode = modeCode; },
            .getCameraControlMode = [this]() { return cameraControlMode; },
            .setCameraControlMode = [this](const int modeCode) {
              cameraControlMode = modeCode;
            },
            .sceneViewRect = [this]() { return rect; },
            .dirty = []() { return false; },
            .permission = []() { return std::string("user"); },
            .debugEnabled = [this]() { return debugEnabled; },
            .setDebugEnabled = [this](const bool enabled) {
              debugEnabled = enabled;
            },
            .currentDocumentPath = []() { return std::optional<std::string>{}; },
            .currentSourceKind = []() { return std::optional<std::string>{}; },
            .persistedHistory = []() { return std::vector<std::string>{}; },
            .appendConsoleDebugLine =
                [this](std::string_view line) { consolePanel.appendSystemLine(line); },
        });
  }
};

void testDispatchRecordsHistoryAndPreservesQuotedToken() {
  CommandBus bus;
  std::vector<std::string> capturedArgs;
  bus.registerHandler(
      "select", "select <path>",
      [&](std::vector<std::string> args) {
        capturedArgs = std::move(args);
        return CommandResult{true, "selected", {}};
      });

  const CommandResult result = bus.dispatch("select \"node with spaces\"");
  EXPECT(result.ok, "quoted dispatch succeeds");
  EXPECT(capturedArgs.size() == 1, "quoted token stays single argument");
  EXPECT(capturedArgs[0] == "node with spaces",
         "quoted token content preserved");
  EXPECT(bus.history().size() == 1, "successful dispatch enters history");
  EXPECT(bus.history().back().line == "select \"node with spaces\"",
         "history stores original line");
  EXPECT(bus.history().back().result.ok, "history stores successful result");
  EXPECT(bus.history().back().timestampMs > 0, "history stores timestamp");
}

void testEscapesReachHandler() {
  CommandBus bus;
  std::vector<std::string> capturedArgs;
  bus.registerHandler(
      "echo", "echo <text>",
      [&](std::vector<std::string> args) {
        capturedArgs = std::move(args);
        return CommandResult{true, "ok", {}};
      });

  const CommandResult result =
      bus.dispatch("echo \"line1\\nline2\\\\tail\\\"quote\"");
  EXPECT(result.ok, "escape-heavy command succeeds");
  EXPECT(capturedArgs.size() == 1, "escaped quoted token stays one arg");
  EXPECT(capturedArgs[0] == std::string("line1\nline2\\tail\"quote"),
         "escapes decode correctly");
}

void testUnknownVerbAndParseErrorsStayInBand() {
  CommandBus bus;

  const CommandResult unknown = bus.dispatch("missing arg");
  EXPECT(!unknown.ok, "unknown command returns failure");
  EXPECT(unknown.message == "unknown command: missing",
         "unknown command message matches contract");

  const CommandResult parseError = bus.dispatch("select \"unterminated");
  EXPECT(!parseError.ok, "parse error returns failure");
  EXPECT(parseError.message.find("parse error:") != std::string::npos,
         "parse error message is surfaced");
  EXPECT(bus.history().size() == 2, "failed dispatches also enter history");
}

void testHandlerExceptionBecomesFailedResult() {
  CommandBus bus;
  bus.registerHandler(
      "boom", "boom",
      [](std::vector<std::string>) -> CommandResult {
        throw std::runtime_error("boom");
      });

  const CommandResult result = bus.dispatch("boom");
  EXPECT(!result.ok, "exception path returns failure");
  EXPECT(result.message.find("exception: boom") != std::string::npos,
         "exception text propagated to result");
}

void testDispatchScriptSkipsCommentsAndContinuesAfterFailure() {
  CommandBus bus;
  int echoCount = 0;
  bus.registerHandler(
      "echo", "echo <text>",
      [&](std::vector<std::string> args) {
        ++echoCount;
        return CommandResult{true, args.empty() ? std::string{} : args.front(), {}};
      });
  bus.registerHandler(
      "fail", "fail",
      [](std::vector<std::string>) { return CommandResult{false, "failed", {}}; });

  const std::vector<CommandResult> results = bus.dispatchScript(
      "# comment\n\n echo first\nfail\necho second\n");
  EXPECT(results.size() == 3, "script returns one result per non-comment line");
  EXPECT(results[0].ok, "first script command succeeds");
  EXPECT(!results[1].ok, "middle script command may fail");
  EXPECT(results[2].ok, "script continues after failure");
  EXPECT(echoCount == 2, "later command still runs after failure");
  EXPECT(bus.history().size() == 3, "script dispatch contributes to history");
}

void testNestedDispatchesShareTopLevelDispatchIdentity() {
  CommandBus bus;
  std::optional<u64> outerActiveDispatchId;
  std::optional<u64> innerActiveDispatchId;

  bus.registerHandler(
      "inner", "inner",
      [&](std::vector<std::string>) {
        innerActiveDispatchId = bus.activeTopLevelDispatchId();
        return CommandResult{true, "inner ok", {}, {}};
      });
  bus.registerHandler(
      "outer", "outer",
      [&](std::vector<std::string>) {
        outerActiveDispatchId = bus.activeTopLevelDispatchId();
        return bus.dispatch("inner");
      });

  const CommandResult result = bus.dispatch("outer");
  EXPECT(result.ok, "nested dispatch test should succeed");
  EXPECT(outerActiveDispatchId.has_value(),
         "outer handler should observe an active top-level dispatch id");
  EXPECT(innerActiveDispatchId.has_value(),
         "inner handler should observe an active top-level dispatch id");
  EXPECT(outerActiveDispatchId == innerActiveDispatchId,
         "nested dispatches should share one top-level dispatch id");
  EXPECT(bus.history().size() == 2,
         "nested dispatch test should record both inner and outer commands");
  EXPECT(bus.history()[0].topLevelDispatchId == bus.history()[1].topLevelDispatchId,
         "nested history entries should share one top-level dispatch id");
  EXPECT(!bus.activeTopLevelDispatchId().has_value(),
         "active top-level dispatch id should clear after dispatch returns");
}

void testListVerbsAndBriefAreDeterministic() {
  CommandBus bus;
  bus.registerHandler("zeta", "last", [](std::vector<std::string>) {
    return CommandResult{true, {}, {}};
  });
  bus.registerHandler("alpha", "first", [](std::vector<std::string>) {
    return CommandResult{true, {}, {}};
  });

  const std::vector<std::string> verbs = bus.listVerbs();
  EXPECT(verbs.size() == 2, "listVerbs returns all verbs");
  EXPECT(verbs[0] == "alpha" && verbs[1] == "zeta",
         "listVerbs sorts output for stable UI/autocomplete");
  EXPECT(bus.brief("alpha") == "first", "brief returns registered summary");
  EXPECT(bus.brief("missing").empty(), "brief returns empty for unknown verb");

  bus.unregisterHandler("alpha");
  EXPECT(bus.listVerbs().size() == 1, "unregister removes verb");
}

void testEditorStateUsesWeakSelection() {
  EditorState state;
  auto node = SceneNode::create("selected_node");

  state.select({node});
  EXPECT(state.getSelected().size() == 1 && state.getSelected()[0] == node,
         "selected node resolves while alive");
  EXPECT(state.getPrimarySelected().has_value() &&
             &state.getPrimarySelected()->get() == node.get(),
         "primary selection tracks most recent node");

  const auto second = SceneNode::create("second_node");
  state.selectAdd(second);
  EXPECT(state.getSelected().size() == 2, "selectAdd appends live selection");
  EXPECT(state.getPrimarySelected().has_value() &&
             &state.getPrimarySelected()->get() == second.get(),
         "primary selection becomes last added node");

  state.selectRemove(second);
  EXPECT(state.getSelected().size() == 1 && state.getSelected()[0] == node,
         "selectRemove erases requested node only");

  node.reset();
  EXPECT(state.getSelected().empty(), "expired selected nodes drop from live snapshot");
  EXPECT(!state.getPrimarySelected().has_value(),
         "expired selected nodes clear primary selection view");

  state.deselect();
  EXPECT(state.getSelected().empty(), "deselect keeps state empty");

  EXPECT(!state.isPreviewEnabled(), "preview defaults to off");
  state.setPreviewEnabled(true);
  EXPECT(state.isPreviewEnabled(), "preview setter turns preview on");
  state.togglePreviewEnabled();
  EXPECT(!state.isPreviewEnabled(), "preview toggle flips preview flag");
}

void testBuiltinHelpSelectAndDeselect() {
  CommandFixture fixture;

  const CommandResult help = fixture.bus.dispatch("help select");
  EXPECT(help.ok, "help select succeeds");
  EXPECT(help.message.find("select - select <path>") != std::string::npos,
         "help select shows brief");

  const CommandResult helpAll = fixture.bus.dispatch("help");
  EXPECT(helpAll.ok, "help succeeds");
  EXPECT(helpAll.message.find("move - move <path> <x> <y> <z>") != std::string::npos,
         "help lists registered builtin commands");
  EXPECT(helpAll.structured.find("\"verbs\"") != std::string::npos,
         "help returns structured verb list");

  const CommandResult select = fixture.bus.dispatch("select /world/cube");
  EXPECT(select.ok, "select succeeds for existing path");
  EXPECT(fixture.editorState.getSelected().size() == 1 && fixture.editorState.getSelected()[0] == fixture.cube,
         "select updates EditorState");

  const CommandResult deselect = fixture.bus.dispatch("deselect");
  EXPECT(deselect.ok, "deselect succeeds");
  EXPECT(fixture.editorState.getSelected().empty(), "deselect clears EditorState");
}

void testBuiltinTransformCommandsAndGet() {
  CommandFixture fixture;

  const CommandResult move = fixture.bus.dispatch("move /world/cube 1 2 3");
  EXPECT(move.ok, "move succeeds");
  EXPECT(nearlyEqual(fixture.cube->getTranslation().x, 1.0f)
             && nearlyEqual(fixture.cube->getTranslation().y, 2.0f)
             && nearlyEqual(fixture.cube->getTranslation().z, 3.0f),
         "move updates translation");

  const CommandResult scaleUniform = fixture.bus.dispatch("scale /world/cube 2");
  EXPECT(scaleUniform.ok, "uniform scale succeeds");
  EXPECT(nearlyEqual(fixture.cube->getScale().x, 2.0f)
             && nearlyEqual(fixture.cube->getScale().y, 2.0f)
             && nearlyEqual(fixture.cube->getScale().z, 2.0f),
         "uniform scale updates all axes");

  const CommandResult rotate = fixture.bus.dispatch("rotate /world/cube 90 0 0");
  EXPECT(rotate.ok, "rotate succeeds");
  const Quatf expected =
      Quatf::fromAxisAngle(Vec3f{1.0f, 0.0f, 0.0f}, kPi * 0.5f).normalized();
  EXPECT(nearlyEqual(fixture.cube->getRotation().w, expected.w)
             && nearlyEqual(fixture.cube->getRotation().v.x, expected.v.x)
             && nearlyEqual(fixture.cube->getRotation().v.y, expected.v.y)
             && nearlyEqual(fixture.cube->getRotation().v.z, expected.v.z),
         "rotate writes quaternion converted from degrees");

  const CommandResult getTranslation = fixture.bus.dispatch("get /world/cube.translation");
  EXPECT(getTranslation.ok, "get translation succeeds");
  EXPECT(getTranslation.structured.find("\"x\":1") != std::string::npos,
         "get translation returns structured vector payload");

  const CommandResult getFov = fixture.bus.dispatch("get /camera_main.fov");
  EXPECT(getFov.ok, "get fov succeeds on camera node");
  EXPECT(getFov.message.find("60.000") != std::string::npos,
         "get fov reports camera value");
  EXPECT(getFov.structured.find("\"value\":60.000") != std::string::npos,
         "get fov returns structured numeric payload");
}

void testBuiltinListCommands() {
  CommandFixture fixture;

  const CommandResult listNodes = fixture.bus.dispatch("list nodes");
  EXPECT(listNodes.ok, "list nodes succeeds");
  EXPECT(listNodes.message.find("world") != std::string::npos,
         "list nodes includes world path segment");
  EXPECT(listNodes.structured.find("\"tree\"") != std::string::npos,
         "list nodes returns structured tree field");

  const CommandResult listCameras = fixture.bus.dispatch("list cameras");
  EXPECT(listCameras.ok, "list cameras succeeds");
  EXPECT(listCameras.message.find("/camera_main") != std::string::npos,
         "list cameras includes camera path");

  const CommandResult listLights = fixture.bus.dispatch("list lights");
  EXPECT(listLights.ok, "list lights succeeds");
  EXPECT(listLights.structured.find("\"count\":1") != std::string::npos,
         "list lights reports default light count");
}

void testBuiltinCommandErrors() {
  CommandFixture fixture;

  const CommandResult badSelect = fixture.bus.dispatch("select /missing");
  EXPECT(!badSelect.ok, "select fails for missing node");
  EXPECT(badSelect.message == "node not found: /missing",
         "select missing node returns explicit error");

  const CommandResult badMove = fixture.bus.dispatch("move /world/cube nope 0 0");
  EXPECT(!badMove.ok, "move fails on invalid float");
  EXPECT(badMove.message == "invalid float for move",
         "move invalid float error is stable");

  const CommandResult badScale = fixture.bus.dispatch("scale /world/cube 1 2");
  EXPECT(!badScale.ok, "scale fails on bad arity");
  EXPECT(badScale.message.find("usage: scale") != std::string::npos,
         "scale bad arity reports usage");

  const CommandResult badGet = fixture.bus.dispatch("get /world/cube.fov");
  EXPECT(!badGet.ok, "get fov fails on non-camera node");
  EXPECT(badGet.message == "field not available on node: fov",
         "get non-camera fov reports clear error");

  const CommandResult badList = fixture.bus.dispatch("list textures");
  EXPECT(!badList.ok, "list fails for unknown target");
  EXPECT(badList.message == "unknown list target: textures",
         "list unknown target error is stable");

  const CommandResult badHelp = fixture.bus.dispatch("help missing");
  EXPECT(!badHelp.ok, "help fails for unknown command");
  EXPECT(badHelp.message == "unknown command: missing",
         "help unknown command uses same not-found wording");
}



void testBuiltinAddRemoveSetCommands() {
  CommandFixture fixture;
  std::vector<LX_core::SceneEvent> runtimeEvents;
  auto subscription = fixture.scene->events().subscribe(
      [&](const LX_core::SceneEvent &event) { runtimeEvents.push_back(event); });

  const CommandResult selectResult = fixture.bus.dispatch("select /world/cube");
  EXPECT(selectResult.ok, "select before add commands succeeds");

  const CommandResult addMesh = fixture.bus.dispatch("add mesh probe");
  EXPECT(!addMesh.ok, "add mesh is rejected after concrete primitive kinds");

  const CommandResult addCamera = fixture.bus.dispatch("add camera debug_cam");
  EXPECT(addCamera.ok, "add camera succeeds");
  EXPECT(fixture.scene->findByPath("/world/cube/debug_cam") != nullptr,
         "add camera attaches under selected parent");
  EXPECT(fixture.scene->getCameras().size() == 2,
         "add camera grows camera registry");

  const usize lightCountBefore = fixture.scene->getLights().size();
  const CommandResult addLight = fixture.bus.dispatch("add light fill");
  EXPECT(addLight.ok, "add light succeeds");
  EXPECT(fixture.scene->findByPath("/world/cube/fill") != nullptr,
         "add light creates named placeholder node");
  EXPECT(fixture.scene->getLights().size() == lightCountBefore + 1,
         "add light appends a scene light resource");

  runtimeEvents.clear();
  const CommandResult setFov = fixture.bus.dispatch("set /camera_main.fov 75");
  EXPECT(setFov.ok, "set fov succeeds");
  EXPECT(nearlyEqual(fixture.camera->getFovY(), 75.0f),
         "set fov updates camera component");
  EXPECT(!runtimeEvents.empty(), "set fov should emit a runtime event");
  EXPECT(runtimeEvents.size() == 1,
         "set fov should emit exactly one camera-properties runtime event");
  EXPECT(runtimeEvents.back().type == LX_core::SceneEventType::SceneNodeChanged &&
             runtimeEvents.back().path == "/camera_main" &&
             runtimeEvents.back().aspects.size() == 1 &&
             runtimeEvents.back().aspects.front() ==
                 LX_core::SceneNodeAspect::CameraProperties,
         "set fov should emit a camera-properties scene node change");

  const CommandResult setTranslation =
      fixture.bus.dispatch("set /world/cube.translation 4 5 6");
  EXPECT(setTranslation.ok, "set translation succeeds");
  EXPECT(nearlyEqual(fixture.cube->getTranslation().x, 4.0f) &&
             nearlyEqual(fixture.cube->getTranslation().y, 5.0f) &&
             nearlyEqual(fixture.cube->getTranslation().z, 6.0f),
         "set translation updates node transform");

  const CommandResult setVisibility =
      fixture.bus.dispatch("set /world/cube.visibilityMask 255");
  EXPECT(setVisibility.ok, "set visibilityMask succeeds");
  EXPECT(fixture.cube->getVisibilityLayerMask() == 255u,
         "set visibilityMask updates node visibility");

  const CommandResult setNear = fixture.bus.dispatch("set /camera_main.near 0.5");
  EXPECT(setNear.ok, "set near succeeds");
  EXPECT(nearlyEqual(fixture.camera->getNearPlane(), 0.5f),
         "set near updates camera");

  const CommandResult setFar = fixture.bus.dispatch("set /camera_main.far 250");
  EXPECT(setFar.ok, "set far succeeds");
  EXPECT(nearlyEqual(fixture.camera->getFarPlane(), 250.0f),
         "set far updates camera");

  const CommandResult setProjection =
      fixture.bus.dispatch("set /camera_main.projection orthographic");
  EXPECT(setProjection.ok, "set projection succeeds");
  EXPECT(fixture.camera->getProjectionType() == CameraType::Orthographic,
         "set projection updates camera type");

  const CommandResult setCulling =
      fixture.bus.dispatch("set /camera_main.cullingMask 15");
  EXPECT(setCulling.ok, "set cullingMask succeeds");
  EXPECT(fixture.camera->getCullingMask() == 15u,
         "set cullingMask updates camera");

  const CommandResult setLightDirection =
      fixture.bus.dispatch("set /dir_light.direction 0 -1 0");
  EXPECT(setLightDirection.ok, "set light direction succeeds");

  const CommandResult setLightColor =
      fixture.bus.dispatch("set /dir_light.color 0.2 0.4 0.6");
  EXPECT(setLightColor.ok, "set light color succeeds");

  const CommandResult setLightIntensity =
      fixture.bus.dispatch("set /dir_light.intensity 3.5");
  EXPECT(setLightIntensity.ok, "set light intensity succeeds");
  const auto dirLight = std::dynamic_pointer_cast<DirectionalLight>(
      fixture.scene->getLights().front());
  EXPECT(nearlyEqual(dirLight->getDirection().x, 0.0f) &&
             nearlyEqual(dirLight->getDirection().y, -1.0f) &&
             nearlyEqual(dirLight->getDirection().z, 0.0f),
         "set direction updates scene light");
  EXPECT(nearlyEqual(dirLight->getColor().x, 0.2f) &&
             nearlyEqual(dirLight->getColor().y, 0.4f) &&
             nearlyEqual(dirLight->getColor().z, 0.6f) &&
             nearlyEqual(dirLight->getIntensity(), 3.5f),
         "set color/intensity updates scene light");

  fixture.lightNode->setName("sun");
  auto fillNode = SceneNode::create("fill_light_node");
  fillNode->setName("fill");
  fillNode->setParent(fixture.world);
  fixture.scene->addRenderable(fillNode);
  auto fillLight = std::make_shared<DirectionalLight>();
  fillLight->setIntensity(1.25f);
  fixture.scene->attachLight(fillNode, fillLight);

  runtimeEvents.clear();
  const CommandResult setSunIntensity =
      fixture.bus.dispatch("set /sun.intensity 4.5");
  EXPECT(setSunIntensity.ok,
         "set light intensity should work for renamed light nodes");
  EXPECT(nearlyEqual(dirLight->getIntensity(), 4.5f),
         "renamed light node should still target its attached light");
  EXPECT(nearlyEqual(fillLight->getIntensity(), 1.25f),
         "renamed light command should not mutate other scene lights");
  EXPECT(!runtimeEvents.empty(), "set intensity should emit a runtime event");
  EXPECT(runtimeEvents.size() == 1,
         "renamed light set should emit exactly one light-properties runtime event");
  EXPECT(runtimeEvents.back().type == LX_core::SceneEventType::SceneNodeChanged &&
             runtimeEvents.back().path == "/sun" &&
             runtimeEvents.back().aspects.size() == 1 &&
             runtimeEvents.back().aspects.front() ==
                 LX_core::SceneNodeAspect::LightProperties,
         "renamed light set should emit a light-properties scene node change");

  const CommandResult removeCamera =
      fixture.bus.dispatch("remove /world/cube/debug_cam");
  EXPECT(removeCamera.ok, "remove child camera succeeds");
  EXPECT(fixture.scene->findByPath("/world/cube/debug_cam") == nullptr,
         "removed camera path no longer resolves");

  const SceneNodeSharedPtr branch = SceneNode::create("node_branch");
  branch->setName("branch");
  branch->addComponent<LX_core::MeshComponent>(makeUnitSquareMesh());
  branch->setParent(fixture.cube);
  fixture.scene->addRenderable(branch);

  const SceneNodeSharedPtr leaf = SceneNode::create("node_leaf");
  leaf->setName("leaf");
  leaf->addComponent<LX_core::MeshComponent>(makeUnitSquareMesh());
  leaf->setParent(branch);
  fixture.scene->addRenderable(leaf);

  const SceneNodeSharedPtr branchCamera = SceneNode::create("node_branch_camera");
  branchCamera->setName("branch_cam");
  branchCamera->addComponent<CameraComponent>();
  branchCamera->setParent(branch);
  fixture.scene->addCamera(branchCamera);

  EXPECT(fixture.scene->findByPath("/world/cube/branch/leaf") == leaf.get(),
         "subtree leaf exists before removal");
  EXPECT(fixture.scene->findByPath("/world/cube/branch/branch_cam") == branchCamera.get(),
         "subtree camera exists before removal");
  EXPECT(fixture.scene->getCameras().size() == 2,
         "subtree camera is registered before removal");

  const CommandResult removeBranch = fixture.bus.dispatch("remove /world/cube/branch");
  EXPECT(removeBranch.ok, "remove subtree root succeeds");
  EXPECT(fixture.scene->findByPath("/world/cube/branch") == nullptr,
         "removed subtree root no longer resolves");
  EXPECT(fixture.scene->findByPath("/world/cube/branch/leaf") == nullptr,
         "removed subtree leaf no longer resolves");
  EXPECT(fixture.scene->findByPath("/world/cube/branch/branch_cam") == nullptr,
         "removed subtree camera no longer resolves");
  EXPECT(fixture.scene->getCameras().size() == 1,
         "remove subtree detaches descendant cameras from registry");

  const CommandResult removeRoot = fixture.bus.dispatch("remove /");
  EXPECT(!removeRoot.ok, "remove root should fail");
  EXPECT(removeRoot.message == "cannot remove scene root",
         "remove root should report explicit root-role restriction");
  EXPECT(fixture.scene->findByPath("/") == fixture.scene->getRootNode().get(),
         "remove root failure should leave scene root intact");
}

void testBuiltinCreatesAndEditsTypedLights() {
  CommandFixture fixture;

  const CommandResult addPoint = fixture.bus.dispatch("add light:point point_fill");
  EXPECT(addPoint.ok, "add point light succeeds");
  auto* pointNode = fixture.scene->findByPath("/point_fill");
  EXPECT(pointNode != nullptr, "point light node should exist");
  if (pointNode != nullptr) {
    const auto point = fixture.scene->getPointLight(*pointNode);
    EXPECT(point != nullptr, "point light runtime instance should attach");
    const CommandResult setRange =
        fixture.bus.dispatch("set /point_fill.light.range 7.5");
    EXPECT(setRange.ok, "set point light range succeeds");
    const CommandResult setColor =
        fixture.bus.dispatch("set /point_fill.light.color 0.3 0.4 0.5");
    EXPECT(setColor.ok, "set point light color succeeds");
    EXPECT(nearlyEqual(point->getRange(), 7.5f) &&
               nearlyEqual(point->getColor().z, 0.5f),
           "point light fields should update runtime state");
  }

  const CommandResult addSpot = fixture.bus.dispatch("add light:spot spot_key");
  EXPECT(addSpot.ok, "add spot light succeeds");
  auto* spotNode = fixture.scene->findByPath("/spot_key");
  EXPECT(spotNode != nullptr, "spot light node should exist");
  if (spotNode != nullptr) {
    const auto spot = fixture.scene->getSpotLight(*spotNode);
    EXPECT(spot != nullptr, "spot light runtime instance should attach");
    const CommandResult setDirection =
        fixture.bus.dispatch("set /spot_key.light.direction 0 -0.5 -1");
    EXPECT(setDirection.ok, "set spot light direction succeeds");
    const auto inner =
        fixture.bus.dispatch("set /spot_key.light.innerConeDegrees 15");
    const auto outer =
        fixture.bus.dispatch("set /spot_key.light.outerConeDegrees 30");
    EXPECT(inner.ok && outer.ok, "set spot cone fields succeeds");
    EXPECT(nearlyEqual(spot->getDirection().z, -1.0f) &&
               nearlyEqual(spot->getInnerConeDegrees(), 15.0f) &&
               nearlyEqual(spot->getOuterConeDegrees(), 30.0f),
           "spot light fields should update runtime state");
  }
}

void testBuiltinCamAndPreviewCommands() {
  CommandFixture fixture;

  const CommandResult camFov = fixture.bus.dispatch("cam fov 80");
  EXPECT(camFov.ok, "cam fov succeeds");
  EXPECT(nearlyEqual(fixture.camera->getFovY(), 80.0f),
         "cam fov updates active camera");
  EXPECT(camFov.metadata.find("editor_camera.resync") !=
             camFov.metadata.end() &&
             camFov.metadata.at("editor_camera.resync") == "true",
         "cam fov requests camera rig state resync");

  const CommandResult camLookAt =
      fixture.bus.dispatch("cam look-at 1 2 3 0 0 0");
  EXPECT(camLookAt.ok, "cam look-at succeeds");
  EXPECT(nearlyEqual(fixture.camera->getEyePosition().x, 1.0f) &&
             nearlyEqual(fixture.camera->getEyePosition().y, 2.0f) &&
             nearlyEqual(fixture.camera->getEyePosition().z, 3.0f),
         "cam look-at updates camera position");
  EXPECT(camLookAt.metadata.find("editor_camera.resync") !=
             camLookAt.metadata.end() &&
             camLookAt.metadata.at("editor_camera.resync") == "true",
         "cam look-at requests camera rig state resync");

  const CommandResult camReset = fixture.bus.dispatch("cam reset");
  EXPECT(camReset.ok, "cam reset succeeds");
  EXPECT(nearlyEqual(fixture.camera->getEyePosition().z, 3.0f),
         "cam reset restores default eye distance");
  EXPECT(camReset.metadata.find("editor_camera.resync") !=
             camReset.metadata.end() &&
             camReset.metadata.at("editor_camera.resync") == "true",
         "cam reset requests camera rig state resync");

  const SceneNodeSharedPtr editorCameraNode = SceneNode::create("node_editor_camera");
  editorCameraNode->setName("editor_cam");
  auto editorCamera = editorCameraNode->addComponent<CameraComponent>();
  editorCameraNode->setTranslation({1.0f, 2.0f, 3.0f});
  fixture.scene->addCamera(editorCameraNode);

  const SceneNodeSharedPtr gameCameraNode = SceneNode::create("node_game_camera");
  gameCameraNode->setName("game_cam");
  auto gameCamera = gameCameraNode->addComponent<CameraComponent>();
  gameCameraNode->setTranslation({7.0f, 8.0f, 9.0f});
  fixture.scene->addCamera(gameCameraNode);

  fixture.editorState.setEditorCamera(editorCameraNode);
  fixture.editorState.setPreviewCamera(gameCameraNode);
  editorCamera->get().updateMatrices();
  gameCamera->get().updateMatrices();
  const CommandResult camResetToGame =
      fixture.bus.dispatch("cam reset-editor-to-game");
  EXPECT(camResetToGame.ok, "cam reset-editor-to-game succeeds");
  EXPECT(nearlyEqual(editorCamera->get().getEyePosition().x, 7.0f) &&
             nearlyEqual(editorCamera->get().getEyePosition().y, 8.0f) &&
             nearlyEqual(editorCamera->get().getEyePosition().z, 9.0f),
         "cam reset-editor-to-game copies the preview camera pose onto the editor camera");
  EXPECT(camResetToGame.metadata.find("editor_camera.resync") !=
             camResetToGame.metadata.end() &&
             camResetToGame.metadata.at("editor_camera.resync") == "true",
         "cam reset-editor-to-game requests camera rig state resync");

  const CommandResult previewOn = fixture.bus.dispatch("preview on");
  EXPECT(previewOn.ok, "preview on succeeds");
  EXPECT(fixture.editorState.isPreviewEnabled(),
         "preview on updates editor state");
  EXPECT(previewOn.metadata.find("scene.rebuild") != previewOn.metadata.end() &&
             previewOn.metadata.at("scene.rebuild") == "true",
         "preview on requests scene rebuild so active camera resources refresh");

  const CommandResult previewToggle = fixture.bus.dispatch("preview toggle");
  EXPECT(previewToggle.ok, "preview toggle succeeds");
  EXPECT(!fixture.editorState.isPreviewEnabled(),
         "preview toggle flips editor state");
  EXPECT(previewToggle.metadata.find("scene.rebuild") !=
             previewToggle.metadata.end() &&
             previewToggle.metadata.at("scene.rebuild") == "true",
         "preview toggle requests scene rebuild so active camera resources refresh");
}

void testBuiltinRemainingCommandErrors() {
  CommandFixture fixture;

  const CommandResult badAdd = fixture.bus.dispatch("add volume x");
  EXPECT(!badAdd.ok, "add fails for unknown kind");
  EXPECT(badAdd.message == "unknown add target: volume",
         "add unknown kind returns stable error");

  const CommandResult badRemove = fixture.bus.dispatch("remove /missing");
  EXPECT(!badRemove.ok, "remove fails for missing node");
  EXPECT(badRemove.message == "node not found: /missing",
         "remove missing node returns stable error");

  const CommandResult badSet = fixture.bus.dispatch("set /world/cube.fov 90");
  EXPECT(!badSet.ok, "set fov on non-camera fails");
  EXPECT(badSet.message == "field not available on node: fov",
         "set non-camera fov reports clear error");

  const CommandResult badCam = fixture.bus.dispatch("cam spin");
  EXPECT(!badCam.ok, "cam fails for unknown action");
  EXPECT(badCam.message == "unknown cam action: spin",
         "cam unknown action returns stable error");

  const CommandResult badPreview = fixture.bus.dispatch("preview maybe");
  EXPECT(!badPreview.ok, "preview fails for unknown action");
  EXPECT(badPreview.message == "unknown preview action: maybe",
         "preview unknown action returns stable error");
}

void testSceneCommandsRequireRegisteredSceneIoCallbacks() {
  CommandFixture fixture;

  const CommandResult listResult = fixture.bus.dispatch("scene list");
  EXPECT(!listResult.ok, "scene list should fail before scene io is wired");
  EXPECT(listResult.message.find("unknown command: scene") == std::string::npos,
         "scene list should fail through a scene command handler");

  const CommandResult loadResult = fixture.bus.dispatch("scene load assets/example_scene.yaml");
  EXPECT(!loadResult.ok, "scene load should fail before scene io is wired");
  EXPECT(loadResult.message.find("unknown command: scene") == std::string::npos,
         "scene load should fail through a scene command handler, not unknown-command dispatch");

  const CommandResult saveResult = fixture.bus.dispatch("scene save");
  EXPECT(!saveResult.ok, "scene save should fail before scene io is wired");
  EXPECT(saveResult.message.find("unknown command: scene") == std::string::npos,
         "scene save should fail through a scene command handler, not unknown-command dispatch");

  const CommandResult saveAsResult =
      fixture.bus.dispatch("scene save assets/example_scene.yaml");
  EXPECT(!saveAsResult.ok, "scene save <path> should fail before scene io is wired");
  EXPECT(saveAsResult.message.find("unknown command: scene") == std::string::npos,
         "scene save <path> should fail through a scene command handler");
}

void testSceneCommandsUseRegisteredSceneIoCallbacks() {
  SceneSharedPtr scene = Scene::create(nullptr);
  EditorState editorState;
  CommandBus bus;

  std::vector<std::string> loadPaths;
  std::vector<std::string> savePaths;
  SceneIoContext sceneIo{
      .load = [&](const std::string &path) {
        loadPaths.push_back(path);
        return CommandResult{true, "loaded " + path, "{\"path\":\"" + path + "\"}"};
      },
      .save = [&](const std::optional<std::string> &path) {
        savePaths.push_back(path.value_or(std::string{}));
        if (path.has_value()) {
          return CommandResult{true, "saved " + *path,
                               "{\"path\":\"" + *path + "\"}"};
        }
        return CommandResult{true, "saved current scene", "{\"path\":null}"};
      },
      .list = [&]() {
        return CommandResult{true, "listed scenes",
                             "{\"entries\":[{\"id\":\"sample.scene.yaml\",\"kind\":\"asset\"}]}"};
      }};
  registerBuiltinCommands(bus, editorState, *scene, sceneIo);

  const CommandResult listResult = bus.dispatch("scene list");
  EXPECT(listResult.ok, "scene list should call registered callback");
  EXPECT(listResult.structured.find("\"kind\":\"asset\"") != std::string::npos,
         "scene list should surface structured catalog payload");

  const CommandResult loadResult = bus.dispatch("scene load assets/one.yaml");
  EXPECT(loadResult.ok, "scene load should call registered callback");
  EXPECT(loadPaths.size() == 1 && loadPaths[0] == "assets/one.yaml",
         "scene load passes the requested path to callback");

  const CommandResult saveResult = bus.dispatch("scene save");
  EXPECT(saveResult.ok, "scene save should call registered callback");
  EXPECT(savePaths.size() == 1 && savePaths[0].empty(),
         "scene save without path should pass nullopt semantics to callback");

  const CommandResult saveAsResult = bus.dispatch("scene save assets/two.yaml");
  EXPECT(saveAsResult.ok, "scene save <path> should call registered callback");
  EXPECT(savePaths.size() == 2 && savePaths[1] == "assets/two.yaml",
         "scene save <path> passes explicit path to callback");
}

void testAdminCommandsRequireRegisteredCallbacks() {
  CommandFixture fixture;

  const CommandResult onResult = fixture.bus.dispatch("admin on");
  EXPECT(!onResult.ok, "admin on should fail before callback wiring");
  EXPECT(onResult.message.find("unknown command: admin") == std::string::npos,
         "admin on should fail through admin handler");

  const CommandResult statusResult = fixture.bus.dispatch("admin status");
  EXPECT(!statusResult.ok, "admin status should fail before callback wiring");
  EXPECT(statusResult.message.find("unknown command: admin") == std::string::npos,
         "admin status should fail through admin handler");
}

void testAdminCommandsUseRegisteredCallbacks() {
  SceneSharedPtr scene = Scene::create(nullptr);
  EditorState editorState;
  CommandBus bus;
  bool adminEnabled = false;

  SceneIoContext sceneIo{
      .setAdmin = [&](const bool enabled) {
        adminEnabled = enabled;
        return CommandResult{true, enabled ? "admin enabled" : "admin disabled",
                             enabled ? "{\"permission\":\"admin\"}"
                                     : "{\"permission\":\"user\"}"};
      },
      .adminStatus = [&]() {
        return CommandResult{true, adminEnabled ? "admin" : "user",
                             adminEnabled ? "{\"permission\":\"admin\"}"
                                          : "{\"permission\":\"user\"}"};
      }};
  registerBuiltinCommands(bus, editorState, *scene, sceneIo);

  const CommandResult onResult = bus.dispatch("admin on");
  EXPECT(onResult.ok && adminEnabled, "admin on should call registered callback");

  const CommandResult statusResult = bus.dispatch("admin status");
  EXPECT(statusResult.ok, "admin status should succeed through callback");
  EXPECT(statusResult.structured.find("\"permission\":\"admin\"") !=
             std::string::npos,
         "admin status should expose current permission");

  const CommandResult offResult = bus.dispatch("admin off");
  EXPECT(offResult.ok && !adminEnabled,
         "admin off should disable admin through callback");
}

void testSceneLoadClearsRedoHistory() {
  CommandFixture undoFixture;

  SceneIoContext sceneIo{
      .load = [](const std::string &path) {
        return CommandResult{true, "loaded " + path, "{\"path\":\"" + path + "\"}"};
      }};
  registerBuiltinCommands(undoFixture.bus, undoFixture.editorState,
                          *undoFixture.scene, sceneIo);

  const CommandResult moveResult =
      undoFixture.bus.dispatch("move /world/cube 1 0 0");
  EXPECT(moveResult.ok, "setup move should succeed");
  EXPECT(undoFixture.bus.canUndo(),
         "mutating command should leave undo history before scene load");

  const CommandResult loadUndoResult =
      undoFixture.bus.dispatch("scene load assets/reloaded.yaml");
  EXPECT(loadUndoResult.ok, "scene load should succeed through callback");
  EXPECT(!undoFixture.bus.canUndo(),
         "successful scene load should invalidate stale undo history");

  CommandFixture redoFixture;
  registerBuiltinCommands(redoFixture.bus, redoFixture.editorState,
                          *redoFixture.scene, sceneIo);

  const CommandResult redoMoveResult =
      redoFixture.bus.dispatch("move /world/cube 1 0 0");
  EXPECT(redoMoveResult.ok, "redo setup move should succeed");

  const CommandResult undoResult = redoFixture.bus.undo();
  EXPECT(undoResult.ok, "undo should succeed before scene load");
  EXPECT(redoFixture.bus.canRedo(), "undoable command should leave redo history");

  const CommandResult loadRedoResult =
      redoFixture.bus.dispatch("scene load assets/reloaded.yaml");
  EXPECT(loadRedoResult.ok, "scene load should succeed with redo history present");
  EXPECT(!redoFixture.bus.canRedo(),
         "successful scene load should clear stale redo history");
}

void testSceneSavePreservesRedoHistory() {
  CommandFixture undoFixture;

  SceneIoContext sceneIo{
      .save = [](const std::optional<std::string> &path) {
        if (path.has_value()) {
          return CommandResult{true, "saved " + *path,
                               "{\"path\":\"" + *path + "\"}"};
        }
        return CommandResult{true, "saved current scene", "{\"path\":null}"};
      }};
  registerBuiltinCommands(undoFixture.bus, undoFixture.editorState,
                          *undoFixture.scene, sceneIo);

  const CommandResult moveResult =
      undoFixture.bus.dispatch("move /world/cube 1 0 0");
  EXPECT(moveResult.ok, "setup move should succeed");
  EXPECT(undoFixture.bus.canUndo(),
         "mutating command should leave undo history before scene save");

  const CommandResult saveUndoResult =
      undoFixture.bus.dispatch("scene save assets/snapshot.yaml");
  EXPECT(saveUndoResult.ok, "scene save should succeed through callback");
  EXPECT(undoFixture.bus.canUndo(),
         "successful scene save should preserve undo history");

  CommandFixture redoFixture;
  registerBuiltinCommands(redoFixture.bus, redoFixture.editorState,
                          *redoFixture.scene, sceneIo);

  const CommandResult redoMoveResult =
      redoFixture.bus.dispatch("move /world/cube 1 0 0");
  EXPECT(redoMoveResult.ok, "redo setup move should succeed");

  const CommandResult undoResult = redoFixture.bus.undo();
  EXPECT(undoResult.ok, "undo should succeed before scene save");
  EXPECT(redoFixture.bus.canRedo(),
         "undoable command should leave redo history");

  const CommandResult saveRedoResult =
      redoFixture.bus.dispatch("scene save assets/snapshot.yaml");
  EXPECT(saveRedoResult.ok, "scene save should succeed with redo history present");
  EXPECT(redoFixture.bus.canRedo(),
         "successful scene save should preserve redo history");
}

void testCameraControlStatusPreservesRedoHistory() {
  SceneViewerCommandFixture fixture;

  const CommandResult moveResult =
      fixture.base.bus.dispatch("move /world/cube 1 0 0");
  EXPECT(moveResult.ok, "redo setup move should succeed");

  const CommandResult undoResult = fixture.base.bus.undo();
  EXPECT(undoResult.ok, "undo should succeed before camera status");
  EXPECT(fixture.base.bus.canRedo(),
         "undoable command should leave redo history before camera status");

  const CommandResult statusResult =
      fixture.base.bus.dispatch("cam control status");
  EXPECT(statusResult.ok, "cam control status should succeed");
  EXPECT(statusResult.structured.find("\"camera\":\"orbit\"") !=
             std::string::npos,
         "camera status should report current camera control mode");
  EXPECT(fixture.base.bus.canRedo(),
         "read-only camera status should preserve redo history");
}

void testSceneViewerModeAndStateCommands() {
  SceneViewerCommandFixture fixture;

  const CommandResult rejectedMode = fixture.base.bus.dispatch("mode freefly");
  EXPECT(!rejectedMode.ok,
         "mode freefly should be rejected because camera is separate");
  EXPECT(rejectedMode.message.find("cam control freefly") != std::string::npos,
         "mode freefly error should point at cam control freefly");

  const CommandResult modeResult = fixture.base.bus.dispatch("mode selection");
  EXPECT(modeResult.ok, "mode selection should succeed");
  EXPECT(modeResult.structured.find("\"mode\":\"selection\"") !=
             std::string::npos,
         "mode command should return selection mode payload");

  const CommandResult cameraResult =
      fixture.base.bus.dispatch("cam control freefly");
  EXPECT(cameraResult.ok, "cam control freefly should succeed");
  EXPECT(cameraResult.structured.find("\"camera\":\"freefly\"") !=
             std::string::npos,
         "camera command should return camera payload");

  const CommandResult selectResult = fixture.base.bus.dispatch("select /world/cube");
  EXPECT(selectResult.ok, "select setup for state summary should succeed");
  const CommandResult summaryResult = fixture.base.bus.dispatch("state summary");
  EXPECT(summaryResult.ok, "state summary should succeed");
  EXPECT(summaryResult.structured.find("\"sceneName\"") != std::string::npos,
         "state summary should include scene name");
  EXPECT(summaryResult.structured.find("\"selectionCount\":1") !=
             std::string::npos,
         "state summary should include selection count");
  EXPECT(summaryResult.structured.find("\"sourceKind\":\"local\"") !=
             std::string::npos,
         "state summary should include source kind");
  EXPECT(summaryResult.structured.find("\"permission\":\"user\"") !=
             std::string::npos,
         "state summary should include permission");

  const CommandResult selectionResult =
      fixture.base.bus.dispatch("state selection");
  EXPECT(selectionResult.ok, "state selection should succeed");
  EXPECT(selectionResult.structured.find("/world/cube") != std::string::npos,
         "state selection should include selected path");

  const CommandResult camerasResult = fixture.base.bus.dispatch("state cameras");
  EXPECT(camerasResult.ok, "state cameras should succeed");
  EXPECT(camerasResult.structured.find("\"editor\"") != std::string::npos,
         "state cameras should include editor camera payload");

  const CommandResult sceneResult = fixture.base.bus.dispatch("state scene");
  EXPECT(sceneResult.ok, "state scene should succeed");
  EXPECT(sceneResult.structured.find("\"nodeCount\"") != std::string::npos,
         "state scene should include scene counts");
  EXPECT(sceneResult.structured.find("\"permission\":\"user\"") !=
             std::string::npos,
         "state scene should include permission");

  const CommandResult toolbarResult = fixture.base.bus.dispatch("state toolbar");
  EXPECT(toolbarResult.ok, "state toolbar should succeed");
  EXPECT(toolbarResult.structured.find("\"mode\":\"selection\"") !=
             std::string::npos,
         "state toolbar should reflect current editor mode");
  EXPECT(toolbarResult.structured.find("\"camera\":\"freefly\"") !=
             std::string::npos,
         "state toolbar should reflect current camera control mode");
  EXPECT(toolbarResult.structured.find("\"debugEnabled\":false") !=
             std::string::npos,
         "state toolbar should include debug toggle state");

  const CommandResult modeStatus = fixture.base.bus.dispatch("mode status");
  EXPECT(modeStatus.ok, "mode status should succeed");
  EXPECT(modeStatus.structured.find("\"mode\":\"selection\"") !=
             std::string::npos,
         "mode status should report current editor mode");

  const CommandResult cameraStatus =
      fixture.base.bus.dispatch("cam control status");
  EXPECT(cameraStatus.ok, "cam control status should succeed");
  EXPECT(cameraStatus.structured.find("\"camera\":\"freefly\"") !=
             std::string::npos,
         "camera status should report current camera control mode");
}

void testSceneViewerDebugCommandsUpdateSummaryAndToolbarState() {
  SceneViewerCommandFixture fixture;

  const CommandResult initialStatus = fixture.base.bus.dispatch("debug status");
  EXPECT(initialStatus.ok, "debug status should succeed");
  EXPECT(initialStatus.structured.find("\"debugEnabled\":false") !=
             std::string::npos,
         "debug status should report disabled by default");

  const CommandResult enable = fixture.base.bus.dispatch("debug on");
  EXPECT(enable.ok, "debug on should succeed");
  EXPECT(enable.structured.find("\"debugEnabled\":true") != std::string::npos,
         "debug on should report enabled state");

  const CommandResult summaryResult = fixture.base.bus.dispatch("state summary");
  EXPECT(summaryResult.ok, "state summary should still succeed after debug on");
  EXPECT(summaryResult.structured.find("\"debugEnabled\":true") !=
             std::string::npos,
         "state summary should surface debug state");

  const CommandResult toolbarResult = fixture.base.bus.dispatch("state toolbar");
  EXPECT(toolbarResult.ok, "state toolbar should succeed after debug on");
  EXPECT(toolbarResult.structured.find("\"debugEnabled\":true") !=
             std::string::npos,
         "state toolbar should surface debug state");

  const CommandResult disable = fixture.base.bus.dispatch("debug off");
  EXPECT(disable.ok, "debug off should succeed");
  EXPECT(disable.structured.find("\"debugEnabled\":false") !=
             std::string::npos,
         "debug off should report disabled state");
}

void testSceneViewerPickCommandUsesInteractionController() {
  SceneViewerPickFixture fixture;
  const CommandResult result = fixture.bus.dispatch("pick 400 300");
  EXPECT(result.ok, "pick command should succeed for centered cube");
  EXPECT(!fixture.editorState.getSelected().empty(),
         "pick command should update selection through interaction controller");
  EXPECT(result.structured.find("\"lastHitPoint\"") != std::string::npos,
         "pick command should surface hit point through selection payload");

  fixture.editorState.setPreviewEnabled(true);
  const CommandResult previewBlocked = fixture.bus.dispatch("pick 400 300");
  EXPECT(!previewBlocked.ok, "pick should reject preview mode");
}

void testSceneViewerPickScreenCommandUsesExplicitViewportSize() {
  SceneViewerPickFixture fixture;
  fixture.rect = LX_demo::lxe_editor::SceneViewRect{
      .x = 123.0f,
      .y = 45.0f,
      .width = 321.0f,
      .height = 210.0f,
  };

  const CommandResult result =
      fixture.bus.dispatch("pick screen 400 300 800 600");
  EXPECT(result.ok, "pick screen should succeed with explicit viewport size");
  EXPECT(!fixture.editorState.getSelected().empty(),
         "pick screen should update selection through interaction controller");
  EXPECT(result.structured.find("\"lastHitPoint\"") != std::string::npos,
         "pick screen should surface hit point through selection payload");
}

void testSceneViewerPickDebugLogsArePrintedToConsole() {
  SceneViewerPickFixture fixture;
  const CommandResult enable = fixture.bus.dispatch("debug on");
  EXPECT(enable.ok, "debug on should succeed before pick logging");

  const CommandResult result = fixture.bus.dispatch("pick 400 300");
  EXPECT(result.ok, "pick command should succeed while debug logging is enabled");

  const std::string consoleText = fixture.consolePanel.displayedText();
  EXPECT(consoleText.find("pick_debug") != std::string::npos,
         "console should include pick debug log header");
  EXPECT(consoleText.find("\"screenPixel\"") != std::string::npos,
         "console should include click pixel coordinates");
  EXPECT(consoleText.find("\"screenNdc\"") != std::string::npos,
         "console should include click ndc coordinates");
  EXPECT(consoleText.find("\"hitWorld\"") != std::string::npos,
         "console should include world-space hit point");
  EXPECT(consoleText.find("\"hitNdc\"") != std::string::npos,
         "console should include hit ndc coordinates");
  EXPECT(consoleText.find("\"projectedPixel\"") != std::string::npos,
         "console should include hit reprojection pixel coordinates");
}

void testConsolePanelFormatsCommandAndResultWithNewPrompts() {
  CommandFixture fixture;
  ConsolePanel panel(fixture.bus);

  panel.submitLine("select /world/cube");
  const std::string consoleText = panel.displayedText();
  const std::string resultMessage = fixture.bus.history().back().result.message;

  EXPECT(fixture.editorState.getSelected().size() == 1 && fixture.editorState.getSelected()[0] == fixture.cube,
         "panel submit routes through builtin command bus");
  EXPECT(panel.displayedEntries().size() == 1,
         "panel display shows newly executed command");
  EXPECT(consoleText.find("> select /world/cube") != std::string::npos,
         "displayedText should render commands with the new prompt prefix");
  EXPECT(consoleText.find('\n' + resultMessage) !=
             std::string::npos,
         "displayedText should render result text without a prompt prefix");
  EXPECT(consoleText.find("< select /world/cube") == std::string::npos,
         "legacy command prefix should be removed");
  EXPECT(consoleText.find("\n> " + resultMessage) == std::string::npos,
         "legacy result prefix should be removed");
  EXPECT(fixture.bus.history().size() == 1,
         "panel submit contributes to command history");
}

void testSceneViewerPickDebugLogsAttachToNewestCommandEntry() {
  SceneViewerPickFixture fixture;
  const CommandResult enable = fixture.bus.dispatch("debug on");
  EXPECT(enable.ok, "debug on should succeed before pick logging");

  const CommandResult pick = fixture.bus.dispatch("pick 400 300");
  EXPECT(pick.ok, "pick command should succeed while debug logging is enabled");
  const CommandResult listNodes = fixture.bus.dispatch("list nodes");
  EXPECT(listNodes.ok, "later visible command should succeed after pick logging");

  const std::string consoleText = fixture.consolePanel.displayedText();
  const usize pickCommandPos = consoleText.find("> pick 400 300");
  const usize pickResultPos = consoleText.find(pick.message, pickCommandPos);
  const usize debugPos = consoleText.find("pick_debug", pickResultPos);
  const usize laterCommandPos = consoleText.find("> list nodes", debugPos);
  const usize laterResultPos = consoleText.find(listNodes.message, laterCommandPos);

  EXPECT(pickCommandPos != std::string::npos,
         "pick command should remain visible in console output");
  EXPECT(pickResultPos != std::string::npos,
         "pick result should remain visible in console output");
  EXPECT(debugPos != std::string::npos,
         "pick debug output should remain visible in console output");
  EXPECT(laterCommandPos != std::string::npos,
         "later command should remain visible in console output");
  EXPECT(laterResultPos != std::string::npos,
         "later command result should remain visible in console output");
  EXPECT(pickCommandPos < pickResultPos && pickResultPos < debugPos,
         "pick debug output should appear after the owning command result");
  EXPECT(debugPos < laterCommandPos && laterCommandPos < laterResultPos,
         "pick debug output should stay attached before the later command block");
}

void testConsoleClearDropsOldDebugAttachmentsFromVisibleOutput() {
  SceneViewerPickFixture fixture;
  EXPECT(fixture.bus.dispatch("debug on").ok,
         "debug on should succeed before clear behavior test");
  EXPECT(fixture.bus.dispatch("pick 400 300").ok,
         "pick should succeed before clear behavior test");
  const usize historySizeBeforeClear = fixture.bus.history().size();

  fixture.consolePanel.clearDisplay();
  const std::string afterClear = fixture.consolePanel.displayedText();
  EXPECT(afterClear.find("pick_debug") == std::string::npos,
         "clearDisplay should hide prior attached debug lines");
  EXPECT(afterClear.find("> pick 400 300") == std::string::npos,
         "clearDisplay should hide prior visible command entries");
  EXPECT(fixture.consolePanel.displayedEntries().empty(),
         "clearDisplay hides old output without mutating bus history");
  EXPECT(fixture.bus.history().size() == historySizeBeforeClear,
         "clearDisplay leaves command bus history intact");

  EXPECT(fixture.bus.dispatch("pick 400 300").ok,
         "pick should still work after clear");
  const std::string afterNewPick = fixture.consolePanel.displayedText();
  EXPECT(!fixture.consolePanel.displayedEntries().empty(),
         "new entries appear after clearDisplay checkpoint");
  EXPECT(afterNewPick.find("> pick 400 300") != std::string::npos,
         "new command should be visible after clear");
  EXPECT(afterNewPick.find(fixture.bus.history().back().result.message) !=
             std::string::npos,
         "new command result should be visible after clear");
  EXPECT(afterNewPick.find("pick_debug") != std::string::npos,
         "newly attached debug output should be visible after clear");
  const usize commandPos = afterNewPick.find("> pick 400 300");
  const usize resultPos =
      afterNewPick.find(fixture.bus.history().back().result.message, commandPos);
  const usize debugPos = afterNewPick.find("pick_debug", commandPos);
  EXPECT(commandPos < resultPos && resultPos < debugPos,
         "newly attached debug output should follow the new visible command result");
  EXPECT(fixture.bus.history().size() > historySizeBeforeClear,
         "history keeps full record after clearDisplay");
}

void testConsoleSystemLinesStayOrphanedWithoutVisibleOwner() {
  CommandFixture fixture;
  ConsolePanel panel(fixture.bus);

  panel.clearDisplay();
  panel.appendSystemLine("orphan line");

  const std::string orphanOnlyText = panel.displayedText();
  EXPECT(orphanOnlyText.find("orphan line") != std::string::npos,
         "system line should be visible immediately without a visible command owner");
  EXPECT(panel.displayedEntries().empty(),
         "orphan system line should not create a synthetic history entry");

}

void testConsoleSystemLinesDoNotRetroactivelyAttachAfterClear() {
  CommandFixture fixture;
  ConsolePanel panel(fixture.bus);

  panel.clearDisplay();
  panel.appendSystemLine("orphan line");
  panel.submitLine("help");

  const std::string afterCommandText = panel.displayedText();
  const std::string helpResult = fixture.bus.history().back().result.message;
  const usize helpCommandPos = afterCommandText.find("> help");
  const usize helpResultPos = afterCommandText.find(helpResult, helpCommandPos);
  const usize orphanPos = afterCommandText.find("orphan line");

  EXPECT(helpCommandPos != std::string::npos,
         "new command should still render after orphan system output");
  EXPECT(helpResultPos != std::string::npos,
         "new command result should still render after orphan system output");
  EXPECT(orphanPos != std::string::npos,
         "orphan system line should remain visible after a later command");
  EXPECT(afterCommandText.find(helpResult + "\norphan line") == std::string::npos,
         "orphan system line should not migrate into the later command block");
}

void testConsoleLateSystemLinesAttachToNewestVisibleEntry() {
  CommandFixture fixture;
  ConsolePanel panel(fixture.bus);

  panel.submitLine("help");
  panel.appendSystemLine("late line");

  const std::string consoleText = panel.displayedText();
  const std::string helpResult = fixture.bus.history().back().result.message;
  const usize helpCommandPos = consoleText.find("> help");
  const usize helpResultPos = consoleText.find(helpResult, helpCommandPos);
  const usize lateLinePos = consoleText.find("late line", helpResultPos);

  EXPECT(helpCommandPos != std::string::npos,
         "late-line test should keep the owning command visible");
  EXPECT(helpResultPos != std::string::npos,
         "late-line test should keep the owning result visible");
  EXPECT(lateLinePos != std::string::npos,
         "late system line should remain visible");
  EXPECT(helpCommandPos < helpResultPos && helpResultPos < lateLinePos,
         "late system line should render under the newest visible command result");
  EXPECT(consoleText.find(helpResult + "\n\nlate line") == std::string::npos,
         "late system line should not render as a separate orphan block");
}

void testConsoleInDispatchSystemLinesDoNotDependOnResultMatchingOrReadTiming() {
  CommandBus bus;
  ConsolePanel panel(bus);

  bus.registerHandler(
      "emit", "emit",
      [&](std::vector<std::string>) {
        panel.appendSystemLine("owned line");
        return CommandResult{true, "same result", {}, {}};
      });
  bus.registerHandler(
      "same", "same",
      [](std::vector<std::string>) {
        return CommandResult{true, "same result", {}, {}};
      });

  panel.submitLine("emit");
  EXPECT(bus.history().size() == 1,
         "first command should be recorded before the collision command runs");
  EXPECT(bus.dispatch("same").ok,
         "second command with colliding result text should still succeed");

  const std::string consoleText = panel.displayedText();
  const usize emitCommandPos = consoleText.find("> emit");
  const usize emitResultPos = consoleText.find("same result", emitCommandPos);
  const usize ownedLinePos = consoleText.find("owned line", emitResultPos);
  const usize sameCommandPos = consoleText.find("> same", ownedLinePos);
  const usize sameResultPos = consoleText.find("same result", sameCommandPos);

  EXPECT(emitCommandPos != std::string::npos,
         "first command should remain visible in collision regression");
  EXPECT(emitResultPos != std::string::npos,
         "first result should remain visible in collision regression");
  EXPECT(ownedLinePos != std::string::npos,
         "in-dispatch system line should remain visible in collision regression");
  EXPECT(sameCommandPos != std::string::npos,
         "second command should remain visible in collision regression");
  EXPECT(sameResultPos != std::string::npos,
         "second result should remain visible in collision regression");
  EXPECT(emitCommandPos < emitResultPos && emitResultPos < ownedLinePos,
         "in-dispatch system line should stay under the owning command result");
  EXPECT(ownedLinePos < sameCommandPos && sameCommandPos < sameResultPos,
         "in-dispatch system line should not migrate to a later colliding result");
}

void testHistorylessUndoConsoleLinesStayVisible() {
  CommandBus bus;
  ConsolePanel panel(bus);
  bool enabled = false;

  bus.registerHandler(
      "toggle", CommandMetadata{
                    .brief = "toggle",
                    .inverse =
                        [](const ParsedCommand &, const CommandResult &) {
                          return std::optional<std::string>{"toggle_inverse"};
                        },
                    .mutatesState = true},
      [&](std::vector<std::string>) {
        enabled = !enabled;
        return CommandResult{true, enabled ? "enabled" : "disabled", {}, {}};
      });
  bus.registerHandler(
      "toggle_inverse", "toggle_inverse",
      [&](std::vector<std::string>) {
        panel.appendSystemLine("undo line");
        enabled = !enabled;
        return CommandResult{true, enabled ? "enabled" : "disabled", {}, {}};
      });

  panel.submitLine("toggle");
  EXPECT(enabled, "setup command should mutate state before undo");
  const usize historySizeBeforeUndo = bus.history().size();

  const CommandResult undoResult = bus.undo();
  EXPECT(undoResult.ok, "historyless undo should succeed");
  EXPECT(!enabled, "historyless undo should restore prior state");
  EXPECT(bus.history().size() == historySizeBeforeUndo,
         "historyless undo should not record a new top-level history entry");

  const std::string consoleText = panel.displayedText();
  const usize toggleCommandPos = consoleText.find("> toggle");
  const usize toggleResultPos = consoleText.find("enabled", toggleCommandPos);
  const usize undoLinePos = consoleText.find("undo line", toggleResultPos);

  EXPECT(toggleCommandPos != std::string::npos,
         "owning visible command should remain visible after historyless undo");
  EXPECT(toggleResultPos != std::string::npos,
         "owning visible result should remain visible after historyless undo");
  EXPECT(undoLinePos != std::string::npos,
         "historyless undo system line should remain visible");
  EXPECT(toggleCommandPos < toggleResultPos && toggleResultPos < undoLinePos,
         "historyless undo system line should attach through the visible late/orphan path");
}

void testConsolePanelBrowseAndAutocomplete() {
  CommandFixture fixture;
  ConsolePanel panel(fixture.bus);

  panel.submitLine("help");
  panel.submitLine("list nodes");

  panel.browseHistoryOlder();
  EXPECT(panel.getInputText() == "list nodes",
         "first older browse recalls latest command");
  panel.browseHistoryOlder();
  EXPECT(panel.getInputText() == "help",
         "second older browse recalls previous command");
  panel.browseHistoryNewer();
  EXPECT(panel.getInputText() == "list nodes",
         "newer browse walks forward through history");
  panel.browseHistoryNewer();
  EXPECT(panel.getInputText().empty(),
         "newer browse past latest clears draft input");

  panel.setInputText("sel");
  panel.autocompleteInput();
  EXPECT(panel.getInputText() == "select ",
         "autocomplete completes unique builtin verb and appends space");

  panel.setInputText("set /world/c");
  panel.autocompleteInput();
  EXPECT(panel.getInputText() == "set /world/cube ",
         "autocomplete completes set target path through bus completer");

  panel.setInputText("move ");
  panel.autocompleteInput();
  EXPECT(panel.getInputText() == "move /",
         "autocomplete keeps trailing space so first-arg completion can start");

  panel.setInputText("set /world/cube.t");
  panel.autocompleteInput();
  EXPECT(panel.getInputText() == "set /world/cube.translation ",
         "autocomplete completes set field suffix through bus completer");
}

void testConsoleInputControllerHistoryKeepsDraft() {
  CommandFixture fixture;
  ConsoleInputController controller(fixture.bus);

  controller.submitLine("help");
  controller.submitLine("list nodes");
  controller.setInputText("move /world/cube");

  controller.browseHistoryOlder();
  EXPECT(controller.inputText() == "list nodes",
         "first older browse should recall latest history entry");
  controller.browseHistoryNewer();
  EXPECT(controller.inputText() == "move /world/cube",
         "moving past newest history entry should restore draft input");
}

void testConsoleInputControllerCompletionBehaviors() {
  CommandFixture fixture;
  ConsoleInputController controller(fixture.bus);

  controller.setInputText("sel");
  controller.autocomplete();
  EXPECT(controller.inputText() == "select ",
         "unique verb completion should append trailing space");

  controller.setInputText("set /world/c");
  controller.autocomplete();
  EXPECT(controller.inputText() == "set /world/cube ",
         "unique argument completion should append trailing space");

  controller.setInputText("r");
  controller.autocomplete();
  EXPECT(controller.inputText() == "r",
         "ambiguous completion with unchanged prefix should keep original input");
  const std::string helper = controller.helperOutputText();
  EXPECT(helper.find("remove") != std::string::npos,
         "ambiguous completion should list remove candidate");
  EXPECT(helper.find("redo") != std::string::npos,
         "ambiguous completion should list redo candidate");
}

void testConsoleInputControllerEscRestoresDraft() {
  CommandFixture fixture;
  ConsoleInputController controller(fixture.bus);

  controller.submitLine("help");
  controller.submitLine("list nodes");
  controller.setInputText("set /world/cube.translation");
  controller.browseHistoryOlder();
  controller.browseHistoryOlder();
  controller.cancelHistoryBrowse();

  EXPECT(controller.inputText() == "set /world/cube.translation",
         "canceling history browse should restore original draft");
}

void testConsoleInputControllerCallbackEvents() {
  CommandFixture fixture;
  ConsoleInputController controller(fixture.bus);

  controller.submitLine("help");
  controller.submitLine("list nodes");

  controller.setInputText("sel");
  (void)controller.handleCallbackEvent(ImGuiInputTextFlags_CallbackCompletion,
                                       ImGuiKey_Tab, 0);
  EXPECT(controller.inputText() == "select ",
         "completion callback should route to autocomplete");

  (void)controller.handleCallbackEvent(ImGuiInputTextFlags_CallbackHistory,
                                       ImGuiKey_UpArrow, 0);
  EXPECT(controller.inputText() == "list nodes",
         "history callback should route to older history");

  (void)controller.handleCallbackEvent(ImGuiInputTextFlags_CallbackHistory,
                                       ImGuiKey_DownArrow, 0);
  EXPECT(controller.inputText() == "select ",
         "history callback should restore draft after returning past newest");

  EXPECT(controller.handleCallbackEvent(ImGuiInputTextFlags_CallbackCharFilter, 0,
                                        '\n') == 1,
         "char filter should reject newline insertion");
  EXPECT(controller.handleCallbackEvent(ImGuiInputTextFlags_CallbackCharFilter, 0,
                                        '\r') == 1,
         "char filter should reject carriage return insertion");
  EXPECT(controller.handleCallbackEvent(ImGuiInputTextFlags_CallbackCharFilter, 0,
                                        'a') == 0,
         "char filter should allow ordinary characters");
}

void testConsolePanelCharFilterDoesNotRewriteCallbackBuffer() {
  CommandFixture fixture;
  ConsoleInputController controller(fixture.bus);
  controller.setInputText("draft");

  std::array<char, 64> filterBuffer{};
  std::memcpy(filterBuffer.data(), "keep", 5);
  ImGuiInputTextCallbackData filterData{};
  filterData.EventFlag = ImGuiInputTextFlags_CallbackCharFilter;
  filterData.EventChar = 'a';
  filterData.UserData = &controller;
  filterData.Buf = filterBuffer.data();
  filterData.BufSize = static_cast<int>(filterBuffer.size());

  EXPECT(ConsolePanel::inputTextCallback(&filterData) == 0,
         "char filter callback should allow ordinary characters");
  EXPECT(std::string(filterData.Buf) == "keep",
         "char filter callback should not rewrite ImGui text buffer");
  EXPECT(!filterData.BufDirty,
         "char filter callback should not mark callback buffer dirty");
  EXPECT(controller.inputText() == "draft",
         "char filter callback should not mutate controller-owned draft text");
}

void testConsolePanelShouldSubmitPlainEnterOnly() {
  EXPECT(ConsolePanel::shouldSubmitInputOnPlainEnter(
             true, true, false, false, false, false, false) == true,
         "plain enter on active multiline input should submit");
  EXPECT(ConsolePanel::shouldSubmitInputOnPlainEnter(
             true, false, true, false, false, false, false) == true,
         "plain keypad enter on active multiline input should submit");
  EXPECT(ConsolePanel::shouldSubmitInputOnPlainEnter(
             false, true, false, false, false, false, false) == false,
         "inactive multiline input should not submit on enter");
  EXPECT(ConsolePanel::shouldSubmitInputOnPlainEnter(
             true, true, false, true, false, false, false) == false,
         "ctrl-enter should not submit");
  EXPECT(ConsolePanel::shouldSubmitInputOnPlainEnter(
             true, true, false, false, true, false, false) == false,
         "shift-enter should not submit");
  EXPECT(ConsolePanel::shouldSubmitInputOnPlainEnter(
             true, true, false, false, false, true, false) == false,
         "alt-enter should not submit");
  EXPECT(ConsolePanel::shouldSubmitInputOnPlainEnter(
             true, true, false, false, false, false, true) == false,
         "super-enter should not submit");
}

void testConsolePanelUsesSingleLineHistoryCompatibleInputFlags() {
  const ImGuiInputTextFlags flags = ConsolePanel::inputTextFlags();
  EXPECT((flags & ImGuiInputTextFlags_EnterReturnsTrue) != 0,
         "single-line console input should use EnterReturnsTrue for reliable submit");
  EXPECT((flags & ImGuiInputTextFlags_CallbackHistory) != 0,
         "console input should keep history callback support");
  EXPECT((flags & ImGuiInputTextFlags_CallbackCompletion) != 0,
         "console input should keep completion callback support");
  EXPECT((flags & ImGuiInputTextFlags_CallbackCharFilter) != 0,
         "console input should keep character filtering support");
  EXPECT(!ConsolePanel::usesMultilineInput(),
         "console input widget must remain single-line when history callbacks are enabled");
}

void testConsoleInputControllerSyncsCallbackBufferAfterCompletionAndHistory() {
  CommandFixture fixture;
  ConsoleInputController controller(fixture.bus);

  controller.submitLine("help");
  controller.submitLine("list nodes");
  controller.setInputText("sel");

  std::array<char, 64> completionBuffer{};
  ImGuiInputTextCallbackData completionData{};
  completionData.EventFlag = ImGuiInputTextFlags_CallbackCompletion;
  completionData.EventKey = ImGuiKey_Tab;
  completionData.Buf = completionBuffer.data();
  completionData.BufSize = static_cast<int>(completionBuffer.size());

  (void)controller.handleCallbackEvent(completionData.EventFlag,
                                       completionData.EventKey,
                                       completionData.EventChar);
  controller.syncCallbackBuffer(completionData);
  EXPECT(std::string(completionData.Buf) == "select ",
         "callback sync should copy completion text into ImGui buffer");
  EXPECT(completionData.BufTextLen == 7,
         "callback sync should update completion buffer length");
  EXPECT(completionData.CursorPos == completionData.BufTextLen,
         "callback sync should move completion cursor to buffer end");
  EXPECT(completionData.SelectionStart == completionData.BufTextLen &&
             completionData.SelectionEnd == completionData.BufTextLen,
         "callback sync should collapse completion selection at buffer end");
  EXPECT(completionData.BufDirty,
         "callback sync should mark completion buffer dirty");

  controller.setInputText("draft");
  std::array<char, 64> historyBuffer{};
  ImGuiInputTextCallbackData historyData{};
  historyData.EventFlag = ImGuiInputTextFlags_CallbackHistory;
  historyData.EventKey = ImGuiKey_UpArrow;
  historyData.Buf = historyBuffer.data();
  historyData.BufSize = static_cast<int>(historyBuffer.size());

  (void)controller.handleCallbackEvent(historyData.EventFlag, historyData.EventKey,
                                       historyData.EventChar);
  controller.syncCallbackBuffer(historyData);
  EXPECT(std::string(historyData.Buf) == "list nodes",
         "callback sync should copy history text into ImGui buffer");
  EXPECT(historyData.BufTextLen == 10,
         "callback sync should update history buffer length");
  EXPECT(historyData.CursorPos == historyData.BufTextLen,
         "callback sync should move history cursor to buffer end");
  EXPECT(historyData.SelectionStart == historyData.BufTextLen &&
             historyData.SelectionEnd == historyData.BufTextLen,
         "callback sync should collapse history selection at buffer end");
  EXPECT(historyData.BufDirty,
         "callback sync should mark history buffer dirty");
}

void testConsoleInputControllerPersistsHistoryLines() {
  CommandFixture fixture;
  ConsoleInputController controller(fixture.bus);

  controller.setPersistedHistory({"help", "list nodes"});
  controller.setInputText("move /world/cube");
  controller.browseHistoryOlder();
  EXPECT(controller.inputText() == "list nodes",
         "persisted history should seed history browsing after restart");
  controller.browseHistoryOlder();
  EXPECT(controller.inputText() == "help",
         "older browsing should continue across persisted entries");

  controller.submitLine("select /world/cube");
  const auto persisted = controller.persistedHistory();
  EXPECT(persisted.size() == 3,
         "persisted history export should include newly submitted commands");
  EXPECT(persisted.back() == "select /world/cube",
         "persisted history export should append the latest submitted command");
}

void testConsoleInputControllerSanitizesMultilineSubmitToSingleLine() {
  CommandFixture fixture;
  ConsoleInputController controller(fixture.bus);

  controller.submitLine("select\n/world/cube");

  EXPECT(fixture.bus.history().size() == 1,
         "multiline widget submit should still dispatch one logical command");
  EXPECT(fixture.bus.history().back().result.ok,
         "sanitized multiline submit should succeed");
  EXPECT(fixture.bus.history().back().line == "select /world/cube",
         "sanitized multiline submit should collapse embedded newlines to spaces");
  EXPECT(controller.persistedHistory().size() == 1,
         "persisted history should record sanitized multiline submit");
  EXPECT(controller.persistedHistory().back() == "select /world/cube",
         "persisted history should store the single-line sanitized command");
  EXPECT(fixture.editorState.getSelected().size() == 1 &&
             fixture.editorState.getSelected()[0] == fixture.cube,
         "sanitized multiline submit should preserve normal command behavior");
}

void testConsolePanelUndoRedoShortcutsUseCommandBus() {
  CommandFixture fixture;
  ConsolePanel panel(fixture.bus);

  panel.submitLine("move /world/cube 1 0 0");
  EXPECT(nearlyEqual(fixture.cube->getTranslation().x, 1.0f),
         "setup move should update translation");

  panel.dispatchUndo();
  EXPECT(nearlyEqual(fixture.cube->getTranslation().x, -0.5f),
         "dispatchUndo should route through command bus undo");
  EXPECT(!fixture.bus.history().empty() &&
             fixture.bus.history().back().line == "undo",
         "dispatchUndo should record undo command in history");

  panel.dispatchRedo();
  EXPECT(nearlyEqual(fixture.cube->getTranslation().x, 1.0f),
         "dispatchRedo should route through command bus redo");
  EXPECT(!fixture.bus.history().empty() &&
             fixture.bus.history().back().line == "redo",
         "dispatchRedo should record redo command in history");
}

} // namespace

int main() {
  testDispatchRecordsHistoryAndPreservesQuotedToken();
  testEscapesReachHandler();
  testUnknownVerbAndParseErrorsStayInBand();
  testHandlerExceptionBecomesFailedResult();
  testDispatchScriptSkipsCommentsAndContinuesAfterFailure();
  testNestedDispatchesShareTopLevelDispatchIdentity();
  testListVerbsAndBriefAreDeterministic();
  testEditorStateUsesWeakSelection();
  testBuiltinHelpSelectAndDeselect();
  testBuiltinTransformCommandsAndGet();
  testBuiltinListCommands();
  testBuiltinCommandErrors();
  testBuiltinAddRemoveSetCommands();
  testBuiltinCreatesAndEditsTypedLights();
  testBuiltinCamAndPreviewCommands();
  testBuiltinRemainingCommandErrors();
  testSceneCommandsRequireRegisteredSceneIoCallbacks();
  testSceneCommandsUseRegisteredSceneIoCallbacks();
  testAdminCommandsRequireRegisteredCallbacks();
  testAdminCommandsUseRegisteredCallbacks();
  testSceneLoadClearsRedoHistory();
  testSceneSavePreservesRedoHistory();
  testCameraControlStatusPreservesRedoHistory();
  testSceneViewerModeAndStateCommands();
  testSceneViewerDebugCommandsUpdateSummaryAndToolbarState();
  testSceneViewerPickCommandUsesInteractionController();
  testSceneViewerPickScreenCommandUsesExplicitViewportSize();
  testSceneViewerPickDebugLogsArePrintedToConsole();
  testConsolePanelFormatsCommandAndResultWithNewPrompts();
  testSceneViewerPickDebugLogsAttachToNewestCommandEntry();
  testConsoleClearDropsOldDebugAttachmentsFromVisibleOutput();
  testConsoleSystemLinesStayOrphanedWithoutVisibleOwner();
  testConsoleSystemLinesDoNotRetroactivelyAttachAfterClear();
  testConsoleLateSystemLinesAttachToNewestVisibleEntry();
  testConsoleInDispatchSystemLinesDoNotDependOnResultMatchingOrReadTiming();
  testHistorylessUndoConsoleLinesStayVisible();
  testConsolePanelBrowseAndAutocomplete();
  testConsolePanelUndoRedoShortcutsUseCommandBus();
  testConsoleInputControllerHistoryKeepsDraft();
  testConsoleInputControllerCompletionBehaviors();
  testConsoleInputControllerEscRestoresDraft();
  testConsoleInputControllerCallbackEvents();
  testConsolePanelCharFilterDoesNotRewriteCallbackBuffer();
  testConsolePanelShouldSubmitPlainEnterOnly();
  testConsolePanelUsesSingleLineHistoryCompatibleInputFlags();
  testConsoleInputControllerSyncsCallbackBufferAfterCompletionAndHistory();
  testConsoleInputControllerPersistsHistoryLines();
  testConsoleInputControllerSanitizesMultilineSubmitToSingleLine();

  if (failures != 0) {
    std::cerr << "test_command_bus failed with " << failures << " failure(s)\n";
    return 1;
  }

  std::cout << "test_command_bus passed\n";
  return 0;
}
