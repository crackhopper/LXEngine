#include "editor/commands/command_bus.hpp"
#include "editor/commands/builtin_commands.hpp"
#include "editor/app/editor_state.hpp"
#include "core/math/quat.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace LX_core;

namespace {

constexpr float kEps = 1e-4f;
int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

[[nodiscard]] bool nearlyEqual(const float a, const float b) {
  return std::fabs(a - b) <= kEps;
}

struct Fixture {
  SceneSharedPtr scene = Scene::create(nullptr);
  EditorState editorState;
  CommandBus bus;
  SceneNodeSharedPtr world = SceneNode::create("node_world");
  SceneNodeSharedPtr a = SceneNode::create("node_a");
  SceneNodeSharedPtr b = SceneNode::create("node_b");
  SceneNodeSharedPtr c = SceneNode::create("node_c");
  SceneNodeSharedPtr cameraNode = SceneNode::create("node_camera");
  std::optional<std::string> materialUri =
      std::string("assets/materials/pbr.material");
  std::optional<MaterialParameterValue> baseColorParameter =
      MaterialParameterValue{
          .type = MaterialParameterValueType::Vec3,
          .vectorValue = {0.8f, 0.8f, 0.8f, 0.0f},
      };

  Fixture() {
    world->setName("world");
    scene->addRenderable(world);

    a->setName("a");
    a->setParent(world);
    scene->addRenderable(a);

    b->setName("b");
    b->setParent(world);
    scene->addRenderable(b);

    c->setName("c");
    c->setParent(world);
    scene->addRenderable(c);

    cameraNode->setName("camera_main");
    cameraNode->addComponent<CameraComponent>();
    scene->addCamera(cameraNode);

    registerBuiltinCommands(
        bus, editorState, *scene,
        SceneIoContext{
            .createNode =
                [](const std::string &, const std::string &nodeName,
                   const std::string &displayName,
                   SceneNodeSharedPtr &outNode) {
                  outNode = SceneNode::create(nodeName);
                  outNode->setName(displayName);
                  return CommandResult{true, "created", {}, {}};
                },
            .getMaterialUri =
                [this](const std::string &) { return materialUri; },
            .setMaterialUri =
                [this](const std::string &, const std::string &uri) {
                  materialUri = uri;
                  return CommandResult{true, "materialUri updated", "{}", {}};
                },
            .getNodeMaterialParameter =
                [this](const std::string &, const std::string &binding,
                       const std::string &member) {
                  if (binding == "TestMaterialBlock" &&
                      member == "baseColor") {
                    return baseColorParameter;
                  }
                  return std::optional<MaterialParameterValue>{};
                },
            .setNodeMaterialParameter =
                [this](const std::string &, const std::string &binding,
                       const std::string &member,
                       const MaterialParameterValue &value) {
                  if (binding != "TestMaterialBlock" ||
                      member != "baseColor") {
                    return CommandResult{false, "unexpected parameter", {},
                                         {}};
                  }
                  baseColorParameter = value;
                  return CommandResult{
                      true, "node material parameter updated", "{}", {}};
                },
        });
  }
};

void testCompleterReturnsScenePathCandidates() {
  Fixture fixture;

  const CompletionResult addCompletion = fixture.bus.complete("add camera");
  EXPECT(addCompletion.candidates.size() == 1 &&
             addCompletion.candidates[0] == "camera:perspective",
         "add completer should suggest concrete creation kinds");
  EXPECT(addCompletion.commonPrefix == "camera:perspective",
         "component type completer should collapse to full match");

  const CompletionResult completion = fixture.bus.complete("move /w");
  EXPECT(completion.candidates.size() >= 4,
         "move path completer should list matching scene paths");
  EXPECT(!completion.candidates.empty() && completion.candidates[0] == "/world",
         "completion should include /world root path");
  EXPECT(completion.commonPrefix == "/world",
         "common prefix should collapse to /world for sibling nodes");

  const CompletionResult setPathCompletion =
      fixture.bus.complete("set /world/a.t");
  EXPECT(setPathCompletion.candidates.size() == 1 &&
             setPathCompletion.candidates[0] == "/world/a.translation",
         "set completer should expand editable field suffixes");
  EXPECT(setPathCompletion.commonPrefix == "/world/a.translation",
         "set field completer should return full common prefix");

  const CompletionResult getPathCompletion =
      fixture.bus.complete("get /camera_main.proj");
  EXPECT(getPathCompletion.candidates.size() == 1 &&
             getPathCompletion.candidates[0] == "/camera_main.projection",
         "get completer should share editable field path completion");
}

void testUndoRedoThroughBusRestoresMoveAndSet() {
  Fixture fixture;

  const CommandResult move = fixture.bus.dispatch("move /world/a 1 2 3");
  EXPECT(move.ok, "move should succeed before undo");
  EXPECT(nearlyEqual(fixture.a->getTranslation().x, 1.0f) &&
             nearlyEqual(fixture.a->getTranslation().y, 2.0f) &&
             nearlyEqual(fixture.a->getTranslation().z, 3.0f),
         "move should update translation");
  EXPECT(fixture.bus.canUndo(),
         "successful mutable move should enter undo stack");

  const CommandResult undoMove = fixture.bus.undo();
  EXPECT(undoMove.ok, "bus.undo should succeed after move");
  EXPECT(nearlyEqual(fixture.a->getTranslation().x, 0.0f) &&
             nearlyEqual(fixture.a->getTranslation().y, 0.0f) &&
             nearlyEqual(fixture.a->getTranslation().z, 0.0f),
         "undo should restore pre-move translation");
  EXPECT(fixture.bus.canRedo(), "undo should populate redo stack");

  const CommandResult redoMove = fixture.bus.redo();
  EXPECT(redoMove.ok, "bus.redo should succeed after undo");
  EXPECT(nearlyEqual(fixture.a->getTranslation().x, 1.0f) &&
             nearlyEqual(fixture.a->getTranslation().y, 2.0f) &&
             nearlyEqual(fixture.a->getTranslation().z, 3.0f),
         "redo should replay forward move");

  const CommandResult rename = fixture.bus.dispatch("set /world/a.name hero");
  EXPECT(rename.ok, "set name should succeed before undo");
  EXPECT(fixture.a->getName() == "hero", "rename should update node name");
  EXPECT(fixture.scene->findByPath("/world/hero") == fixture.a.get(),
         "renamed path should resolve immediately");

  const CommandResult undoRename = fixture.bus.dispatch("undo");
  EXPECT(undoRename.ok, "undo command should route through command bus");
  EXPECT(fixture.a->getName() == "a", "undo should restore old node name");
  EXPECT(fixture.scene->findByPath("/world/a") == fixture.a.get(),
         "undo should restore old path resolution");

  const CommandResult redoRename = fixture.bus.dispatch("redo");
  EXPECT(redoRename.ok, "redo command should route through command bus");
  EXPECT(fixture.a->getName() == "hero", "redo should reapply rename");
}

void testMultiSelectKeepsPrimarySelectionOrder() {
  Fixture fixture;

  const CommandResult select =
      fixture.bus.dispatch("select /world/a /world/b /world/c");
  EXPECT(select.ok, "multi-select should succeed");
  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 3,
         "multi-select should populate all selected nodes");
  EXPECT(selected[0] == fixture.a && selected[1] == fixture.b &&
             selected[2] == fixture.c,
         "selection order should follow command arg order");
  EXPECT(fixture.editorState.getPrimarySelected().has_value() &&
             &fixture.editorState.getPrimarySelected()->get() ==
                 fixture.c.get(),
         "primary selection should be last selected node");

