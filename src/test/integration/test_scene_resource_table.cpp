#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/scene_gpu_records.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <type_traits>

using namespace LX_core;

namespace {

static_assert(
    !std::is_invocable_r_v<MaterialHandle,
                           decltype(&SceneResourceTable::registerMaterial),
                           SceneResourceTable *, MaterialInstanceSharedPtr>,
    "SceneResourceTable must not accept shared_ptr material "
    "registration; the table is the unique owner.");
static_assert(!std::is_invocable_r_v<
                  MeshHandle, decltype(&SceneResourceTable::registerMesh),
                  SceneResourceTable *, MeshBufferSharedPtr>,
              "SceneResourceTable must not accept shared_ptr mesh "
              "registration; the table is the unique owner.");
static_assert(!std::is_invocable_r_v<
                  TextureHandle, decltype(&SceneResourceTable::registerTexture),
                  SceneResourceTable *, CombinedTextureSamplerSharedPtr>,
              "SceneResourceTable must not accept shared_ptr texture "
              "registration; the table is the unique owner.");
static_assert(!std::is_invocable_r_v<
                  LightHandle, decltype(&SceneResourceTable::registerLight),
                  SceneResourceTable *, LightBaseSharedPtr>,
              "SceneResourceTable must not accept shared_ptr light "
              "registration; the table is the unique owner.");
static_assert(!std::is_invocable_r_v<
                  SkeletonHandle,
                  decltype(&SceneResourceTable::registerSkeleton),
                  SceneResourceTable *, SkeletonSharedPtr>,
              "SceneResourceTable must not accept shared_ptr skeleton "
              "registration; the table is the unique owner.");

int s_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__      \
                << ")\n";                                                      \
      ++s_failures;                                                            \
    }                                                                          \
  } while (0)

struct TestVertex final {
  Vec3f pos{};

  static const VertexLayout &getLayout() {
    static const VertexLayout layout{
        {{"inPos", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(TestVertex, pos)}},
        sizeof(TestVertex)};
    return layout;
  }
};

struct TangentVertex final {
  Vec3f pos{};
  Vec3f normal{};
  Vec2f uv{};
  Vec4f tangent{};

  static const VertexLayout &getLayout() {
    static const VertexLayout layout{
        {{"inPos", 0, DataType::Float3, sizeof(TangentVertex),
          offsetof(TangentVertex, pos)},
         {"inNormal", 1, DataType::Float3, sizeof(TangentVertex),
          offsetof(TangentVertex, normal)},
         {"inUV", 2, DataType::Float2, sizeof(TangentVertex),
          offsetof(TangentVertex, uv)},
         {"inTangent", 3, DataType::Float4, sizeof(TangentVertex),
          offsetof(TangentVertex, tangent)}},
        sizeof(TangentVertex)};
    return layout;
  }
};

