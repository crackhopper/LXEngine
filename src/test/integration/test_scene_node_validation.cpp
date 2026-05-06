#include "core/asset/mesh.hpp"
#include "core/asset/skeleton.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "scene_test_helpers.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
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
  BoundingBox bounds;
  for (const auto &vertex : vertices) {
    bounds.merge(vertex.pos);
  }
  auto vb = VertexBuffer<TVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create({0, 1, 2});
  return Mesh::create(vb, ib, bounds);
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
    if (std::filesystem::exists(p / "assets" / "materials"))
      return p / "assets" / "materials";
    auto parent = p.parent_path();
    if (parent == p)
      break;
    p = parent;
  }
  return std::filesystem::current_path() / "assets" / "materials";
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

bool commandExitedSuccessfully(int code) {
  return code == 0;
}

SceneNodeSharedPtr makeNode(const std::string &nodeName, MeshSharedPtr mesh,
                            MaterialInstanceSharedPtr material,
                            SkeletonSharedPtr skeleton = nullptr) {
  auto node = SceneNode::create(nodeName);
  node->addComponent<MeshComponent>(std::move(mesh));
  if (skeleton) {
    node->addComponent<SkeletonComponent>(std::move(skeleton));
  }
  node->addComponent<MaterialComponent>(std::move(material));
  return node;
}

bool nearlyEqualVec3(const Vec3f &a, const Vec3f &b) {
  return std::abs(a.x - b.x) < 1e-5f && std::abs(a.y - b.y) < 1e-5f &&
         std::abs(a.z - b.z) < 1e-5f;
}

bool nearlyEqualMat4(const Mat4f &a, const Mat4f &b) {
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      if (std::abs(a(row, col) - b(row, col)) >= 1e-5f) {
        return false;
      }
    }
  }
  return true;
}

Vec3f transformPoint(const Mat4f &transform, const Vec3f &point = Vec3f{}) {
  return (transform * Vec4f{point.x, point.y, point.z, 1.0f}).toVec3();
}

const PerDrawLayoutBase &readPerDrawLayout(const PerDrawDataSharedPtr &drawData) {
  return *reinterpret_cast<const PerDrawLayoutBase *>(drawData->rawData());
}

std::optional<std::reference_wrapper<const RenderingItem>>
findItemByDrawData(const RenderQueue &queue,
                   const PerDrawDataSharedPtr &drawData) {
  for (const auto &item : queue.getItems()) {
    if (item.drawData == drawData) {
      return std::cref(item);
    }
  }
  return std::nullopt;
}

void triggerDuplicateMeshComponentAssert() {
  auto node =
      makeNode("dup_component", makeMeshWithSkinningInputs(), makeMaterial(false));
  node->addComponent<MeshComponent>(makeMeshWithSkinningInputs());
}

void testComponentAttachRemoveAndListOrder() {
  auto node = SceneNode::create("node_components");
  auto mesh = makeMeshWithSkinningInputs();
  auto material = makeMaterial(false);
  auto skeleton = makeSkeleton();

  EXPECT(node->addComponent<MeshComponent>(mesh).has_value(),
         "mesh component should attach");
  EXPECT(node->addComponent<MaterialComponent>(material).has_value(),
         "material component should attach");
  EXPECT(node->addComponent<SkeletonComponent>(skeleton).has_value(),
         "skeleton component should attach");

  auto meshComponent = node->getComponent<MeshComponent>();
  auto materialComponent = node->getComponent<MaterialComponent>();
  auto skeletonComponent = node->getComponent<SkeletonComponent>();
  EXPECT(meshComponent.has_value(), "mesh component lookup should succeed");
  EXPECT(materialComponent.has_value(),
         "material component lookup should succeed");
  EXPECT(skeletonComponent.has_value(),
         "skeleton component lookup should succeed");
  if (meshComponent) {
    EXPECT(meshComponent->get().getMesh() == mesh,
           "mesh component should preserve mesh handle");
  }
  if (materialComponent) {
    EXPECT(materialComponent->get().getMaterialInstance() == material,
           "material component should preserve material handle");
  }
  if (skeletonComponent) {
    EXPECT(skeletonComponent->get().getSkeleton() == skeleton,
           "skeleton component should preserve skeleton handle");
  }

  auto components = node->listComponents();
  EXPECT(components.size() == 3, "listComponents should expose all components");
  if (components.size() == 3) {
    EXPECT(components[0].get().getTypeId() == componentTypeId<MeshComponent>(),
           "mesh component should keep insertion order");
    EXPECT(components[1].get().getTypeId() ==
               componentTypeId<MaterialComponent>(),
           "material component should keep insertion order");
    EXPECT(components[2].get().getTypeId() ==
               componentTypeId<SkeletonComponent>(),
           "skeleton component should keep insertion order");
  }

  EXPECT(node->removeComponent<MeshComponent>(),
         "removeComponent should remove mesh component");
  EXPECT(!node->getComponent<MeshComponent>().has_value(),
         "removed mesh component should no longer be visible");
  EXPECT(!node->removeComponent<MeshComponent>(),
         "removing absent component should return false");
}