  const CommandResult undoSelect = fixture.bus.dispatch("undo");
  EXPECT(undoSelect.ok, "undo should restore pre-select selection state");
  EXPECT(fixture.editorState.getSelected().empty(),
         "undo after first select should clear selection");
}

void testMultiTargetMoveAppliesDeltaAndUndoRestoresEachNode() {
  Fixture fixture;
  fixture.a->setTranslation({2.0f, 0.0f, 0.0f});
  fixture.b->setTranslation({5.0f, 1.0f, 0.0f});

  const CommandResult move =
      fixture.bus.dispatch("move /world/a /world/b 1 0 0");
  EXPECT(move.ok, "multi-target move should succeed");
  EXPECT(nearlyEqual(fixture.a->getTranslation().x, 3.0f) &&
             nearlyEqual(fixture.b->getTranslation().x, 6.0f),
         "multi-target move should apply delta to each target");

  const CommandResult undoMove = fixture.bus.dispatch("undo");
  EXPECT(undoMove.ok, "undo command should restore multi-target move");
  EXPECT(nearlyEqual(fixture.a->getTranslation().x, 2.0f) &&
             nearlyEqual(fixture.b->getTranslation().x, 5.0f),
         "undo should restore distinct pre-move translations");

  const CommandResult redoMove = fixture.bus.dispatch("redo");
  EXPECT(redoMove.ok, "redo command should replay multi-target move");
  EXPECT(nearlyEqual(fixture.a->getTranslation().x, 3.0f) &&
             nearlyEqual(fixture.b->getTranslation().x, 6.0f),
         "redo should reapply batch delta");
}

