#include "core/asset/mesh.hpp"
#include "core/asset/skeleton.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace LX_core;
using namespace LX_infra;

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

struct VertexPosOnly {
  Vec3f pos;

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {{"inPosition", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexPosOnly, pos)}},
        sizeof(VertexPosOnly)};
    return layout;
  }
};

struct VertexPosColorOnly {
  Vec3f pos;
  Vec4f color;

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {{"inPosition", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexPosColorOnly, pos)},
         {"inColor", 6, DataType::Float4, sizeof(Vec4f),
          offsetof(VertexPosColorOnly, color)}},
        sizeof(VertexPosColorOnly)};
    return layout;
  }
};

struct VertexPosUvOnly {
  Vec3f pos;
  Vec2f uv;

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {{"inPosition", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexPosUvOnly, pos)},
         {"inUV", 2, DataType::Float2, sizeof(Vec2f),
          offsetof(VertexPosUvOnly, uv)}},
        sizeof(VertexPosUvOnly)};
    return layout;
  }
};

struct VertexPosNormalOnly {
  Vec3f pos;
  Vec3f normal;

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {{"inPosition", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexPosNormalOnly, pos)},
         {"inNormal", 1, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexPosNormalOnly, normal)}},
        sizeof(VertexPosNormalOnly)};
    return layout;
  }
};

struct VertexPosNormalUvOnly {
  Vec3f pos;
  Vec3f normal;
  Vec2f uv;

  static const VertexLayout &getLayout() {
    static VertexLayout layout = {
        {{"inPosition", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexPosNormalUvOnly, pos)},
         {"inNormal", 1, DataType::Float3, sizeof(Vec3f),
          offsetof(VertexPosNormalUvOnly, normal)},
         {"inUV", 2, DataType::Float2, sizeof(Vec2f),
          offsetof(VertexPosNormalUvOnly, uv)}},
        sizeof(VertexPosNormalUvOnly)};
    return layout;
  }
};

template <typename TVertex> MeshSharedPtr makeMesh(std::vector<TVertex> vertices) {
  auto vb = VertexBuffer<TVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create({0, 1, 2});
  return Mesh::create(vb, ib);
}

MeshSharedPtr makeMeshWithSkinningInputs() {
  return makeMesh<VertexPosNormalUvBone>(
      std::vector<VertexPosNormalUvBone>{
          VertexPosNormalUvBone{{0, 0, 0},
                                {0, 1, 0},
                                {0, 0},
                                {1, 0, 0, 1},
                                {0, 0, 0, 0},
                                {1, 0, 0, 0}},
          VertexPosNormalUvBone{{1, 0, 0},
                                {0, 1, 0},
                                {1, 0},
                                {1, 0, 0, 1},
                                {0, 0, 0, 0},
                                {1, 0, 0, 0}},
          VertexPosNormalUvBone{{0, 1, 0},
                                {0, 1, 0},
                                {0, 1},
                                {1, 0, 0, 1},
                                {0, 0, 0, 0},
                                {1, 0, 0, 0}},
      });
}

MeshSharedPtr makeMeshWithoutSkinningInputs() {
  std::vector<VertexPBR> vertices(3);
  vertices[0].pos = {0, 0, 0};
  vertices[0].normal = {0, 1, 0};
  vertices[0].uv = {0, 0};
  vertices[0].tangent = {1, 0, 0, 1};
  vertices[1].pos = {1, 0, 0};
  vertices[1].normal = {0, 1, 0};
  vertices[1].uv = {1, 0};
  vertices[1].tangent = {1, 0, 0, 1};
  vertices[2].pos = {0, 1, 0};
  vertices[2].normal = {0, 1, 0};
  vertices[2].uv = {0, 1};
  vertices[2].tangent = {1, 0, 0, 1};
  return makeMesh<VertexPBR>(std::move(vertices));
}