void testRemovingMaterialComponentDetachesPassListener() {
  auto material = makeMaterial(false);
  auto node = makeNode("node_remove_material", makeMeshWithSkinningInputs(),
                       material);
  EXPECT(node->supportsPass(Pass_Forward),
         "node should start valid before removing material component");

  EXPECT(node->removeComponent<MaterialComponent>(),
         "material component should be removable");
  EXPECT(!node->getComponent<MaterialComponent>().has_value(),
         "material component should be gone after removal");
  EXPECT(!node->supportsPass(Pass_Forward),
         "node without material component should not support forward pass");

  material->setPassEnabled(Pass_Forward, false);
  EXPECT(!node->getValidatedPassData(Pass_Forward).has_value(),
         "removed material listener should leave cache empty after pass change");
}

void testDuplicateComponentTypeTriggersProgrammerError(const char *argv0) {
  const std::string command =
      std::string(argv0) + " --duplicate-component-assert >/dev/null 2>&1";
  const int exitCode = std::system(command.c_str());
  EXPECT(!commandExitedSuccessfully(exitCode),
         "duplicate component attachment should fail in child process");
}

void testIndependentSceneNodeValidation() {
  auto node =
      makeNode("node_base", makeMeshWithSkinningInputs(), makeMaterial(false));
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
  auto node = makeNode("node_toggle", makeMeshWithSkinningInputs(), material);
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
  auto nodeA =
      makeNode("node_shared_a", makeMeshWithSkinningInputs(), material);
  auto nodeB =
      makeNode("node_shared_b", makeMeshWithSkinningInputs(), material);
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
  auto node = makeNode("node_backref", makeMeshWithSkinningInputs(), material);
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
  auto node = makeNode("node_detach", makeMeshWithSkinningInputs(), material);

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
  auto parent =
      makeNode("node_parent", makeMeshWithSkinningInputs(), makeMaterial(false));
  auto child =
      makeNode("node_child", makeMeshWithSkinningInputs(), makeMaterial(false));

  parent->setTranslation(Vec3f{2.0f, 0.0f, -1.0f});
  child->setTranslation(Vec3f{0.0f, 3.0f, 4.0f});
  child->setParent(parent);

  EXPECT(nearlyEqualVec3(transformPoint(parent->getWorldTransform()),
                         Vec3f{2.0f, 0.0f, -1.0f}),
         "parent world transform should match parent local transform");
  EXPECT(nearlyEqualVec3(transformPoint(child->getWorldTransform()),
                         Vec3f{2.0f, 3.0f, 3.0f}),
         "child world transform should compose parent and local transforms");
}

