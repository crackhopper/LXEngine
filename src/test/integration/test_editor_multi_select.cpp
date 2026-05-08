#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/editor_state.hpp"
#include "core/editor/scene_tree_panel.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"

#include <iostream>

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

struct Fixture {
  LX_core::EditorState editorState;
  LX_core::CommandBus bus;
  LX_core::SceneNodeSharedPtr world = LX_core::SceneNode::create("world");
  LX_core::SceneNodeSharedPtr a = LX_core::SceneNode::create("a");
  LX_core::SceneNodeSharedPtr b = LX_core::SceneNode::create("b");
  LX_core::SceneNodeSharedPtr c = LX_core::SceneNode::create("c");
  LX_core::SceneNodeSharedPtr outside = LX_core::SceneNode::create("outside");
  LX_core::SceneSharedPtr scene;

  Fixture() {
    world->setName("world");
    a->setName("a");
    b->setName("b");
    c->setName("c");
    outside->setName("outside");

    a->setParent(world);
    b->setParent(world);
    c->setParent(world);

    scene = LX_core::Scene::create("editor_multi_select", world);
    scene->addRenderable(a);
    scene->addRenderable(b);
    scene->addRenderable(c);
    scene->addRenderable(outside);

    LX_core::registerBuiltinCommands(bus, editorState, *scene);
  }
};

void testCtrlClickAddsNodeAndUpdatesPrimarySelection() {
  Fixture fixture;
  LX_core::SceneTreePanel panel(fixture.bus, fixture.editorState, *fixture.scene);

  const auto first = fixture.bus.dispatch("select /world/a");
  EXPECT(first.ok, "initial select should succeed");

  const auto ctrlClick = panel.handleNodeClick(*fixture.b, true, false);
  EXPECT(ctrlClick.ok, "ctrl-click add should succeed");

  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 2, "ctrl-click should keep both selected nodes");
  EXPECT(selected[0] == fixture.a && selected[1] == fixture.b,
         "ctrl-click should append target path to selection");
  EXPECT(fixture.editorState.getPrimarySelected().has_value() &&
             &fixture.editorState.getPrimarySelected()->get() == fixture.b.get(),
         "ctrl-click target should become primary selection");
  EXPECT(fixture.bus.history().back().line == "select /world/a /world/b",
         "ctrl-click should dispatch one multi-path select command");
}

void testShiftClickSelectsSiblingRangeFromPrimaryAnchor() {
  Fixture fixture;
  LX_core::SceneTreePanel panel(fixture.bus, fixture.editorState, *fixture.scene);

  EXPECT(fixture.bus.dispatch("select /world/a").ok,
         "range anchor select should succeed");
  const auto shiftClick = panel.handleNodeClick(*fixture.c, false, true);
  EXPECT(shiftClick.ok, "shift-click range should succeed");

  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 3, "shift-click should include sibling range");
  EXPECT(selected[0] == fixture.a && selected[1] == fixture.b &&
             selected[2] == fixture.c,
         "shift-click should select contiguous sibling paths in order");
  EXPECT(fixture.editorState.getPrimarySelected().has_value() &&
             &fixture.editorState.getPrimarySelected()->get() == fixture.c.get(),
         "shift-click target should become primary selection");
  EXPECT(fixture.bus.history().back().line ==
             "select /world/a /world/b /world/c",
         "shift-click should dispatch one range select command");
}

void testShiftClickFallsBackToSingleSelectAcrossDifferentParents() {
  Fixture fixture;
  LX_core::SceneTreePanel panel(fixture.bus, fixture.editorState, *fixture.scene);

  EXPECT(fixture.bus.dispatch("select /world/b").ok,
         "initial select should succeed");
  const auto shiftClick = panel.handleNodeClick(*fixture.outside, false, true);
  EXPECT(shiftClick.ok, "cross-parent shift-click should still succeed");

  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.size() == 1 && selected[0] == fixture.outside,
         "cross-parent shift-click should fall back to single replace selection");
  EXPECT(fixture.bus.history().back().line == "select /outside",
         "cross-parent shift-click should dispatch single-node select");
}

} // namespace

int main() {
  expSetEnvVK();
  testCtrlClickAddsNodeAndUpdatesPrimarySelection();
  testShiftClickSelectsSiblingRangeFromPrimaryAnchor();
  testShiftClickFallsBackToSingleSelectAcrossDifferentParents();

  if (failures == 0) {
    std::cout << "[PASS] editor_multi_select tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