void testPreviewAndCamFovGainUndoCoverage() {
  Fixture fixture;
  auto camera = fixture.cameraNode->getComponent<CameraComponent>();
  camera->get().setFovY(60.0f);

  const CommandResult previewOn = fixture.bus.dispatch("preview on");
  EXPECT(previewOn.ok, "preview on should succeed");
  EXPECT(fixture.editorState.isPreviewEnabled(),
         "preview on should enable preview mode");

  const CommandResult undoPreview = fixture.bus.dispatch("undo");
  EXPECT(undoPreview.ok, "undo should restore preview state");
  EXPECT(!fixture.editorState.isPreviewEnabled(),
         "undo should restore previous preview-disabled state");

  const CommandResult camFov = fixture.bus.dispatch("cam fov 75");
  EXPECT(camFov.ok, "cam fov should succeed");
  EXPECT(nearlyEqual(camera->get().getFovY(), 75.0f),
         "cam fov should update active camera fov");

  const CommandResult undoFov = fixture.bus.dispatch("undo");
  EXPECT(undoFov.ok, "undo should restore previous camera fov");
  EXPECT(nearlyEqual(camera->get().getFovY(), 60.0f),
         "undo should restore camera fov");
}

void testAddRemoveSupportUndoRedo() {
  Fixture fixture;

  const CommandResult selectParent = fixture.bus.dispatch("select /world");
  EXPECT(selectParent.ok, "selecting add parent should succeed");

  const CommandResult addCamera =
      fixture.bus.dispatch("add camera:perspective debug_cam");
  EXPECT(addCamera.ok, "add camera should succeed");
  EXPECT(fixture.scene->findByPath("/world/debug_cam") != nullptr,
         "added camera path should resolve");

  const CommandResult undoAdd = fixture.bus.dispatch("undo");
  EXPECT(undoAdd.ok, "undo should remove newly added camera");
  EXPECT(fixture.scene->findByPath("/world/debug_cam") == nullptr,
         "undo add should remove created camera node");

  const CommandResult redoAdd = fixture.bus.dispatch("redo");
  EXPECT(redoAdd.ok, "redo should restore add camera");
  auto *restoredCameraNode = fixture.scene->findByPath("/world/debug_cam");
  EXPECT(restoredCameraNode != nullptr, "redo add should recreate camera node");

  const CommandResult setCameraFov =
      fixture.bus.dispatch("set /world/debug_cam.fov 75");
  EXPECT(setCameraFov.ok, "set camera field before remove should succeed");

  const CommandResult removeCamera =
      fixture.bus.dispatch("remove /world/debug_cam");
  EXPECT(removeCamera.ok, "remove camera should succeed");
  EXPECT(fixture.scene->findByPath("/world/debug_cam") == nullptr,
         "removed camera path should disappear");

  const CommandResult undoRemove = fixture.bus.dispatch("undo");
  EXPECT(undoRemove.ok, "undo should restore removed camera");
  restoredCameraNode = fixture.scene->findByPath("/world/debug_cam");
  EXPECT(restoredCameraNode != nullptr,
         "undo remove should restore camera path");
  auto restoredCamera = restoredCameraNode->getComponent<CameraComponent>();
  EXPECT(restoredCamera.has_value() &&
             nearlyEqual(restoredCamera->get().getFovY(), 75.0f),
         "undo remove should restore camera component state");

  const CommandResult redoRemove = fixture.bus.dispatch("redo");
  EXPECT(redoRemove.ok, "redo should remove restored camera again");
  EXPECT(fixture.scene->findByPath("/world/debug_cam") == nullptr,
         "redo remove should remove camera again");
}

