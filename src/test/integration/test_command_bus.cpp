#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/console_panel.hpp"
#include "core/editor/editor_state.hpp"
#include "core/math/quat.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <cmath>
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

struct CommandFixture {
  SceneSharedPtr scene = Scene::create(nullptr);
  EditorState editorState;
  CommandBus bus;
  SceneNodeSharedPtr world = SceneNode::create("node_world");
  SceneNodeSharedPtr cube = SceneNode::create("node_cube");
  SceneNodeSharedPtr cameraNode = SceneNode::create("node_camera");
  CameraComponent *camera = nullptr;

  CommandFixture() {
    world->setName("world");
    scene->addRenderable(world);

    cube->setName("cube");
    cube->setParent(world);
    scene->addRenderable(cube);

    cameraNode->setName("camera_main");
    const auto cameraComponent = cameraNode->addComponent<CameraComponent>();
    camera = &cameraComponent->get();
    camera->fovY = 60.0f;
    scene->addCamera(cameraNode);

    registerBuiltinCommands(bus, editorState, *scene);
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

  state.select(node);
  EXPECT(state.getSelected() == node, "selected node resolves while alive");

  node.reset();
  EXPECT(!state.getSelected(), "expired selected node clears naturally");

  state.deselect();
  EXPECT(!state.getSelected(), "deselect keeps state empty");

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
  EXPECT(fixture.editorState.getSelected() == fixture.cube,
         "select updates EditorState");

  const CommandResult deselect = fixture.bus.dispatch("deselect");
  EXPECT(deselect.ok, "deselect succeeds");
  EXPECT(!fixture.editorState.getSelected(), "deselect clears EditorState");
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

  const CommandResult selectResult = fixture.bus.dispatch("select /world/cube");
  EXPECT(selectResult.ok, "select before add commands succeeds");

  const CommandResult addMesh = fixture.bus.dispatch("add mesh probe");
  EXPECT(addMesh.ok, "add mesh succeeds");
  EXPECT(fixture.scene->findByPath("/world/cube/probe") != nullptr,
         "add mesh attaches new node under selected parent");

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

  const CommandResult setFov = fixture.bus.dispatch("set /camera_main.fov 75");
  EXPECT(setFov.ok, "set fov succeeds");
  EXPECT(nearlyEqual(fixture.camera->fovY, 75.0f),
         "set fov updates camera component");

  const CommandResult setTranslation =
      fixture.bus.dispatch("set /world/cube.translation 4 5 6");
  EXPECT(setTranslation.ok, "set translation succeeds");
  EXPECT(nearlyEqual(fixture.cube->getTranslation().x, 4.0f) &&
             nearlyEqual(fixture.cube->getTranslation().y, 5.0f) &&
             nearlyEqual(fixture.cube->getTranslation().z, 6.0f),
         "set translation updates node transform");

  const CommandResult removeProbe = fixture.bus.dispatch("remove /world/cube/probe");
  EXPECT(removeProbe.ok, "remove child node succeeds");
  EXPECT(fixture.scene->findByPath("/world/cube/probe") == nullptr,
         "remove detaches child from path lookup");

  const CommandResult removeCamera =
      fixture.bus.dispatch("remove /world/cube/debug_cam");
  EXPECT(removeCamera.ok, "remove child camera succeeds");
  EXPECT(fixture.scene->findByPath("/world/cube/debug_cam") == nullptr,
         "removed camera path no longer resolves");
}

void testBuiltinCamAndPreviewCommands() {
  CommandFixture fixture;

  const CommandResult camFov = fixture.bus.dispatch("cam fov 80");
  EXPECT(camFov.ok, "cam fov succeeds");
  EXPECT(nearlyEqual(fixture.camera->fovY, 80.0f),
         "cam fov updates active camera");

  const CommandResult camLookAt =
      fixture.bus.dispatch("cam look-at 1 2 3 0 0 0");
  EXPECT(camLookAt.ok, "cam look-at succeeds");
  EXPECT(nearlyEqual(fixture.camera->getEyePosition().x, 1.0f) &&
             nearlyEqual(fixture.camera->getEyePosition().y, 2.0f) &&
             nearlyEqual(fixture.camera->getEyePosition().z, 3.0f),
         "cam look-at updates camera position");

  const CommandResult camReset = fixture.bus.dispatch("cam reset");
  EXPECT(camReset.ok, "cam reset succeeds");
  EXPECT(nearlyEqual(fixture.camera->getEyePosition().z, 3.0f),
         "cam reset restores default eye distance");

  const CommandResult previewOn = fixture.bus.dispatch("preview on");
  EXPECT(previewOn.ok, "preview on succeeds");
  EXPECT(fixture.editorState.isPreviewEnabled(),
         "preview on updates editor state");

  const CommandResult previewToggle = fixture.bus.dispatch("preview toggle");
  EXPECT(previewToggle.ok, "preview toggle succeeds");
  EXPECT(!fixture.editorState.isPreviewEnabled(),
         "preview toggle flips editor state");
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

void testConsolePanelSubmitsAndClearsDisplay() {
  CommandFixture fixture;
  ConsolePanel panel(fixture.bus);

  panel.submitLine("select /world/cube");
  EXPECT(fixture.editorState.getSelected() == fixture.cube,
         "panel submit routes through builtin command bus");
  EXPECT(panel.displayedEntries().size() == 1,
         "panel display shows newly executed command");
  EXPECT(fixture.bus.history().size() == 1,
         "panel submit contributes to command history");

  panel.clearDisplay();
  EXPECT(panel.displayedEntries().empty(),
         "clearDisplay hides old output without mutating bus history");
  EXPECT(fixture.bus.history().size() == 1,
         "clearDisplay leaves command bus history intact");

  panel.submitLine("list nodes");
  EXPECT(panel.displayedEntries().size() == 1,
         "new entries appear after clearDisplay checkpoint");
  EXPECT(fixture.bus.history().size() == 2,
         "history keeps full record after clearDisplay");
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
}

} // namespace

int main() {
  testDispatchRecordsHistoryAndPreservesQuotedToken();
  testEscapesReachHandler();
  testUnknownVerbAndParseErrorsStayInBand();
  testHandlerExceptionBecomesFailedResult();
  testDispatchScriptSkipsCommentsAndContinuesAfterFailure();
  testListVerbsAndBriefAreDeterministic();
  testEditorStateUsesWeakSelection();
  testBuiltinHelpSelectAndDeselect();
  testBuiltinTransformCommandsAndGet();
  testBuiltinListCommands();
  testBuiltinCommandErrors();
  testBuiltinAddRemoveSetCommands();
  testBuiltinCamAndPreviewCommands();
  testBuiltinRemainingCommandErrors();
  testConsolePanelSubmitsAndClearsDisplay();
  testConsolePanelBrowseAndAutocomplete();

  if (failures != 0) {
    std::cerr << "test_command_bus failed with " << failures << " failure(s)\n";
    return 1;
  }

  std::cout << "test_command_bus passed\n";
  return 0;
}
