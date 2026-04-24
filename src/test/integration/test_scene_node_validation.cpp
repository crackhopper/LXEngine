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

bool nearlyEqualVec3(const Vec3f &a, const Vec3f &b) {
  return std::abs(a.x - b.x) < 1e-5f && std::abs(a.y - b.y) < 1e-5f &&
         std::abs(a.z - b.z) < 1e-5f;
}

Vec3f transformPoint(const Mat4f &transform, const Vec3f &point = Vec3f{}) {
  return (transform * Vec4f{point.x, point.y, point.z, 1.0f}).toVec3();
}

const PerDrawLayoutBase &readPerDrawLayout(const PerDrawDataSharedPtr &drawData) {
  return *reinterpret_cast<const PerDrawLayoutBase *>(drawData->rawData());
}

const RenderingItem *findItemByDrawData(const RenderQueue &queue,
                                        const PerDrawDataSharedPtr &drawData) {
  for (const auto &item : queue.getItems()) {
    if (item.drawData == drawData) {
      return &item;
    }
  }
  return nullptr;
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

void testSceneNodeBackrefUsesWeakOwnershipContract() {
  auto material = makeMaterial(false);
  auto node = SceneNode::create("node_backref", makeMeshWithSkinningInputs(),
                                material, nullptr);
  EXPECT(!node->getAttachedScene(),
         "fresh node should not report an attached scene");

  auto scene = Scene::create("BackrefScene", node);
  auto attachedScene = node->getAttachedScene();
  EXPECT(attachedScene != nullptr,
         "adding node to scene should establish back-reference");
  if (attachedScene) {
    EXPECT(attachedScene->getSceneName() == "BackrefScene",
           "back-reference should lock the owning scene");
  }
}

void testSceneDestructionDetachesSceneNodesFromMaterialListener() {
  auto material = makeMaterial(false);
  auto node = SceneNode::create("node_detach", makeMeshWithSkinningInputs(),
                                material, nullptr);

  {
    auto scene = Scene::create("TemporaryScene", node);
    EXPECT(node->supportsPass(Pass_Forward), "scene-owned node starts validated");
    EXPECT(node->getAttachedScene() != nullptr,
           "scene-owned node reports attached scene before destruction");
  }

  EXPECT(!node->getAttachedScene(),
         "scene destruction should clear weak back-reference");

  material->setPassEnabled(Pass_Forward, false);
  EXPECT(!node->supportsPass(Pass_Forward),
         "detached node should rebuild locally after scene destruction");
  EXPECT(!node->getValidatedPassData(Pass_Forward).has_value(),
         "detached node should clear disabled pass after scene destruction");

  material->setPassEnabled(Pass_Forward, true);
  EXPECT(node->supportsPass(Pass_Forward),
         "detached node should revalidate locally after scene destruction");
}

void testSceneNodeHierarchyPropagatesWorldTransform() {
  auto parent = SceneNode::create("node_parent", makeMeshWithSkinningInputs(),
                                  makeMaterial(false), nullptr);
  auto child = SceneNode::create("node_child", makeMeshWithSkinningInputs(),
                                 makeMaterial(false), nullptr);

  parent->setLocalTransform(Mat4f::translate(Vec3f{2.0f, 0.0f, -1.0f}));
  child->setLocalTransform(Mat4f::translate(Vec3f{0.0f, 3.0f, 4.0f}));
  child->setParent(parent);

  EXPECT(nearlyEqualVec3(transformPoint(parent->getWorldTransform()),
                         Vec3f{2.0f, 0.0f, -1.0f}),
         "parent world transform should match parent local transform");
  EXPECT(nearlyEqualVec3(transformPoint(child->getWorldTransform()),
                         Vec3f{2.0f, 3.0f, 3.0f}),
         "child world transform should compose parent and local transforms");
}

void testHierarchyChangesDirtyChildPerDrawModel() {
  auto parent = SceneNode::create("node_parent_dirty",
                                  makeMeshWithSkinningInputs(),
                                  makeMaterial(false), nullptr);
  auto child = SceneNode::create("node_child_dirty", makeMeshWithSkinningInputs(),
                                 makeMaterial(false), nullptr);
  child->setParent(parent);
  child->setLocalTransform(Mat4f::translate(Vec3f{0.0f, 1.0f, 0.0f}));

  parent->setLocalTransform(Mat4f::translate(Vec3f{1.0f, 0.0f, 0.0f}));
  const auto &before = readPerDrawLayout(child->getPerDrawData());
  EXPECT(nearlyEqualVec3(transformPoint(before.model), Vec3f{1.0f, 1.0f, 0.0f}),
         "child per-draw model should reflect initial parent transform");

  parent->setLocalTransform(Mat4f::translate(Vec3f{5.0f, -2.0f, 0.0f}));
  const auto &after = readPerDrawLayout(child->getPerDrawData());
  EXPECT(nearlyEqualVec3(transformPoint(after.model), Vec3f{5.0f, -1.0f, 0.0f}),
         "changing parent transform should dirty and refresh child per-draw model");
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

void testRenderQueueUsesHierarchyDerivedWorldTransform() {
  auto parent = SceneNode::create("node_queue_parent",
                                  makeMeshWithSkinningInputs(),
                                  makeMaterial(false), nullptr);
  auto child = SceneNode::create("node_queue_child", makeMeshWithSkinningInputs(),
                                 makeMaterial(false), nullptr);
  parent->setLocalTransform(Mat4f::translate(Vec3f{4.0f, 0.5f, 0.0f}));
  child->setLocalTransform(Mat4f::translate(Vec3f{0.0f, 1.5f, 0.0f}));
  child->setParent(parent);

  auto scene = Scene::create("SceneQueueHierarchy", parent);
  scene->addRenderable(child);

  RenderQueue queue;
  queue.buildFromScene(*scene, Pass_Forward, RenderTarget{});

  EXPECT(queue.getItems().size() == 2,
         "queue should include both parent and child renderables");
  const auto *childItem = findItemByDrawData(queue, child->getPerDrawData());
  EXPECT(childItem != nullptr,
         "queue should carry the child per-draw data pointer");
  if (childItem) {
    const auto &layout = readPerDrawLayout(childItem->drawData);
    EXPECT(nearlyEqualVec3(transformPoint(layout.model), Vec3f{4.0f, 2.0f, 0.0f}),
           "queue draw data should use hierarchy-derived child world transform");
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

void testProgrammerErrorsThrowLogicError() {
  bool threw = false;
  try {
    auto material = makeMaterial(false);
    auto nodeA =
        SceneNode::create("dup_node", makeMeshWithSkinningInputs(), material);
    auto nodeB =
        SceneNode::create("dup_node", makeMeshWithSkinningInputs(), material);
    auto scene = Scene::create("DuplicateScene", nodeA);
    scene->addRenderable(nodeB);
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "duplicate node names must throw logic_error");

  threw = false;
  try {
    auto node = SceneNode::create(
        "bad_vertex_color", makeMeshPositionOnly(),
        makeMaterial({ShaderVariant{"USE_VERTEX_COLOR", true},
                      ShaderVariant{"USE_LIGHTING", false}}),
        nullptr);
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing vertex color input must throw logic_error");

  threw = false;
  try {
    auto node = SceneNode::create(
        "bad_uv", makeMeshPositionOnly(),
        makeMaterial({ShaderVariant{"USE_UV", true},
                      ShaderVariant{"USE_LIGHTING", false}}),
        nullptr);
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing uv input must throw logic_error");

  threw = false;
  try {
    auto node = SceneNode::create("bad_lighting", makeMeshPositionOnly(),
                                  makeMaterial(false), nullptr);
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing normal input must throw logic_error");

  threw = false;
  try {
    auto node = SceneNode::create(
        "bad_normal_map", makeMeshWithNormalAndUvOnly(),
        makeMaterial({ShaderVariant{"USE_UV", true},
                      ShaderVariant{"USE_LIGHTING", true},
                      ShaderVariant{"USE_NORMAL_MAP", true}}),
        nullptr);
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing tangent input must throw logic_error");

  threw = false;
  try {
    auto node = SceneNode::create("bad_skinning",
                                  makeMeshWithoutSkinningInputs(),
                                  makeMaterial(true), makeSkeleton());
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing skinning vertex inputs must throw logic_error");

  threw = false;
  try {
    auto node = SceneNode::create("bad_skinning_skeleton",
                                  makeMeshWithSkinningInputs(),
                                  makeMaterial(true), nullptr);
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing skeleton for skinned pass must throw logic_error");
}

} // namespace

int main(int argc, char **argv) {
  expSetEnvVK();
  if (!initializeRuntimeAssetRoot()) {
    std::cerr << "SKIP: failed to locate shader assets\n";
    return 0;
  }

  testIndependentSceneNodeValidation();
  testPassEnableStateRebuildsCache();
  testSharedMaterialPassChangesRevalidateAllSceneNodes();
  testSceneNodeBackrefUsesWeakOwnershipContract();
  testSceneDestructionDetachesSceneNodesFromMaterialListener();
  testSceneNodeHierarchyPropagatesWorldTransform();
  testHierarchyChangesDirtyChildPerDrawModel();
  testOrdinaryMaterialWritesDoNotChangeValidatedPassState();
  testOptionalSampledResourcesDoNotBlockValidation();
  testSkinningVariantChangesPipelineKeyAndAddsBones();
  testRenderQueueConsumesValidatedSceneNode();
  testRenderQueueUsesHierarchyDerivedWorldTransform();
  testSceneAssignsStableDebugId();
  testProgrammerErrorsThrowLogicError();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all scene node validation tests passed\n";
  return 0;
}