void testMaterialEditsRequestSceneRebuild() {
  Fixture fixture;

  const CommandResult legacySetColor =
      fixture.bus.dispatch("set /world/a.nodeMaterial.baseColor 0.2 0.3 0.4");
  EXPECT(!legacySetColor.ok, "legacy node material baseColor should fail");

  const CommandResult setColor = fixture.bus.dispatch(
      "set /world/a.nodeMaterial.TestMaterialBlock.baseColor 0.2 0.3 0.4");
  EXPECT(setColor.ok, "set generic node material baseColor should succeed");
  EXPECT(setColor.metadata.find("scene.rebuild") != setColor.metadata.end() &&
             setColor.metadata.at("scene.rebuild") == "true",
         "set generic node material baseColor should request scene rebuild");

  const CommandResult setUri = fixture.bus.dispatch(
      "set /world/a.materialUri assets/materials/pbr_gold.material");
  EXPECT(setUri.ok, "set materialUri should succeed");
  EXPECT(setUri.metadata.find("scene.rebuild") != setUri.metadata.end() &&
             setUri.metadata.at("scene.rebuild") == "true",
         "set materialUri should request scene rebuild");

  const CommandResult undoUri = fixture.bus.dispatch("undo");
  EXPECT(undoUri.ok, "undo materialUri should succeed");
  EXPECT(undoUri.metadata.find("scene.rebuild") != undoUri.metadata.end() &&
             undoUri.metadata.at("scene.rebuild") == "true",
         "undo materialUri should request scene rebuild");
}

void testConcreteAddKindsUseHistory() {
  Fixture fixture;

  const CommandResult addCube =
      fixture.bus.dispatch("add primitive:cube Cube /world 1 2 3");
  EXPECT(addCube.ok, "add primitive cube should use concrete kind");
  auto *cube = fixture.scene->findByPath("/world/Cube");
  EXPECT(cube != nullptr, "added primitive should resolve by path");
  EXPECT(cube != nullptr && nearlyEqual(cube->getTranslation().x, 1.0f) &&
             nearlyEqual(cube->getTranslation().y, 2.0f) &&
             nearlyEqual(cube->getTranslation().z, 3.0f),
         "add primitive should apply explicit placement");

  const CommandResult undoCube = fixture.bus.dispatch("undo");
  EXPECT(undoCube.ok, "undo should remove added primitive");
  EXPECT(fixture.scene->findByPath("/world/Cube") == nullptr,
         "undo add primitive should remove node");

  const CommandResult redoCube = fixture.bus.dispatch("redo");
  EXPECT(redoCube.ok, "redo should restore added primitive");
  EXPECT(fixture.scene->findByPath("/world/Cube") != nullptr,
         "redo add primitive should restore node");

  const CommandResult addLight =
      fixture.bus.dispatch("add light:directional Key /world");
  EXPECT(addLight.ok, "add directional light should succeed");
  auto *lightNode = fixture.scene->findByPath("/world/Key");
  EXPECT(lightNode != nullptr &&
             fixture.scene->getDirectionalLight(*lightNode).has_value(),
         "add directional light should attach a light payload");

  const CommandResult addModelWithDisplayName = fixture.bus.dispatch(
      "add model:characters_blocky_a \"Blocky Character A\" /world");
  EXPECT(addModelWithDisplayName.ok,
         "add model should accept asset display names with spaces");
  EXPECT(fixture.scene->findByPath("/world/Blocky_Character_A") != nullptr,
         "add model should sanitize display name for scene path");
}