void testHierarchyChangesDirtyChildPerDrawModel() {
  auto parent = makeNode("node_parent_dirty", makeMeshWithSkinningInputs(),
                         makeMaterial(false));
  auto child = makeNode("node_child_dirty", makeMeshWithSkinningInputs(),
                        makeMaterial(false));
  child->setParent(parent);
  child->setTranslation(Vec3f{0.0f, 1.0f, 0.0f});

  parent->setTranslation(Vec3f{1.0f, 0.0f, 0.0f});
  const auto &before = readPerDrawLayout(child->getPerDrawData());
  EXPECT(nearlyEqualVec3(transformPoint(before.model), Vec3f{1.0f, 1.0f, 0.0f}),
         "child per-draw model should reflect initial parent transform");

  parent->setTranslation(Vec3f{5.0f, -2.0f, 0.0f});
  const auto &after = readPerDrawLayout(child->getPerDrawData());
  EXPECT(nearlyEqualVec3(transformPoint(after.model), Vec3f{5.0f, -1.0f, 0.0f}),
         "changing parent transform should dirty and refresh child per-draw model");
}

void testParentedCameraFollowsHierarchyTranslation() {
  auto parent = SceneNode::create("camera_parent");
  parent->setTranslation(Vec3f{1.0f, 2.0f, 3.0f});

  auto cameraNode = SceneNode::create("camera_child");
  cameraNode->setParent(parent);

  auto camera = cameraNode->addComponent<CameraComponent>();
  EXPECT(camera.has_value(), "camera component should attach to node");
  if (!camera.has_value()) {
    return;
  }

  camera->get().lookAt(Vec3f{5.0f, 6.0f, 7.0f}, Vec3f{5.0f, 6.0f, 6.0f},
                       Vec3f{0.0f, 1.0f, 0.0f});

  const Vec3f eyeBefore = camera->get().getEyePosition();
  const Vec3f targetBefore = camera->get().getLookTarget();

  parent->setTranslation(Vec3f{4.0f, -1.0f, 8.0f});

  const Vec3f expectedDelta{3.0f, -3.0f, 5.0f};
  EXPECT(nearlyEqualVec3(camera->get().getEyePosition(), eyeBefore + expectedDelta),
         "camera eye should follow parent world translation");
  EXPECT(nearlyEqualVec3(camera->get().getLookTarget(), targetBefore + expectedDelta),
         "camera look target should follow parent world translation");
}

void testCameraScaleDoesNotAffectViewMatrix() {
  auto cameraNode = SceneNode::create("scaled_camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  EXPECT(camera.has_value(), "camera component should attach to node");
  if (!camera.has_value()) {
    return;
  }

  camera->get().lookAt(Vec3f{3.0f, 4.0f, 5.0f}, Vec3f{0.0f, 1.0f, 0.0f},
                       Vec3f{0.0f, 1.0f, 0.0f});
  const Mat4f baselineView = camera->get().getViewMatrix();

  auto local = cameraNode->getLocalTransform();
  local.scale = Vec3f{2.0f, 3.0f, 4.0f};
  cameraNode->setLocalTransform(local);

  EXPECT(nearlyEqualMat4(camera->get().getViewMatrix(), baselineView),
         "camera view matrix should ignore owner scale");
}

void testOrdinaryMaterialWritesDoNotChangeValidatedPassState() {
  auto material = makeMaterial(false);
  auto nodeA =
      makeNode("node_non_structural_a", makeMeshWithSkinningInputs(), material);
  auto nodeB =
      makeNode("node_non_structural_b", makeMeshWithSkinningInputs(), material);
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
  auto node = makeNode("node_optional_textures", makeMeshWithUvOnly(), material);

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
  auto baseNode = makeNode("node_unskinned", mesh, makeMaterial(false));
  auto skinnedNode =
      makeNode("node_skinned", mesh, makeMaterial(true), makeSkeleton());

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
  auto node =
      makeNode("node_queue", makeMeshWithSkinningInputs(), makeMaterial(false));
  auto scene = Scene::create("SceneQueue", node);
  scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());
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
  auto parent = makeNode("node_queue_parent", makeMeshWithSkinningInputs(),
                         makeMaterial(false));
  auto child = makeNode("node_queue_child", makeMeshWithSkinningInputs(),
                        makeMaterial(false));
  parent->setTranslation(Vec3f{4.0f, 0.5f, 0.0f});
  child->setTranslation(Vec3f{0.0f, 1.5f, 0.0f});
  child->setParent(parent);

  auto scene = Scene::create("SceneQueueHierarchy", parent);
  scene->addRenderable(child);
  scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());

  RenderQueue queue;
  queue.buildFromScene(*scene, Pass_Forward, RenderTarget{});

  EXPECT(queue.getItems().size() == 2,
         "queue should include both parent and child renderables");
  auto childItem = findItemByDrawData(queue, child->getPerDrawData());
  EXPECT(childItem.has_value(),
         "queue should carry the child per-draw data pointer");
  if (childItem) {
    const auto &layout = readPerDrawLayout(childItem->get().drawData);
    EXPECT(nearlyEqualVec3(transformPoint(layout.model), Vec3f{4.0f, 2.0f, 0.0f}),
           "queue draw data should use hierarchy-derived child world transform");
  }
}

