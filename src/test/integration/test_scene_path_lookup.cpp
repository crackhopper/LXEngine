#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace LX_core;

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

class FakeShader final : public IShader {
public:
  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }

  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32, u32) const override {
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &) const override {
    return std::nullopt;
  }

  usize getProgramHash() const override { return 0; }
  std::string getShaderName() const override { return "fake_scene_path"; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

std::shared_ptr<SceneNode> makeNode(const std::string &nodeName) {
  auto vb = VertexBuffer<VertexPos>::create(
      std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = IndexBuffer::create({0, 1, 2});
  auto mesh = Mesh::create(vb, ib);

  auto shader = std::make_shared<FakeShader>();
  auto tmpl = MaterialTemplate::create("scene_path_lookup");

  ShaderProgramSet programSet;
  programSet.shaderName = "scene_path_lookup";
  programSet.shader = shader;

  MaterialPassDefinition passDef;
  passDef.shaderProgram = programSet;
  passDef.renderState = RenderState{};
  tmpl->setPassDefinition(Pass_Forward, std::move(passDef));

  auto material = MaterialInstance::create(tmpl);
  return SceneNode::create(nodeName, mesh, material, nullptr);
}

std::string captureStderr(const std::function<void()> &fn) {
  std::ostringstream captured;
  auto *old = std::cerr.rdbuf(captured.rdbuf());
  fn();
  std::cerr.rdbuf(old);
  return captured.str();
}

std::vector<std::string> dumpedPathsFromTree(const std::string &tree) {
  std::vector<std::string> paths;
  std::vector<std::string> stack;
  std::istringstream input(tree);
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line == "/") {
      continue;
    }

    usize depth = 0;
    usize offset = 0;
    while (offset + 4 <= line.size()) {
      const std::string token = line.substr(offset, 4);
      if (token == "│   " || token == "    ") {
        ++depth;
        offset += 4;
        continue;
      }
      break;
    }

    if (offset + 4 > line.size()) {
      continue;
    }

    const std::string branch = line.substr(offset, 4);
    if (branch != "├── " && branch != "└── ") {
      continue;
    }

    const std::string segment = line.substr(offset + 4);
    if (stack.size() <= depth) {
      stack.resize(depth + 1);
    }
    stack[depth] = segment;
    stack.resize(depth + 1);

    std::string path;
    for (const auto &part : stack) {
      path += "/" + part;
    }
    paths.push_back(std::move(path));
  }
  return paths;
}

void testRootLookupAndNestedPathLookup() {
  auto scene = Scene::create(nullptr);
  auto world = makeNode("node_world");
  world->setName("world");
  scene->addRenderable(world);

  auto player = makeNode("node_player");
  player->setName("player");
  player->setParent(world);

  auto arm = makeNode("node_arm");
  arm->setName("arm");
  arm->setParent(player);

  EXPECT(scene->findByPath("/") != nullptr, "root lookup returns synthetic root");
  EXPECT(scene->findByPath("/world") == world.get(), "top-level path resolves");
  EXPECT(scene->findByPath("/world/player/arm") == arm.get(),
         "nested path resolves");
  EXPECT(scene->findByPath("world/player/arm") == arm.get(),
         "root-relative shorthand resolves");
  EXPECT(scene->findByPath("/world/missing") == nullptr,
         "missing node returns null");
  EXPECT(world->getPath() == "/world", "top-level node path is rooted");
  EXPECT(arm->getPath() == "/world/player/arm", "child path reflects ancestors");
}

void testGetPathReflectsReparentingImmediately() {
  auto scene = Scene::create(nullptr);
  auto world = makeNode("node_world");
  world->setName("world");
  scene->addRenderable(world);

  auto player = makeNode("node_player");
  player->setName("player");
  player->setParent(world);

  auto arm = makeNode("node_arm");
  arm->setName("arm");
  arm->setParent(player);

  EXPECT(arm->getPath() == "/world/player/arm", "path starts under player");
  arm->setParent(world);
  EXPECT(arm->getPath() == "/world/arm", "path updates after reparent");
  EXPECT(scene->findByPath("/world/player/arm") == nullptr,
         "old path no longer resolves");
  EXPECT(scene->findByPath("/world/arm") == arm.get(),
         "new path resolves after reparent");
}

void testDuplicateSiblingNamesWarnAndResolveFirst() {
  auto scene = Scene::create(nullptr);
  auto enemies = makeNode("node_enemies");
  enemies->setName("enemies");
  scene->addRenderable(enemies);

  auto firstEnemy = makeNode("node_enemy_1");
  firstEnemy->setName("enemy");
  firstEnemy->setParent(enemies);

  auto secondEnemy = makeNode("node_enemy_2");
  secondEnemy->setName("enemy");
  const std::string warning = captureStderr(
      [&]() { secondEnemy->setParent(enemies); });

  EXPECT(warning.find("[WARN]") != std::string::npos,
         "duplicate sibling name emits warning");
  EXPECT(scene->findByPath("/enemies/enemy") == firstEnemy.get(),
         "duplicate sibling lookup returns first inserted child");
}

void testDumpTreePathsAreReversible() {
  auto scene = Scene::create(nullptr);
  auto world = makeNode("node_world");
  world->setName("world");
  scene->addRenderable(world);

  auto player = makeNode("node_player");
  player->setName("player");
  player->setParent(world);

  auto arm = makeNode("node_arm");
  arm->setName("arm");
  arm->setParent(player);

  auto ground = makeNode("node_ground");
  ground->setName("ground");
  ground->setParent(world);

  const std::string tree = scene->dumpTree();
  EXPECT(tree.find("/\n") == 0, "tree dump starts at root");
  EXPECT(tree.find("world") != std::string::npos, "tree dump contains world");
  EXPECT(tree.find("player") != std::string::npos, "tree dump contains player");
  EXPECT(tree.find("arm") != std::string::npos, "tree dump contains arm");
  EXPECT(tree.find("ground") != std::string::npos, "tree dump contains ground");

  for (const auto &path : dumpedPathsFromTree(tree)) {
    EXPECT(scene->findByPath(path) != nullptr,
           "every dumped path resolves through findByPath");
  }
}

void testNameSanitizationPreservesPathUsability() {
#ifdef NDEBUG
  auto scene = Scene::create(nullptr);
  auto weird = makeNode("node_weird");

  const std::string warning = captureStderr(
      [&]() { weird->setName("enemy/boss main"); });
  scene->addRenderable(weird);

  EXPECT(warning.empty(), "sanitization relies on assert instead of runtime warning");
  EXPECT(weird->getName() == "enemy_boss_main",
         "illegal characters are sanitized to underscores");
  EXPECT(scene->findByPath("/enemy_boss_main") == weird.get(),
         "sanitized node remains path-addressable");
#endif
}

} // namespace

int main() {
  testRootLookupAndNestedPathLookup();
  testGetPathReflectsReparentingImmediately();
  testDuplicateSiblingNamesWarnAndResolveFirst();
  testDumpTreePathsAreReversible();
  testNameSanitizationPreservesPathUsability();

  if (failures != 0) {
    std::cerr << "[FAIL] test_scene_path_lookup failures=" << failures << "\n";
    return 1;
  }

  std::cout << "[PASS] test_scene_path_lookup\n";
  return 0;
}