void testCopyPasteAsSiblingDuplicatesCameraAndSelection() {
  Fixture fixture;
  const auto camera = fixture.cameraNode->getComponent<CameraComponent>();
  camera->get().setFovY(72.0f);
  camera->get().setCullingMask(0x12u);

  const CommandResult copy = fixture.bus.dispatch("copy /camera_main");
  EXPECT(copy.ok, "copy camera should succeed");
  const CommandResult paste =
      fixture.bus.dispatch("paste_as_sibling /camera_main");
  EXPECT(paste.ok, "paste_as_sibling camera should succeed");

  SceneNode *copyNode = fixture.scene->findByPath("/camera_main.copy");
  EXPECT(copyNode != nullptr, "paste should create a copied sibling path");
  EXPECT(copyNode != fixture.cameraNode.get(),
         "paste should create a distinct node");
  EXPECT(copyNode != nullptr &&
             copyNode->getParent() == fixture.cameraNode->getParent(),
         "paste_as_sibling should use target parent");
  if (copyNode != nullptr) {
    const auto copiedCamera = copyNode->getComponent<CameraComponent>();
    EXPECT(copiedCamera.has_value(),
           "camera duplicate should keep camera payload");
    EXPECT(copiedCamera.has_value() &&
               nearlyEqual(copiedCamera->get().getFovY(), 72.0f),
           "camera duplicate should preserve fov");
    EXPECT(copiedCamera.has_value() &&
               copiedCamera->get().getCullingMask() == 0x12u,
           "camera duplicate should preserve culling mask");
  }
  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 1 && selected[0].get() == copyNode,
         "paste should select the new node");

  const CommandResult undo = fixture.bus.dispatch("undo");
  EXPECT(undo.ok, "undo paste should succeed");
  EXPECT(fixture.scene->findByPath("/camera_main.copy") == nullptr,
         "undo paste should remove copied camera");

  const CommandResult redo = fixture.bus.dispatch("redo");
  EXPECT(redo.ok, "redo paste should succeed");
  EXPECT(fixture.scene->findByPath("/camera_main.copy") != nullptr,
         "redo paste should restore copied camera");
}

void testCopyPasteAsSiblingDuplicatesDirectionalLightIndependently() {
  Fixture fixture;
  const CommandResult addLight = fixture.bus.dispatch("add light sun /world");
  EXPECT(addLight.ok, "add light should succeed before duplicate");
  const CommandResult setColor =
      fixture.bus.dispatch("set /world/sun.color 0.2 0.4 0.6");
  EXPECT(setColor.ok, "set light color should succeed before duplicate");

  const CommandResult copy = fixture.bus.dispatch("copy /world/sun");
  EXPECT(copy.ok, "copy light should succeed");
  const CommandResult paste =
      fixture.bus.dispatch("paste_as_sibling /world/sun");
  EXPECT(paste.ok, "paste light should succeed");

  SceneNode *originalNode = fixture.scene->findByPath("/world/sun");
  SceneNode *copyNode = fixture.scene->findByPath("/world/sun.copy");
  EXPECT(originalNode != nullptr && copyNode != nullptr,
         "light copy should create copied sibling");
  if (originalNode != nullptr && copyNode != nullptr) {
    const auto originalLight =
        fixture.scene->getDirectionalLight(*originalNode);
    const auto copiedLight = fixture.scene->getDirectionalLight(*copyNode);
    EXPECT(originalLight.has_value() && copiedLight.has_value(),
           "light duplicate should keep directional light payload");
    EXPECT(originalLight.has_value() && copiedLight.has_value() &&
               &originalLight->get() != &copiedLight->get(),
           "light duplicate should create an independent light binding");
    if (copiedLight) {
      EXPECT(nearlyEqual(copiedLight->get().getColor().x, 0.2f) &&
                 nearlyEqual(copiedLight->get().getColor().y, 0.4f) &&
                 nearlyEqual(copiedLight->get().getColor().z, 0.6f),
             "light duplicate should preserve color payload");
    }
  }
}

void testRenameRejectsSiblingConflict() {
  Fixture fixture;

  const CommandResult conflict = fixture.bus.dispatch("set /world/a.name b");
  EXPECT(!conflict.ok, "rename to sibling name should fail");
  EXPECT(conflict.message == "rename conflict: sibling already named b",
         "rename conflict should use stable message");
  EXPECT(fixture.scene->findByPath("/world/a") == fixture.a.get(),
         "failed rename should keep original path");
}

} // namespace

int main() {
  testCompleterReturnsScenePathCandidates();
  testUndoRedoThroughBusRestoresMoveAndSet();
  testMultiSelectKeepsPrimarySelectionOrder();
  testMultiTargetMoveAppliesDeltaAndUndoRestoresEachNode();
  testPreviewAndCamFovGainUndoCoverage();
  testAddRemoveSupportUndoRedo();
  testMaterialEditsRequestSceneRebuild();
  testConcreteAddKindsUseHistory();
  testCopyPasteAsSiblingDuplicatesCameraAndSelection();
  testCopyPasteAsSiblingDuplicatesDirectionalLightIndependently();
  testRenameRejectsSiblingConflict();

  if (failures != 0) {
    std::cerr << "test_command_bus_v2 failed with " << failures
              << " failure(s)\n";
    return 1;
  }

  std::cout << "test_command_bus_v2 passed\n";
  return 0;
}
