#include "core/scene/light.hpp"
#include "core/scene/scene.hpp"
#include "editor/app/editor_state.hpp"
#include "editor/commands/builtin_commands.hpp"
#include "editor/commands/command_bus.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
  }
}

[[nodiscard]] bool requestsSceneRebuild(const LX_core::CommandResult &result) {
  const auto it = result.metadata.find("scene.rebuild");
  return it != result.metadata.end() && it->second == "true";
}

struct LightCommandFixture final {
  std::shared_ptr<LX_core::Scene> scene = LX_core::Scene::create(nullptr);
  LX_core::EditorState editorState{};
  LX_core::CommandBus bus{};
  LX_core::SceneNodeSharedPtr lightNode =
      LX_core::SceneNode::create("key_light");
  std::shared_ptr<LX_core::DirectionalLight> light =
      std::make_shared<LX_core::DirectionalLight>();

  LightCommandFixture() {
    lightNode->setName("key_light");
    scene->addRenderable(lightNode);
    scene->attachLight(lightNode, light);
    LX_core::registerBuiltinCommands(bus, editorState, *scene);
  }
};

void testLightTransformCommandsDoNotRequestSceneRebuild() {
  LightCommandFixture fixture;

  const LX_core::CommandResult move =
      fixture.bus.dispatch("move /key_light 0.5 1.0 -0.25");
  expect(move.ok, "moving attached directional light should succeed");
  expect(!requestsSceneRebuild(move),
         "moving attached directional light is a runtime data update, not a "
         "scene rebuild");

  const LX_core::CommandResult rotate =
      fixture.bus.dispatch("rotate /key_light 0 45 0");
  expect(rotate.ok, "rotating attached directional light should succeed");
  expect(!requestsSceneRebuild(rotate),
         "rotating attached directional light is a runtime data update, not a "
         "scene rebuild");

  const LX_core::CommandResult getDirection =
      fixture.bus.dispatch("get /key_light.light.direction");
  expect(getDirection.ok,
         "rotating attached directional light should still update light "
         "direction");
}

} // namespace

int main() {
  testLightTransformCommandsDoNotRequestSceneRebuild();
  return 0;
}