class FakeShader final : public IShader {
public:
  explicit FakeShader(std::vector<ShaderResourceBinding> bindings)
      : m_bindings(std::move(bindings)) {}

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }

  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const auto &item : m_bindings) {
      if (item.set == set && item.binding == binding) {
        return std::cref(item);
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const auto &item : m_bindings) {
      if (item.name == name) {
        return std::cref(item);
      }
    }
    return std::nullopt;
  }

  usize getProgramHash() const override { return 0; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

MeshBufferSharedPtr makeMeshBuffer() {
  auto vertices = std::vector<TestVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 2};
  auto vb = VertexBuffer<TestVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  return MeshBuffer::create(
      vb, ib, BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MeshBufferSharedPtr makeTwoTriangleMeshBuffer() {
  auto vertices = std::vector<TestVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
      {{1.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 2, 2, 1, 3};
  auto vb = VertexBuffer<TestVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  return MeshBuffer::create(
      vb, ib, BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MeshBufferSharedPtr makeOffsetMeshBuffer() {
  auto vertices = std::vector<TestVertex>{
      {{-10.0f, -10.0f, 0.0f}},
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{1, 2, 3};
  auto vb = VertexBuffer<TestVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  auto storage = GeometryStorage::create(vb, ib);
  return MeshBuffer::create(
      storage, 1, 0, 3, 3, BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MeshBufferSharedPtr makeInvalidIndexRangeMeshBuffer() {
  auto vertices = std::vector<TestVertex>{
      {{-10.0f, -10.0f, 0.0f}},
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 2, 3};
  auto vb = VertexBuffer<TestVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  auto storage = GeometryStorage::create(vb, ib);
  return MeshBuffer::create(
      storage, 1, 0, 3, 3, BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MeshBufferSharedPtr makeLineListMeshBufferWithTriangleSizedIndexCount() {
  auto vertices = std::vector<TestVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
      {{1.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 1, 2, 2, 3};
  auto vb = VertexBuffer<TestVertex>::create(std::move(vertices));
  auto ib =
      IndexBuffer::create(std::move(indices), PrimitiveTopology::LineList);
  return MeshBuffer::create(
      vb, ib, BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MeshBufferSharedPtr makeTangentSignMeshBuffer() {
  auto vertices = std::vector<TangentVertex>{
      {{0.0f, 0.0f, 0.0f},
       {0.0f, 0.0f, 1.0f},
       {0.25f, 0.75f},
       {1.0f, 0.0f, 0.0f, -1.0f}},
      {{1.0f, 0.0f, 0.0f},
       {0.0f, 0.0f, 1.0f},
       {0.5f, 0.75f},
       {1.0f, 0.0f, 0.0f, -1.0f}},
      {{0.0f, 1.0f, 0.0f},
       {0.0f, 0.0f, 1.0f},
       {0.25f, 1.0f},
       {1.0f, 0.0f, 0.0f, -1.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 2};
  auto vb = VertexBuffer<TangentVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  return MeshBuffer::create(
      vb, ib, BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MaterialInstanceSharedPtr makeGpuRecordMaterial(const Vec4f &baseColor = Vec4f{
                                                    0.25f, 0.5f, 0.75f, 0.9f}) {
  ShaderResourceBinding binding;
  binding.name = "MaterialUBO";
  binding.set = 2;
  binding.binding = 0;
  binding.type = ShaderPropertyType::UniformBuffer;
  binding.size = 32;
  binding.members = {
      {"baseColor", ShaderPropertyType::Vec4, 0, 16},
      {"roughnessFactor", ShaderPropertyType::Float, 16, 4},
  };

  auto shader =
      std::make_shared<FakeShader>(std::vector<ShaderResourceBinding>{binding});
  auto materialTemplate = MaterialTemplate::create("scene_gpu_records");
  ShaderProgramSet shaderSet;
  shaderSet.shaderName = "scene_gpu_records";
  shaderSet.shader = shader;
  MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = std::move(shaderSet);
  passDefinition.renderState = RenderState{};
  materialTemplate->setPassDefinition(Pass_Forward, std::move(passDefinition));
  materialTemplate->rebuildMaterialInterface();

  auto material = MaterialInstance::create(materialTemplate);
  material->setParameter(StringID("MaterialUBO"), StringID("baseColor"),
                         baseColor);
  material->setParameter(StringID("MaterialUBO"), StringID("roughnessFactor"),
                         0.35f);
  return material;
}

MeshBuffer::UniquePtr uniqueMesh(const MeshBufferSharedPtr &mesh) {
  return mesh->cloneUnique();
}

MaterialInstance::UniquePtr
uniqueMaterial(const MaterialInstanceSharedPtr &material) {
  return material->cloneInstanceDataUnique();
}

SkeletonSharedPtr makeSkeleton() {
  return Skeleton::create({
      Bone{"root", -1, Vec3f{0.0f, 0.0f, 0.0f}, Quatf{}},
      Bone{"tip", 0, Vec3f{0.0f, 1.0f, 0.0f}, Quatf{}},
  });
}

void testGeometryStorageAndMeshBufferContract() {
  auto mesh = makeMeshBuffer();
  EXPECT(mesh->getGeometryStorage() != nullptr,
         "MeshBuffer should reference GeometryStorage");
  EXPECT(mesh->getVertexBuffer().getVertexCount() == 3,
         "MeshBuffer should expose storage vertex count");
  EXPECT(mesh->getIndexBuffer().indexCount() == 3,
         "MeshBuffer should expose storage index count");
  EXPECT(mesh->getVertexOffset() == 0, "default vertex offset should be zero");
  EXPECT(mesh->getIndexOffset() == 0, "default index offset should be zero");
  EXPECT(mesh->getBounds().isValid(), "MeshBuffer should carry bounds");
}

void testHandleGenerationInvalidatesStaleMeshHandle() {
  SceneResourceTable table;
  auto first = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  EXPECT(table.isAlive(first), "registered mesh handle should be alive");
  EXPECT(table.resolve(first).has_value(),
         "registered mesh handle should resolve");
  if (const auto mesh = table.resolve(first)) {
    EXPECT(!mesh->get().getGeometryStorage(),
           "table-owned mesh should not retain pending shared geometry");
    EXPECT(mesh->get().getGeometryStorageHandle().isValid(),
           "table-owned mesh should reference table-owned geometry by handle");
    EXPECT(table.resolve(mesh->get().getGeometryStorageHandle()).has_value(),
           "table-owned mesh geometry handle should resolve");
  }

  table.release(first);
  EXPECT(!table.isAlive(first), "released mesh handle should not be alive");
  EXPECT(!table.resolve(first).has_value(),
         "released mesh handle should not resolve");

  auto second = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  EXPECT(second.index == first.index, "table may reuse released slot");
  EXPECT(second.generation != first.generation,
         "reused slot should get a new generation");
  EXPECT(table.isAlive(second), "new mesh handle should be alive");
  EXPECT(!table.isAlive(first), "stale mesh handle should remain invalid");
}

void testSceneRegistersRenderableComponentResources() {
  auto mesh = makeMeshBuffer();
  auto material = MaterialInstance::create(
      MaterialTemplate::create("scene_resource_table"));
  auto node = SceneNode::create("resource_table_node");
  node->addComponent<MeshComponent>(mesh);
  node->addComponent<MaterialComponent>(material);
  node->addComponent<SkeletonComponent>(makeSkeleton());

  auto scene = Scene::create("resource_table_scene", node);

  const auto meshComponent = node->getComponent<MeshComponent>();
  const auto materialComponent = node->getComponent<MaterialComponent>();
  EXPECT(meshComponent.has_value(),
         "node should keep the registered mesh component");
  EXPECT(materialComponent.has_value(),
         "node should keep the registered material component");
  const auto skeletonComponent = node->getComponent<SkeletonComponent>();
  EXPECT(skeletonComponent.has_value(),
         "node should keep the registered skeleton component");
  EXPECT(dynamic_cast<IRenderableComponent *>(&meshComponent->get()) != nullptr,
         "mesh component should expose renderable component capability");

  const MeshHandle meshHandle = meshComponent->get().getMeshHandle();
  const ObjectHandle objectHandle = meshComponent->get().getObjectHandle();
  const GeometryStorageHandle geometryHandle =
      meshComponent->get().getGeometryStorageHandle();
  const MaterialHandle materialHandle =
      materialComponent->get().getMaterialHandle();
  const SkeletonHandle skeletonHandle =
      skeletonComponent ? skeletonComponent->get().getSkeletonHandle()
                        : SkeletonHandle{};
  EXPECT(!meshComponent->get().getPendingMesh(),
         "registered mesh component should clear pending shared mesh");
  EXPECT(!materialComponent->get().getPendingMaterialInstance(),
         "registered material component should clear pending shared material");
  EXPECT(skeletonComponent && !skeletonComponent->get().getPendingSkeleton(),
         "registered skeleton component should clear pending shared skeleton");
  EXPECT(geometryHandle.isValid(),
         "mesh component should receive a geometry storage handle");
  EXPECT(meshHandle.isValid(), "mesh component should receive a mesh handle");
  EXPECT(objectHandle.isValid(),
         "mesh component should receive an object handle");
  EXPECT(materialHandle.isValid(),
         "material component should receive a material handle");
  EXPECT(skeletonHandle.isValid(),
         "skeleton component should receive a skeleton handle");
  EXPECT(scene->resources().geometryStorageCount() == 1,
         "scene resource table should own one geometry storage entry");
  EXPECT(scene->resources().meshCount() == 1,
         "scene resource table should own one mesh entry");
  EXPECT(scene->resources().materialCount() == 1,
         "scene resource table should own one material entry");
  EXPECT(scene->resources().objectCount() == 1,
         "scene resource table should own one object entry");
  EXPECT(scene->resources().skeletonCount() == 1,
         "scene resource table should own one skeleton entry");
  EXPECT(scene->resources().resolve(meshHandle).has_value(),
         "mesh handle should resolve through scene resource table");
  EXPECT(scene->resources().resolve(objectHandle).has_value(),
         "object handle should resolve through scene resource table");
  EXPECT(scene->resources().resolve(geometryHandle).has_value(),
         "geometry storage handle should resolve through scene resource table");
  EXPECT(scene->resources().resolve(materialHandle).has_value(),
         "material handle should resolve through scene resource table");
  EXPECT(scene->resources().resolve(skeletonHandle).has_value(),
         "skeleton handle should resolve through scene resource table");
  if (const auto storage = scene->resources().resolve(geometryHandle)) {
    const auto vertexRef = node->getVertexBuffer();
    const auto indexRef = node->getIndexBuffer();
    EXPECT(vertexRef.isValid() &&
               &vertexRef.get() == &storage->get().getVertexBuffer(),
           "node vertex resource should reference table-owned geometry");
    EXPECT(indexRef.isValid() &&
               &indexRef.get() == &storage->get().getIndexBuffer(),
           "node index resource should reference table-owned geometry");
  }

  const auto snapshot = scene->resources().buildSnapshot();
  EXPECT(snapshot.geometryStorageHandles.size() == 1,
         "snapshot should include geometry storage handles");
  EXPECT(snapshot.meshHandles.size() == 1,
         "snapshot should include mesh handles");
  EXPECT(snapshot.materialHandles.size() == 1,
         "snapshot should include material handles");
  EXPECT(snapshot.skeletonHandles.size() == 1,
         "snapshot should include skeleton handles");
  EXPECT(snapshot.objects.size() == 1,
         "snapshot should include one object view");
  if (!snapshot.objects.empty()) {
    const auto &objectView = snapshot.objects.front();
    EXPECT(objectView.meshIndex == meshHandle.index,
           "object view should reference mesh handle index");
    EXPECT(objectView.materialIndex == materialHandle.index,
           "object view should reference material handle index");
    EXPECT(objectView.visible, "object view should be visible by default");
    EXPECT(objectView.worldBounds.isValid(),
           "object view should include world bounds");
  }

  node->setTranslation({2.0f, 0.0f, 0.0f});
  const auto syncedSnapshot = scene->resources().buildSnapshot();
  EXPECT(syncedSnapshot.objects.size() == 1,
         "scene snapshot should keep the object after transform sync");
  if (!syncedSnapshot.objects.empty()) {
    EXPECT(syncedSnapshot.objects.front().worldBounds.getCenter().x > 1.5f,
           "scene snapshot should refresh object world bounds");
  }
  const auto uploadView = scene->resources().buildUploadView();
  EXPECT(uploadView.objects.size() == 1,
         "scene upload view should keep the object after transform sync");
  if (!uploadView.objects.empty()) {
    EXPECT(uploadView.objects.front().objectToWorld[3].x == 2.0f &&
               uploadView.objects.front().objectToWorld[3].y == 0.0f &&
               uploadView.objects.front().objectToWorld[3].z == 0.0f &&
               uploadView.objects.front().objectToWorld[3].w == 1.0f,
           "scene upload objectToWorld should place translation in column 3");
    EXPECT(uploadView.objects.front().worldToObject[3].x == -2.0f &&
               uploadView.objects.front().worldToObject[3].y == 0.0f &&
               uploadView.objects.front().worldToObject[3].z == 0.0f &&
               uploadView.objects.front().worldToObject[3].w == 1.0f,
           "scene upload worldToObject should carry inverse translation");
  }

  scene->removeRenderable(node);
  EXPECT(!meshComponent->get().getGeometryStorageHandle().isValid(),
         "removed node should clear geometry storage handle");
  EXPECT(!meshComponent->get().getMeshHandle().isValid(),
         "removed node should clear mesh handle");
  EXPECT(!meshComponent->get().getObjectHandle().isValid(),
         "removed node should clear object handle");
  EXPECT(!materialComponent->get().getMaterialHandle().isValid(),
         "removed node should clear material handle");
  if (skeletonComponent) {
    EXPECT(!skeletonComponent->get().getSkeletonHandle().isValid(),
           "removed node should clear skeleton handle");
  }
  EXPECT(scene->resources().geometryStorageCount() == 0,
         "removed node should release geometry storage entry");
  EXPECT(scene->resources().meshCount() == 0,
         "removed node should release mesh entry");
  EXPECT(scene->resources().materialCount() == 0,
         "removed node should release material entry");
  EXPECT(scene->resources().objectCount() == 0,
         "removed node should release object entry");
  EXPECT(scene->resources().skeletonCount() == 0,
         "removed node should release skeleton entry");
}

void testSceneRegistersCameraAndLightResources() {
  auto scene = Scene::create("camera_light_resource_table");
  auto cameraNode = SceneNode::create("resource_camera");
  cameraNode->addComponent<CameraComponent>();
  auto cameraBeforeRegister = cameraNode->getComponent<CameraComponent>();
  EXPECT(cameraBeforeRegister.has_value(),
         "camera node should expose camera component before registration");
  if (cameraBeforeRegister.has_value()) {
    cameraBeforeRegister->get().applyProjectionState(CameraType::Orthographic,
                                                     50.0f, 1.25f, 0.2f, 120.0f,
                                                     -5.0f, 5.0f, -4.0f, 4.0f);
  }

  scene->addCamera(cameraNode);

  const auto cameraComponent = cameraNode->getComponent<CameraComponent>();
  EXPECT(cameraComponent.has_value(),
         "camera node should keep camera component");
  const CameraHandle cameraHandle = cameraComponent->get().getCameraHandle();
  EXPECT(cameraHandle.isValid(),
         "camera component should receive a camera handle");
  const auto cameraHandles = scene->getCameraHandles();
  EXPECT(cameraHandles.size() == 1,
         "scene should expose camera handles instead of owning camera data");
  EXPECT(cameraHandles.front() == cameraHandle,
         "scene camera handle list should preserve registered camera handle");
  EXPECT(scene->resources().cameraCount() == 1,
         "scene resource table should own one camera entry");
  EXPECT(scene->resources().resolve(cameraHandle).has_value(),
         "camera handle should resolve through scene resource table");
  if (const auto cameraResource = scene->resources().resolve(cameraHandle);
      cameraResource.has_value()) {
    EXPECT(cameraResource->get().projection.type == CameraType::Orthographic,
           "camera resource should preserve projection type");
    EXPECT(cameraResource->get().projection.left == -5.0f &&
               cameraResource->get().projection.right == 5.0f &&
               cameraResource->get().projection.bottom == -4.0f &&
               cameraResource->get().projection.top == 4.0f,
           "camera resource should preserve orthographic bounds");
    const Vec3f expectedForward{0.0f, 0.0f, -1.0f};
    EXPECT(cameraResource->get().pose.forward == expectedForward,
           "camera resource should preserve camera pose");
  }

  auto light = std::make_shared<DirectionalLight>();
  scene->addLight(light);
  EXPECT(scene->resources().lightCount() == 1,
         "scene resource table should own one light entry");
  const auto lightHandles = scene->getLightHandles();
  EXPECT(lightHandles.size() == 1,
         "scene should expose light handles instead of owning light pointers");
  EXPECT(scene->resources().resolve(lightHandles.front()).has_value(),
         "scene light handle should resolve through scene resource table");

  const auto snapshot = scene->resources().buildSnapshot();
  EXPECT(snapshot.cameraHandles.size() == 1,
         "snapshot should include camera handles");
  EXPECT(snapshot.lightHandles.size() == 1,
         "snapshot should include light handles");

  scene->removeCamera(cameraNode);
  EXPECT(!cameraComponent->get().getCameraHandle().isValid(),
         "removed camera should clear camera handle");
  EXPECT(scene->resources().cameraCount() == 0,
         "removed camera should release camera entry");

  const auto registeredLights = scene->getLights();
  EXPECT(registeredLights.size() == 1,
         "scene should expose table-owned light for removal");
  if (!registeredLights.empty()) {
    scene->removeLight(registeredLights.front());
  }
  EXPECT(scene->resources().lightCount() == 0,
         "removed light should release light entry");
}

void testSceneGpuRecordLayoutContract() {
  EXPECT(sizeof(SceneGpuVertexRecord) == 64,
         "SceneGpuVertexRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuMeshRecord) == 16,
         "SceneGpuMeshRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuPrimitiveRecord) == 16,
         "SceneGpuPrimitiveRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuObjectRecord) == 176,
         "SceneGpuObjectRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuMaterialRecord) == 80,
         "SceneGpuMaterialRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuFrameParams) == 176,
         "SceneGpuFrameParams std430 contract should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, objectToWorld) == 0,
         "SceneGpuObjectRecord objectToWorld offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, worldToObject) == 64,
         "SceneGpuObjectRecord worldToObject offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, boundsMin) == 128,
         "SceneGpuObjectRecord boundsMin offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, boundsMax) == 144,
         "SceneGpuObjectRecord boundsMax offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, visible) == 160,
         "SceneGpuObjectRecord visible offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, flags) == 164,
         "SceneGpuObjectRecord flags offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, visibilityMask) == 168,
         "SceneGpuObjectRecord visibilityMask offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, debugId) == 172,
         "SceneGpuObjectRecord debugId offset should stay stable");
}

void testSceneGpuVertexRecordPacksTangentSignInUvTangentSignZ() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeTangentSignMeshBuffer()));
  (void)mesh;

  const auto upload = table.buildUploadView();
  EXPECT(upload.vertices.size() == 3,
         "upload view should contain the authored tangent vertices");
  if (upload.vertices.empty()) {
    return;
  }

  EXPECT(upload.vertices[0].uvTangentSign.x == 0.25f &&
             upload.vertices[0].uvTangentSign.y == 0.75f,
         "upload vertex should preserve UV in uvTangentSign.xy");
  EXPECT(upload.vertices[0].uvTangentSign.z == -1.0f,
         "upload vertex should pack authored tangent.w sign into "
         "uvTangentSign.z");
}

void testPbrTextureIndicesEnterUploadView() {
  const bool found =
      cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf");
  EXPECT(found, "DamagedHelmet asset root should be discoverable");
  if (!found) {
    return;
  }

  const auto asset = LX_infra::scene_asset::loadGltfSceneAsset(
      "assets/models/damaged_helmet/DamagedHelmet.gltf",
      "assets/materials/pbr.material");
  const auto material = asset.material;
  SceneResourceTable table;
  const auto meshHandle = table.registerMesh(uniqueMesh(asset.mesh));
  const auto materialHandle = table.registerMaterial(uniqueMaterial(material));
  ObjectResource object;
  object.mesh = meshHandle;
  object.material = materialHandle;
  object.worldBounds = BoundingBox{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
  (void)table.registerObject(object);
  const auto registeredMaterial = table.resolve(materialHandle);
  EXPECT(registeredMaterial.has_value(),
         "registered material should resolve from the table");
  if (!registeredMaterial.has_value()) {
    return;
  }
  EXPECT(registeredMaterial->get()
             .getTextureHandle(StringID("albedoMap"))
             .isValid(),
         "registered material should bind albedoMap through TextureHandle");
  EXPECT(registeredMaterial->get()
             .getTextureHandle(StringID("normalMap"))
             .isValid(),
         "registered material should bind normalMap through TextureHandle");
  EXPECT(registeredMaterial->get()
             .getTextureHandle(StringID("metallicRoughnessMap"))
             .isValid(),
         "registered material should bind metallicRoughnessMap through "
         "TextureHandle");
  EXPECT(
      registeredMaterial->get().getTextureHandle(StringID("aoMap")).isValid(),
      "registered material should bind aoMap through TextureHandle");
  EXPECT(registeredMaterial->get()
             .getTextureHandle(StringID("emissiveMap"))
             .isValid(),
         "registered material should bind emissiveMap through TextureHandle");

  const auto upload = table.buildUploadView();
  EXPECT(!upload.materials.empty(), "upload view should contain material");
  EXPECT(upload.textures.size() == 5,
         "upload view should deduplicate DamagedHelmet PBR textures");
  if (upload.materials.empty()) {
    return;
  }

  EXPECT(upload.materials[0].baseColorTexture != u32_max,
         "base color texture index should be assigned");
  EXPECT(upload.materials[0].normalTexture != u32_max,
         "normal texture index should be assigned");
  EXPECT(upload.materials[0].metallicRoughnessTexture != u32_max,
         "MR texture index should be assigned");
  EXPECT(upload.materials[0].aoTexture != u32_max,
         "AO texture index should be assigned");
  EXPECT(upload.materials[0].emissiveTexture != u32_max,
         "emissive texture index should be assigned");
  EXPECT(upload.materials[0].baseColor.x == 1.0f &&
             upload.materials[0].baseColor.y == 1.0f &&
             upload.materials[0].baseColor.z == 1.0f &&
             upload.materials[0].baseColor.w == 1.0f,
         "DamagedHelmet baseColorFactor should enter the GPU material record");
  EXPECT(upload.materials[0].pbrParams.x == 1.0f,
         "DamagedHelmet metallicFactor should enter the GPU material record");
  EXPECT(upload.materials[0].pbrParams.y == 1.0f,
         "DamagedHelmet roughnessFactor should enter the GPU material record");
  EXPECT(upload.materials[0].pbrParams.w == 1.0f,
         "DamagedHelmet AO scalar should enter the GPU material record");

  const auto rebuiltUpload = table.buildUploadView();
  EXPECT(rebuiltUpload.textures.size() == 5,
         "rebuilt upload view should not accumulate stale texture entries");
}

void testSceneWithoutIblDoesNotCreateDefaultEnvironmentResources() {
  Scene scene("no_ibl_scene");
  const auto resources = scene.resources().getIblEnvironmentResources();
  EXPECT(resources.empty(),
         "scene without configured IBL should not synthesize default "
         "environment descriptor resources");
}

void testSceneResourceTableUploadViewTracksTableGeneration() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  object.visibilityMask = 0x12345678u;
  object.debugOnly = true;
  object.debugId = StringID("scene_gpu_record_object");
  const auto objectHandle = table.registerObject(object);

  const auto firstView = table.buildUploadView();
  EXPECT(firstView.tableGeneration != 0,
         "upload view should expose table mutation generation");
  EXPECT(firstView.meshes.size() == 1, "upload view should expose one mesh");
  EXPECT(firstView.objects.size() == 1, "upload view should expose one object");
  EXPECT(firstView.primitives.size() == 1,
         "upload view should expose one primitive");
  EXPECT(firstView.primitives.front().meshIndex == mesh.index,
         "primitive GPU record should reference compact mesh index");
  EXPECT(firstView.primitives.front().materialIndex == material.index,
         "primitive GPU record should reference compact material index");
  EXPECT(firstView.objects.front().visibilityMask == 0x12345678u,
         "object GPU record should preserve visibility mask");
  EXPECT(firstView.objects.front().flags == 1,
         "object GPU record should preserve flags separately from mask");
  EXPECT(firstView.objects.front().debugId == object.debugId.id,
         "object GPU record should preserve debug id separately from mask");
  EXPECT(firstView.materials.size() == 1,
         "upload view should expose one material");
  EXPECT(firstView.materials.front().baseColor.x == 0.25f &&
             firstView.materials.front().baseColor.y == 0.5f &&
             firstView.materials.front().baseColor.z == 0.75f &&
             firstView.materials.front().baseColor.w == 0.9f,
         "material base color should reach GPU record");
  EXPECT(firstView.materials.front().pbrParams.y == 0.35f,
         "material roughness scalar should reach GPU record");
  EXPECT(firstView.textures.empty(),
         "material without sampler bindings should not upload textures");
  EXPECT(
      firstView.materials.front().baseColorTexture == u32_max &&
          firstView.materials.front().normalTexture == u32_max &&
          firstView.materials.front().metallicRoughnessTexture == u32_max &&
          firstView.materials.front().aoTexture == u32_max &&
          firstView.materials.front().emissiveTexture == u32_max,
      "material without sampler bindings should keep sentinel texture indices");

  object.visible = false;
  table.updateObject(objectHandle, object);
  const auto secondView = table.buildUploadView();
  EXPECT(secondView.tableGeneration > firstView.tableGeneration,
         "object update should advance table mutation generation");
  EXPECT(secondView.objects.front().visible == 0,
         "object visibility should reach GPU record");
}

void testSceneResourceTableUploadViewReflectsMaterialMutationAfterBuild() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto materialInstance = makeGpuRecordMaterial();
  const auto material =
      table.registerMaterial(uniqueMaterial(materialInstance));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto firstView = table.buildUploadView();
  auto tableMaterial = table.resolve(material);
  EXPECT(tableMaterial.has_value(),
         "table-owned material should resolve for mutation");
  if (!tableMaterial.has_value()) {
    return;
  }
  tableMaterial->get().setParameter(StringID("MaterialUBO"),
                                    StringID("baseColor"),
                                    Vec4f{0.9f, 0.8f, 0.7f, 0.6f});
  const auto secondView = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep material mutation object alive");
  EXPECT(secondView.tableGeneration == firstView.tableGeneration,
         "table-owned material mutation should not advance table mutation "
         "generation");
  EXPECT(secondView.materials.size() == 1,
         "upload view should keep one material after mutation");
  EXPECT(secondView.materials.front().baseColor.x == 0.9f &&
             secondView.materials.front().baseColor.y == 0.8f &&
             secondView.materials.front().baseColor.z == 0.7f &&
             secondView.materials.front().baseColor.w == 0.6f,
         "upload view should reflect external material parameter mutation");
}

void testSceneResourceTableTracksActiveMaterialTagSwitch() {
  auto node = SceneNode::create("tagged_resource_table_node");
  node->addComponent<MeshComponent>(makeMeshBuffer());
  auto materialComponent = node->addComponent<MaterialComponent>(
      "realtime-pbr", makeGpuRecordMaterial(Vec4f{0.1f, 0.2f, 0.3f, 1.0f}));
  EXPECT(materialComponent.has_value(),
         "tagged material component should attach");
  materialComponent->get().setTaggedMaterial(
      "offline-pbr", makeGpuRecordMaterial(Vec4f{0.8f, 0.7f, 0.6f, 1.0f}));

  auto scene = Scene::create("tagged_resource_table_scene", node);
  const auto before = scene->resources().buildUploadView();
  EXPECT(before.materials.size() == 1,
         "tagged scene should expose one active material");
  EXPECT(before.materials.front().baseColor.x == 0.1f,
         "initial active tag should feed resource table");

  scene->setActiveMaterialTagForRenderables("offline-pbr");
  const auto after = scene->resources().buildUploadView();
  EXPECT(after.materials.size() == 1,
         "tag switch should keep one active material");
  EXPECT(after.materials.front().baseColor.x == 0.8f &&
             after.materials.front().baseColor.y == 0.7f &&
             after.materials.front().baseColor.z == 0.6f,
         "resource table should reflect switched material tag");
  EXPECT(after.primitives.size() == 1 &&
             after.primitives.front().materialIndex == 0,
         "primitive should reference compact index of switched material");
}

void testSceneResourceTableUploadViewPacksMatrixColumns() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.objectToWorld = Mat4f::translate({2.0f, 3.0f, 4.0f});
  object.worldToObject = Mat4f::translate({-2.0f, -3.0f, -4.0f});
  object.worldBounds = BoundingBox{{2.0f, 3.0f, 4.0f}, {3.0f, 4.0f, 4.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep translated object alive");
  EXPECT(view.objects.size() == 1,
         "upload view should expose translated object");
  if (!view.objects.empty()) {
    EXPECT(view.objects.front().objectToWorld[0].x == 1.0f &&
               view.objects.front().objectToWorld[0].y == 0.0f &&
               view.objects.front().objectToWorld[0].z == 0.0f &&
               view.objects.front().objectToWorld[0].w == 0.0f,
           "objectToWorld first GPU column should contain x basis");
    EXPECT(view.objects.front().objectToWorld[3].x == 2.0f &&
               view.objects.front().objectToWorld[3].y == 3.0f &&
               view.objects.front().objectToWorld[3].z == 4.0f &&
               view.objects.front().objectToWorld[3].w == 1.0f,
           "objectToWorld fourth GPU column should contain translation");
    EXPECT(
        view.objects.front().worldToObject[3].x == -2.0f &&
            view.objects.front().worldToObject[3].y == -3.0f &&
            view.objects.front().worldToObject[3].z == -4.0f &&
            view.objects.front().worldToObject[3].w == 1.0f,
        "worldToObject fourth GPU column should contain inverse translation");
  }
}

void testSceneResourceTableUploadViewUsesCompactRecordIndices() {
  SceneResourceTable table;
  const auto releasedMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto liveMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto releasedMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));
  const auto liveMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource releasedObject;
  releasedObject.mesh = releasedMesh;
  releasedObject.material = releasedMaterial;
  releasedObject.worldBounds =
      BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto releasedObjectHandle = table.registerObject(releasedObject);

  ObjectResource liveObject;
  liveObject.mesh = liveMesh;
  liveObject.material = liveMaterial;
  liveObject.worldBounds = BoundingBox{{2.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 0.0f}};
  const auto liveObjectHandle = table.registerObject(liveObject);

  table.release(releasedObjectHandle);
  table.release(releasedMesh);
  table.release(releasedMaterial);

  const auto view = table.buildUploadView();
  EXPECT(liveMesh.index == 1, "test setup should leave live mesh in slot 1");
  EXPECT(liveMaterial.index == 1,
         "test setup should leave live material in slot 1");
  EXPECT(liveObjectHandle.index == 1,
         "test setup should leave live object in slot 1");
  EXPECT(view.meshes.size() == 1, "upload view should compact live meshes");
  EXPECT(view.materials.size() == 1,
         "upload view should compact live materials");
  EXPECT(view.objects.size() == 1, "upload view should compact live objects");
  EXPECT(view.primitives.size() == 1,
         "upload view should emit one live primitive");

  EXPECT(view.primitives.front().meshIndex < view.meshes.size(),
         "primitive mesh index should point inside compact mesh span");
  EXPECT(view.primitives.front().meshIndex == 0,
         "primitive mesh index should use compact mesh record position");
  EXPECT(view.primitives.front().materialIndex < view.materials.size(),
         "primitive material index should point inside compact material span");
  EXPECT(
      view.primitives.front().materialIndex == 0,
      "primitive material index should use compact material record position");
  EXPECT(view.primitives.front().objectIndex < view.objects.size(),
         "primitive object index should point inside compact object span");
  EXPECT(view.primitives.front().objectIndex == 0,
         "primitive object index should use compact object record position");
}

void testSceneResourceTableUploadViewSkipsObjectsWithReleasedDependencies() {
  SceneResourceTable table;
  const auto releasedMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto liveMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto releasedMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));
  const auto liveMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource missingMeshObject;
  missingMeshObject.mesh = releasedMesh;
  missingMeshObject.material = liveMaterial;
  missingMeshObject.worldBounds =
      BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto missingMeshObjectHandle = table.registerObject(missingMeshObject);

  ObjectResource missingMaterialObject;
  missingMaterialObject.mesh = liveMesh;
  missingMaterialObject.material = releasedMaterial;
  missingMaterialObject.worldBounds =
      BoundingBox{{2.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 0.0f}};
  const auto missingMaterialObjectHandle =
      table.registerObject(missingMaterialObject);

  table.release(releasedMesh);
  table.release(releasedMaterial);

  const auto view = table.buildUploadView();
  EXPECT(view.meshes.size() == 1,
         "upload view should still expose independent live mesh records");
  EXPECT(view.materials.empty(),
         "upload view should not expose material records from objects with "
         "released mesh dependencies");
  EXPECT(table.isAlive(missingMeshObjectHandle),
         "test setup should keep object with released mesh alive");
  EXPECT(table.isAlive(missingMaterialObjectHandle),
         "test setup should keep object with released material alive");
  EXPECT(view.objects.empty(),
         "upload view should skip objects whose mesh or material was released");
  EXPECT(
      view.primitives.empty(),
      "upload view should skip primitives whose mesh or material was released");
}

void testSceneResourceTableUploadViewSkipsObjectsWithStaleDependencies() {
  SceneResourceTable table;
  const auto staleMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto staleMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource staleObject;
  staleObject.mesh = staleMesh;
  staleObject.material = staleMaterial;
  staleObject.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto staleObjectHandle = table.registerObject(staleObject);

  table.release(staleMesh);
  table.release(staleMaterial);
  const auto replacementMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto replacementMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(staleObjectHandle),
         "test setup should keep object with stale dependencies alive");
  EXPECT(replacementMesh.index == staleMesh.index &&
             replacementMesh.generation != staleMesh.generation,
         "test setup should reuse mesh slot with a new generation");
  EXPECT(replacementMaterial.index == staleMaterial.index &&
             replacementMaterial.generation != staleMaterial.generation,
         "test setup should reuse material slot with a new generation");
  EXPECT(view.meshes.size() == 1,
         "upload view should expose replacement live mesh record");
  EXPECT(view.materials.empty(),
         "upload view should not expose material records that no live object "
         "references");
  EXPECT(view.objects.empty(),
         "upload view should skip object with stale mesh/material handles");
  EXPECT(view.primitives.empty(),
         "upload view should skip primitive with stale mesh/material handles");
}

void testSceneResourceTableUploadViewEmitsPrimitivePerTriangle() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeTwoTriangleMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep two-triangle object alive");
  EXPECT(view.indices.size() == 6,
         "two-triangle mesh should upload six indices");
  EXPECT(view.primitives.size() == 2,
         "two-triangle mesh should emit two primitive records");
  if (view.primitives.size() == 2) {
    EXPECT(view.primitives[0].indexOffset == 0,
           "first primitive should start at first uploaded triangle");
    EXPECT(view.primitives[1].indexOffset == 3,
           "second primitive should start at second uploaded triangle");
    EXPECT(view.primitives[0].meshIndex == 0 &&
               view.primitives[1].meshIndex == 0,
           "primitive mesh indices should use compact mesh record position");
    EXPECT(view.primitives[0].materialIndex == 0 &&
               view.primitives[1].materialIndex == 0,
           "primitive material indices should use compact material record "
           "position");
    EXPECT(
        view.primitives[0].objectIndex == 0 &&
            view.primitives[1].objectIndex == 0,
        "primitive object indices should use compact object record position");
  }
}

void testSceneResourceTableUploadViewPacksGlobalCompactVertexIndices() {
  SceneResourceTable table;
  const auto baseMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto mesh = table.registerMesh(uniqueMesh(makeOffsetMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));
  (void)baseMesh;

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep offset mesh object alive");
  EXPECT(view.vertices.size() == 6,
         "upload view should keep vertices from both compact mesh slices");
  EXPECT(view.indices.size() == 6,
         "upload view should keep indices from both compact mesh slices");
  if (view.indices.size() == 6) {
    EXPECT(
        view.indices[3] == 3 && view.indices[4] == 4 && view.indices[5] == 5,
        "offset mesh indices should point directly into compact vertex span");
  }
  if (!view.primitives.empty()) {
    EXPECT(view.primitives[0].indexOffset == 3,
           "offset mesh primitive should point at its global index slice");
  }
}

void testSceneResourceTableUploadViewSkipsInvalidMeshIndexRanges() {
  SceneResourceTable table;
  const auto mesh =
      table.registerMesh(uniqueMesh(makeInvalidIndexRangeMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep object with invalid mesh slice alive");
  EXPECT(view.meshes.empty(),
         "invalid mesh index range should not emit a mesh record");
  EXPECT(view.vertices.empty(),
         "invalid mesh index range should not emit vertices");
  EXPECT(view.indices.empty(),
         "invalid mesh index range should not emit indices");
  EXPECT(view.objects.empty(),
         "invalid mesh index range should not emit dependent object records");
  EXPECT(
      view.primitives.empty(),
      "invalid mesh index range should not emit dependent primitive records");
  EXPECT(view.materials.empty(),
         "invalid mesh index range should not emit dependent material records");
}

void testSceneResourceTableUploadViewSkipsUnsupportedMeshTopology() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(
      uniqueMesh(makeLineListMeshBufferWithTriangleSizedIndexCount()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep object with unsupported topology alive");
  EXPECT(view.meshes.empty(),
         "non-triangle-list mesh should not emit a mesh record");
  EXPECT(view.vertices.empty(),
         "non-triangle-list mesh should not emit vertices");
  EXPECT(view.indices.empty(),
         "non-triangle-list mesh should not emit indices");
  EXPECT(view.objects.empty(),
         "non-triangle-list mesh should not emit dependent object records");
  EXPECT(view.primitives.empty(),
         "non-triangle-list mesh should not emit primitive records");
  EXPECT(
      view.materials.empty(),
      "unsupported mesh topology should not emit dependent material records");
}

} // namespace

int main() {
  testGeometryStorageAndMeshBufferContract();
  testHandleGenerationInvalidatesStaleMeshHandle();
  testSceneRegistersRenderableComponentResources();
  testSceneRegistersCameraAndLightResources();
  testSceneGpuRecordLayoutContract();
  testSceneGpuVertexRecordPacksTangentSignInUvTangentSignZ();
  testPbrTextureIndicesEnterUploadView();
  testSceneWithoutIblDoesNotCreateDefaultEnvironmentResources();
  testSceneResourceTableUploadViewTracksTableGeneration();
  testSceneResourceTableUploadViewReflectsMaterialMutationAfterBuild();
  testSceneResourceTableTracksActiveMaterialTagSwitch();
  testSceneResourceTableUploadViewPacksMatrixColumns();
  testSceneResourceTableUploadViewUsesCompactRecordIndices();
  testSceneResourceTableUploadViewSkipsObjectsWithReleasedDependencies();
  testSceneResourceTableUploadViewSkipsObjectsWithStaleDependencies();
  testSceneResourceTableUploadViewEmitsPrimitivePerTriangle();
  testSceneResourceTableUploadViewPacksGlobalCompactVertexIndices();
  testSceneResourceTableUploadViewSkipsInvalidMeshIndexRanges();
  testSceneResourceTableUploadViewSkipsUnsupportedMeshTopology();

  if (s_failures != 0) {
    std::cerr << "test_scene_resource_table failed: " << s_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_scene_resource_table passed\n";
  return 0;
}
