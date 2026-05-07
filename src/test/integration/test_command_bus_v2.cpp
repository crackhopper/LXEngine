#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/editor_state.hpp"
#include "core/math/quat.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace LX_core;

namespace {

constexpr float kEps = 1e-4f;
int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg \
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

    registerBuiltinCommands(bus, editorState, *scene);
  }
};

void testCompleterReturnsScenePathCandidates() {
  Fixture fixture;

  const CompletionResult completion = fixture.bus.complete("move /w");
  EXPECT(completion.candidates.size() >= 4,
         "move path completer should list matching scene paths");
  EXPECT(!completion.candidates.empty() && completion.candidates[0] == "/world",
         "completion should include /world root path");
  EXPECT(completion.commonPrefix == "/world",
         "common prefix should collapse to /world for sibling nodes");
}

void testUndoRedoThroughBusRestoresMoveAndSet() {
  Fixture fixture;

  const CommandResult move = fixture.bus.dispatch("move /world/a 1 2 3");
  EXPECT(move.ok, "move should succeed before undo");
  EXPECT(nearlyEqual(fixture.a->getTranslation().x, 1.0f) &&
             nearlyEqual(fixture.a->getTranslation().y, 2.0f) &&
             nearlyEqual(fixture.a->getTranslation().z, 3.0f),
         "move should update translation");
  EXPECT(fixture.bus.canUndo(), "successful mutable move should enter undo stack");

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

  const CommandResult select = fixture.bus.dispatch("select /world/a /world/b /world/c");
  EXPECT(select.ok, "multi-select should succeed");
  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 3, "multi-select should populate all selected nodes");
  EXPECT(selected[0] == fixture.a && selected[1] == fixture.b && selected[2] == fixture.c,
         "selection order should follow command arg order");
  EXPECT(fixture.editorState.getPrimarySelected().has_value() &&
             &fixture.editorState.getPrimarySelected()->get() == fixture.c.get(),
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

  const CommandResult move = fixture.bus.dispatch("move /world/a /world/b 1 0 0");
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

} // namespace

int main() {
  testCompleterReturnsScenePathCandidates();
  testUndoRedoThroughBusRestoresMoveAndSet();
  testMultiSelectKeepsPrimarySelectionOrder();
  testMultiTargetMoveAppliesDeltaAndUndoRestoresEachNode();

  if (failures != 0) {
    std::cerr << "test_command_bus_v2 failed with " << failures << " failure(s)\n";
    return 1;
  }

  std::cout << "test_command_bus_v2 passed\n";
  return 0;
}