void testSceneAssignsStableDebugId() {
  auto node =
      makeNode("node_debug", makeMeshWithSkinningInputs(), makeMaterial(false));
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
    auto nodeA = makeNode("dup_node", makeMeshWithSkinningInputs(), material);
    auto nodeB = makeNode("dup_node", makeMeshWithSkinningInputs(), material);
    auto scene = Scene::create("DuplicateScene", nodeA);
    scene->addRenderable(nodeB);
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "duplicate node names must throw logic_error");

  threw = false;
  try {
    auto node = makeNode("bad_vertex_color", makeMeshPositionOnly(),
                         makeMaterial({ShaderVariant{"USE_VERTEX_COLOR", true},
                                       ShaderVariant{"USE_LIGHTING", false}}));
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing vertex color input must throw logic_error");

  threw = false;
  try {
    auto node = makeNode("bad_uv", makeMeshPositionOnly(),
                         makeMaterial({ShaderVariant{"USE_UV", true},
                                       ShaderVariant{"USE_LIGHTING", false}}));
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing uv input must throw logic_error");

  threw = false;
  try {
    auto node = makeNode("bad_lighting", makeMeshPositionOnly(),
                         makeMaterial(false));
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing normal input must throw logic_error");

  threw = false;
  try {
    auto node = makeNode(
        "bad_normal_map", makeMeshWithNormalAndUvOnly(),
        makeMaterial({ShaderVariant{"USE_UV", true},
                      ShaderVariant{"USE_LIGHTING", true},
                      ShaderVariant{"USE_NORMAL_MAP", true}}));
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing tangent input must throw logic_error");

  threw = false;
  try {
    auto node = makeNode("bad_skinning", makeMeshWithoutSkinningInputs(),
                         makeMaterial(true), makeSkeleton());
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing skinning vertex inputs must throw logic_error");

  threw = false;
  try {
    auto node = makeNode("bad_skinning_skeleton", makeMeshWithSkinningInputs(),
                         makeMaterial(true));
    (void)node;
  } catch (const std::logic_error &) {
    threw = true;
  }
  EXPECT(threw, "missing skeleton for skinned pass must throw logic_error");
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--duplicate-component-assert") {
    triggerDuplicateMeshComponentAssert();
    return 0;
  }

  expSetEnvVK();
  if (!initializeRuntimeAssetRoot()) {
    std::cerr << "SKIP: failed to locate shader assets\n";
    return 0;
  }

  testComponentAttachRemoveAndListOrder();
  testRemovingMaterialComponentDetachesPassListener();
  testDuplicateComponentTypeTriggersProgrammerError(argv[0]);
  testIndependentSceneNodeValidation();
  testPassEnableStateRebuildsCache();
  testSharedMaterialPassChangesRevalidateAllSceneNodes();
  testSceneNodeBackrefUsesWeakOwnershipContract();
  testSceneDestructionDetachesSceneNodesFromMaterialListener();
  testSceneNodeHierarchyPropagatesWorldTransform();
  testHierarchyChangesDirtyChildPerDrawModel();
  testParentedCameraFollowsHierarchyTranslation();
  testCameraScaleDoesNotAffectViewMatrix();
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