MeshSharedPtr makeMeshPositionOnly() {
  return makeMesh<VertexPosOnly>({{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
}

MeshSharedPtr makeMeshWithVertexColorOnly() {
  return makeMesh<VertexPosColorOnly>(
      {{{0, 0, 0}, {1, 0, 0, 1}},
       {{1, 0, 0}, {0, 1, 0, 1}},
       {{0, 1, 0}, {0, 0, 1, 1}}});
}

MeshSharedPtr makeMeshWithUvOnly() {
  return makeMesh<VertexPosUvOnly>(
      {{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{0, 1, 0}, {0, 1}}});
}

MeshSharedPtr makeMeshWithNormalOnly() {
  return makeMesh<VertexPosNormalOnly>(
      {{{0, 0, 0}, {0, 1, 0}},
       {{1, 0, 0}, {0, 1, 0}},
       {{0, 1, 0}, {0, 1, 0}}});
}

MeshSharedPtr makeMeshWithNormalAndUvOnly() {
  return makeMesh<VertexPosNormalUvOnly>(
      {{{0, 0, 0}, {0, 1, 0}, {0, 0}},
       {{1, 0, 0}, {0, 1, 0}, {1, 0}},
       {{0, 1, 0}, {0, 1, 0}, {0, 1}}});
}

SkeletonSharedPtr makeSkeleton() {
  std::vector<Bone> bones = {
      Bone{"root", -1, Vec3f{0, 0, 0}, Quatf{}, Vec3f{1, 1, 1}},
  };
  return Skeleton::create(bones);
}

std::filesystem::path findProjectMaterialsDir() {
  auto p = std::filesystem::current_path();
  for (int i = 0; i < 5; ++i) {
    if (std::filesystem::exists(p / "materials"))
      return p / "materials";
    auto parent = p.parent_path();
    if (parent == p)
      break;
    p = parent;
  }
  return std::filesystem::current_path() / "materials";
}

MaterialInstanceSharedPtr makeMaterialFromYaml(const std::string &yamlContent) {
  auto tmpPath = findProjectMaterialsDir() / "test_node_val.material";
  {
    std::ofstream out(tmpPath);
    out << yamlContent;
  }
  auto mat = loadGenericMaterial(tmpPath);
  std::filesystem::remove(tmpPath);
  return mat;
}

MaterialInstanceSharedPtr makeMaterial(bool skinning) {
  std::string yaml =
      "shader: blinnphong_0\n"
      "variants:\n"
      "  USE_LIGHTING: true\n"
      "  USE_SKINNING: " + std::string(skinning ? "true" : "false") + "\n"
      "variantRules:\n"
      "  - requires: [USE_SKINNING]\n"
      "    depends: [USE_LIGHTING]\n"
      "passes:\n"
      "  Forward:\n"
      "    renderState:\n"
      "      depthTest: true\n";
  return makeMaterialFromYaml(yaml);
}

MaterialInstanceSharedPtr
makeMaterial(std::vector<ShaderVariant> variants) {
  std::string yaml =
      "shader: blinnphong_0\n"
      "variants:\n";
  for (const auto &v : variants)
    yaml += "  " + v.macroName + ": " + (v.enabled ? "true" : "false") + "\n";
  yaml +=
      "variantRules:\n"
      "  - requires: [USE_NORMAL_MAP]\n"
      "    depends: [USE_LIGHTING, USE_UV]\n"
      "  - requires: [USE_SKINNING]\n"
      "    depends: [USE_LIGHTING]\n"
      "passes:\n"
      "  Forward:\n"
      "    renderState:\n"
      "      depthTest: true\n";
  return makeMaterialFromYaml(yaml);
}

bool hasBinding(const std::vector<IGpuResourceSharedPtr> &resources,
                const char *bindingName) {
  const StringID id(bindingName);
  for (const auto &resource : resources) {
    if (resource && resource->getBindingName() == id)
      return true;
  }
  return false;
}

int runSelf(const std::filesystem::path &self, const char *mode) {
  const std::string cmd =
      "\"" + self.string() + "\" " + mode + " >/dev/null 2>&1";
  return std::system(cmd.c_str());
}

void testIndependentSceneNodeValidation() {
  auto node = SceneNode::create("node_base", makeMeshWithSkinningInputs(),
                                makeMaterial(false), nullptr);
  EXPECT(node->supportsPass(Pass_Forward),
         "independent SceneNode should validate without Scene");
  auto validated = node->getValidatedPassData(Pass_Forward);
  EXPECT(validated.has_value(), "validated pass data should exist");
  if (validated) {
    EXPECT(!hasBinding(validated->get().descriptorResources, "Bones"),
           "non-skinned node should not carry Bones");
  }
}

void testPassEnableStateRebuildsCache() {
  auto material = makeMaterial(false);
  auto node = SceneNode::create("node_toggle", makeMeshWithSkinningInputs(),
                                material, nullptr);
  EXPECT(node->supportsPass(Pass_Forward), "forward pass starts enabled");

  material->setPassEnabled(Pass_Forward, false);
  EXPECT(!node->supportsPass(Pass_Forward),
         "disabling a pass invalidates SceneNode cache");
  EXPECT(!node->getValidatedPassData(Pass_Forward).has_value(),
         "validated entry removed when pass disabled");

  material->setPassEnabled(Pass_Forward, true);
  EXPECT(node->supportsPass(Pass_Forward),
         "reenabling a pass rebuilds SceneNode cache");
  EXPECT(node->getValidatedPassData(Pass_Forward).has_value(),
         "validated entry restored when pass reenabled");
}

void testSharedMaterialPassChangesRevalidateAllSceneNodes() {
  auto material = makeMaterial(false);
  auto nodeA = SceneNode::create("node_shared_a", makeMeshWithSkinningInputs(),
                                 material, nullptr);
  auto nodeB = SceneNode::create("node_shared_b", makeMeshWithSkinningInputs(),
                                 material, nullptr);
  auto scene = Scene::create("SharedScene", nodeA);
  scene->addRenderable(nodeB);

  EXPECT(nodeA->supportsPass(Pass_Forward), "nodeA starts validated");
  EXPECT(nodeB->supportsPass(Pass_Forward), "nodeB starts validated");

  material->setPassEnabled(Pass_Forward, false);
  EXPECT(!nodeA->supportsPass(Pass_Forward),
         "shared material disable propagates to first node");
  EXPECT(!nodeB->supportsPass(Pass_Forward),
         "shared material disable propagates to second node");

  material->setPassEnabled(Pass_Forward, true);
  EXPECT(nodeA->supportsPass(Pass_Forward),
         "shared material reenable rebuilds first node");
  EXPECT(nodeB->supportsPass(Pass_Forward),
         "shared material reenable rebuilds second node");
}

void testSceneDestructionDetachesSceneNodesFromMaterialListener() {
  auto material = makeMaterial(false);
  auto node = SceneNode::create("node_detach", makeMeshWithSkinningInputs(),
                                material, nullptr);

  {
    auto scene = Scene::create("TemporaryScene", node);
    EXPECT(node->supportsPass(Pass_Forward), "scene-owned node starts validated");
  }

  material->setPassEnabled(Pass_Forward, false);
  EXPECT(!node->supportsPass(Pass_Forward),
         "detached node should rebuild locally after scene destruction");
  EXPECT(!node->getValidatedPassData(Pass_Forward).has_value(),
         "detached node should clear disabled pass after scene destruction");

  material->setPassEnabled(Pass_Forward, true);
  EXPECT(node->supportsPass(Pass_Forward),
         "detached node should revalidate locally after scene destruction");
}

void testOrdinaryMaterialWritesDoNotChangeValidatedPassState() {
  auto material = makeMaterial(false);
  auto nodeA = SceneNode::create("node_non_structural_a",
                                 makeMeshWithSkinningInputs(), material,
                                 nullptr);
  auto nodeB = SceneNode::create("node_non_structural_b",
                                 makeMeshWithSkinningInputs(), material,
                                 nullptr);
  auto scene = Scene::create("NonStructuralScene", nodeA);
  scene->addRenderable(nodeB);

  auto beforeA = nodeA->getValidatedPassData(Pass_Forward);
  auto beforeB = nodeB->getValidatedPassData(Pass_Forward);
  EXPECT(beforeA.has_value(), "nodeA validated before non-structural write");
  EXPECT(beforeB.has_value(), "nodeB validated before non-structural write");

  material->setParameter(StringID("MaterialUBO"), StringID("shininess"), 42.0f);
  material->syncGpuData();

  auto afterA = nodeA->getValidatedPassData(Pass_Forward);
  auto afterB = nodeB->getValidatedPassData(Pass_Forward);
  EXPECT(nodeA->supportsPass(Pass_Forward),
         "nodeA stays supported after ordinary material write");
  EXPECT(nodeB->supportsPass(Pass_Forward),
         "nodeB stays supported after ordinary material write");
  EXPECT(afterA.has_value(), "nodeA validated data survives ordinary write");
  EXPECT(afterB.has_value(), "nodeB validated data survives ordinary write");
}

void testOptionalSampledResourcesDoNotBlockValidation() {
  auto material = makeMaterial({ShaderVariant{"USE_UV", true},
                                ShaderVariant{"USE_LIGHTING", false}});
  auto node = SceneNode::create("node_optional_textures", makeMeshWithUvOnly(),
                                material, nullptr);

  EXPECT(node->supportsPass(Pass_Forward),
         "optional sampled resources should not be required structurally");
  auto validated = node->getValidatedPassData(Pass_Forward);
  EXPECT(validated.has_value(),
         "validated pass data should exist without bound optional textures");
  if (validated) {
    EXPECT(hasBinding(validated->get().descriptorResources, "MaterialUBO"),
           "material buffer should still be bound");
    EXPECT(!hasBinding(validated->get().descriptorResources, "albedoMap"),
           "missing optional texture should be skipped");
  }
}

void testSkinningVariantChangesPipelineKeyAndAddsBones() {
  auto mesh = makeMeshWithSkinningInputs();
  auto baseNode =
      SceneNode::create("node_unskinned", mesh, makeMaterial(false), nullptr);
  auto skinnedNode =
      SceneNode::create("node_skinned", mesh, makeMaterial(true), makeSkeleton());

  auto baseData = baseNode->getValidatedPassData(Pass_Forward);
  auto skinnedData = skinnedNode->getValidatedPassData(Pass_Forward);
  EXPECT(baseData.has_value(), "unskinned validated data exists");
  EXPECT(skinnedData.has_value(), "skinned validated data exists");
  if (baseData && skinnedData) {
    EXPECT(baseData->get().pipelineKey != skinnedData->get().pipelineKey,
           "variant difference should change pipeline key");
    EXPECT(hasBinding(skinnedData->get().descriptorResources, "Bones"),
           "skinned validated entry should include Bones resource");
  }
}

void testRenderQueueConsumesValidatedSceneNode() {
  auto node = SceneNode::create("node_queue", makeMeshWithSkinningInputs(),
                                makeMaterial(false), nullptr);
  auto scene = Scene::create("SceneQueue", node);
  RenderQueue queue;
  queue.buildFromScene(*scene, Pass_Forward, RenderTarget{});

  EXPECT(queue.getItems().size() == 1, "queue should consume one SceneNode");
  auto validated = node->getValidatedPassData(Pass_Forward);
  EXPECT(validated.has_value(), "validated entry should still exist");
  if (!queue.getItems().empty() && validated) {
    EXPECT(queue.getItems()[0].pipelineKey == validated->get().pipelineKey,
           "queue should reuse SceneNode validated pipeline key");
    EXPECT(queue.getItems()[0].descriptorResources.size() >=
               validated->get().descriptorResources.size(),
           "scene-level resources should be appended after validated resources");
  }
}

void testSceneAssignsStableDebugId() {
  auto node = SceneNode::create("node_debug", makeMeshWithSkinningInputs(),
                                makeMaterial(false), nullptr);
  EXPECT(node->getDebugId() == StringID{},
         "detached SceneNode should not have a scene debug id yet");
  auto scene = Scene::create("SceneDebug", node);
  (void)scene;
  EXPECT(node->getDebugId() == StringID("SceneDebug/node_debug"),
         "scene attachment should assign stable scene/node debug id");
}

int duplicateMode() {
  auto material = makeMaterial(false);
  auto nodeA =
      SceneNode::create("dup_node", makeMeshWithSkinningInputs(), material);
  auto nodeB =
      SceneNode::create("dup_node", makeMeshWithSkinningInputs(), material);
  auto scene = Scene::create("DuplicateScene", nodeA);
  scene->addRenderable(nodeB);
  return 0;
}

int invalidSkinningMode() {
  auto node = SceneNode::create("bad_skinning", makeMeshWithoutSkinningInputs(),
                                makeMaterial(true), makeSkeleton());
  (void)node;
  return 0;
}

int invalidVertexColorMode() {
  auto node = SceneNode::create(
      "bad_vertex_color", makeMeshPositionOnly(),
      makeMaterial({ShaderVariant{"USE_VERTEX_COLOR", true},
                    ShaderVariant{"USE_LIGHTING", false}}),
      nullptr);
  (void)node;
  return 0;
}

int invalidUvMode() {
  auto node = SceneNode::create(
      "bad_uv", makeMeshPositionOnly(),
      makeMaterial({ShaderVariant{"USE_UV", true},
                    ShaderVariant{"USE_LIGHTING", false}}),
      nullptr);
  (void)node;
  return 0;
}

int invalidLightingMode() {
  auto node = SceneNode::create("bad_lighting", makeMeshPositionOnly(),
                                makeMaterial(false), nullptr);
  (void)node;
  return 0;
}

int invalidNormalMapMode() {
  auto node = SceneNode::create(
      "bad_normal_map", makeMeshWithNormalAndUvOnly(),
      makeMaterial({ShaderVariant{"USE_UV", true},
                    ShaderVariant{"USE_LIGHTING", true},
                    ShaderVariant{"USE_NORMAL_MAP", true}}),
      nullptr);
  (void)node;
  return 0;
}

int invalidSkinningSkeletonMode() {
  auto node = SceneNode::create("bad_skinning_skeleton",
                                makeMeshWithSkinningInputs(), makeMaterial(true),
                                nullptr);
  (void)node;
  return 0;
}

void testFatalSubprocesses(const std::filesystem::path &self) {
  EXPECT(runSelf(self, "--duplicate") != 0,
         "duplicate node names must terminate in subprocess");
  EXPECT(runSelf(self, "--invalid-vertex-color") != 0,
         "missing vertex color input must terminate in subprocess");
  EXPECT(runSelf(self, "--invalid-uv") != 0,
         "missing uv input must terminate in subprocess");
  EXPECT(runSelf(self, "--invalid-lighting") != 0,
         "missing normal input must terminate in subprocess");
  EXPECT(runSelf(self, "--invalid-normal-map") != 0,
         "missing tangent input must terminate in subprocess");
  EXPECT(runSelf(self, "--invalid-skinning") != 0,
         "missing skinning vertex inputs must terminate in subprocess");
  EXPECT(runSelf(self, "--invalid-skinning-skeleton") != 0,
         "missing skeleton for skinned pass must terminate in subprocess");
}

} // namespace

int main(int argc, char **argv) {
  expSetEnvVK();
  if (argc > 1) {
    cdToWhereResourcesCouldFound("blinnphong_0");
    const std::string mode = argv[1];
    if (mode == "--duplicate")
      return duplicateMode();
    if (mode == "--invalid-vertex-color")
      return invalidVertexColorMode();
    if (mode == "--invalid-uv")
      return invalidUvMode();
    if (mode == "--invalid-lighting")
      return invalidLightingMode();
    if (mode == "--invalid-normal-map")
      return invalidNormalMapMode();
    if (mode == "--invalid-skinning")
      return invalidSkinningMode();
    if (mode == "--invalid-skinning-skeleton")
      return invalidSkinningSkeletonMode();
  }

  if (!cdToWhereResourcesCouldFound("blinnphong_0")) {
    std::cerr << "SKIP: failed to locate shader assets\n";
    return 0;
  }

  testIndependentSceneNodeValidation();
  testPassEnableStateRebuildsCache();
  testSharedMaterialPassChangesRevalidateAllSceneNodes();
  testSceneDestructionDetachesSceneNodesFromMaterialListener();
  testOrdinaryMaterialWritesDoNotChangeValidatedPassState();
  testOptionalSampledResourcesDoNotBlockValidation();
  testSkinningVariantChangesPipelineKeyAndAddsBones();
  testRenderQueueConsumesValidatedSceneNode();
  testSceneAssignsStableDebugId();
  testFatalSubprocesses(std::filesystem::absolute(argv[0]));

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all scene node validation tests passed\n";
  return 0;
}
