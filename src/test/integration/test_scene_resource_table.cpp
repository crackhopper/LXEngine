#include "core/asset/mesh.hpp"
#include "core/asset/material_instance.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/asset/shader.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/scene_gpu_records.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <cstddef>
#include <iostream>

using namespace LX_core;

namespace {

int s_failures = 0;

#define EXPECT(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__     \
                << ")\n";                                                     \
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
  return MeshBuffer::create(vb, ib, BoundingBox{{0.0f, 0.0f, 0.0f},
                                                {1.0f, 1.0f, 0.0f}});
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
  return MeshBuffer::create(vb, ib, BoundingBox{{0.0f, 0.0f, 0.0f},
                                                {1.0f, 1.0f, 0.0f}});
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
  return MeshBuffer::create(storage, 1, 0, 3, 3,
                            BoundingBox{{0.0f, 0.0f, 0.0f},
                                        {1.0f, 1.0f, 0.0f}});
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
  return MeshBuffer::create(storage, 1, 0, 3, 3,
                            BoundingBox{{0.0f, 0.0f, 0.0f},
                                        {1.0f, 1.0f, 0.0f}});
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
  auto ib = IndexBuffer::create(std::move(indices), PrimitiveTopology::LineList);
  return MeshBuffer::create(vb, ib, BoundingBox{{0.0f, 0.0f, 0.0f},
                                                {1.0f, 1.0f, 0.0f}});
}

MaterialInstanceSharedPtr makeGpuRecordMaterial() {
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
                         Vec4f{0.25f, 0.5f, 0.75f, 0.9f});
  material->setParameter(StringID("MaterialUBO"), StringID("roughnessFactor"),
                         0.35f);
  return material;
}

void testGeometryStorageAndMeshBufferContract() {
  auto mesh = makeMeshBuffer();
  EXPECT(mesh->getGeometryStorage() != nullptr,
         "MeshBuffer should reference GeometryStorage");
  EXPECT(mesh->getVertexBuffer()->getVertexCount() == 3,
         "MeshBuffer should expose storage vertex count");
  EXPECT(mesh->getIndexBuffer()->indexCount() == 3,
         "MeshBuffer should expose storage index count");
  EXPECT(mesh->getVertexOffset() == 0, "default vertex offset should be zero");
  EXPECT(mesh->getIndexOffset() == 0, "default index offset should be zero");
  EXPECT(mesh->getBounds().isValid(), "MeshBuffer should carry bounds");
}

void testHandleGenerationInvalidatesStaleMeshHandle() {
  SceneResourceTable table;
  auto first = table.registerMesh(makeMeshBuffer());
  EXPECT(table.isAlive(first), "registered mesh handle should be alive");
  EXPECT(table.resolve(first).has_value(),
         "registered mesh handle should resolve");

  table.release(first);
  EXPECT(!table.isAlive(first), "released mesh handle should not be alive");
  EXPECT(!table.resolve(first).has_value(),
         "released mesh handle should not resolve");

  auto second = table.registerMesh(makeMeshBuffer());
  EXPECT(second.index == first.index, "table may reuse released slot");
  EXPECT(second.generation != first.generation,
         "reused slot should get a new generation");
  EXPECT(table.isAlive(second), "new mesh handle should be alive");
  EXPECT(!table.isAlive(first), "stale mesh handle should remain invalid");
}

void testSceneRegistersRenderableComponentResources() {
  auto mesh = makeMeshBuffer();
  auto material =
      MaterialInstance::create(MaterialTemplate::create("scene_resource_table"));
  auto node = SceneNode::create("resource_table_node");
  node->addComponent<MeshComponent>(mesh);
  node->addComponent<MaterialComponent>(material);

  auto scene = Scene::create("resource_table_scene", node);

  const auto meshComponent = node->getComponent<MeshComponent>();
  const auto materialComponent = node->getComponent<MaterialComponent>();
  EXPECT(meshComponent.has_value(),
         "node should keep the registered mesh component");
  EXPECT(materialComponent.has_value(),
         "node should keep the registered material component");
  EXPECT(dynamic_cast<IRenderableComponent *>(&meshComponent->get()) != nullptr,
         "mesh component should expose renderable component capability");

  const MeshHandle meshHandle = meshComponent->get().getMeshHandle();
  const ObjectHandle objectHandle = meshComponent->get().getObjectHandle();
  const GeometryStorageHandle geometryHandle =
      meshComponent->get().getGeometryStorageHandle();
  const MaterialHandle materialHandle =
      materialComponent->get().getMaterialHandle();
  EXPECT(geometryHandle.isValid(),
         "mesh component should receive a geometry storage handle");
  EXPECT(meshHandle.isValid(), "mesh component should receive a mesh handle");
  EXPECT(objectHandle.isValid(),
         "mesh component should receive an object handle");
  EXPECT(materialHandle.isValid(),
         "material component should receive a material handle");
  EXPECT(scene->resources().geometryStorageCount() == 1,
         "scene resource table should own one geometry storage entry");
  EXPECT(scene->resources().meshCount() == 1,
         "scene resource table should own one mesh entry");
  EXPECT(scene->resources().materialCount() == 1,
         "scene resource table should own one material entry");
  EXPECT(scene->resources().objectCount() == 1,
         "scene resource table should own one object entry");
  EXPECT(scene->resources().resolve(meshHandle).has_value(),
         "mesh handle should resolve through scene resource table");
  EXPECT(scene->resources().resolve(objectHandle).has_value(),
         "object handle should resolve through scene resource table");
  EXPECT(scene->resources().resolve(geometryHandle).has_value(),
         "geometry storage handle should resolve through scene resource table");
  EXPECT(scene->resources().resolve(materialHandle).has_value(),
         "material handle should resolve through scene resource table");

  const auto snapshot = scene->resources().buildSnapshot();
  EXPECT(snapshot.geometryStorageHandles.size() == 1,
         "snapshot should include geometry storage handles");
  EXPECT(snapshot.meshHandles.size() == 1,
         "snapshot should include mesh handles");
  EXPECT(snapshot.materialHandles.size() == 1,
         "snapshot should include material handles");
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
  const auto syncedSnapshot = scene->buildRenderSceneSnapshot();
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
  EXPECT(scene->resources().geometryStorageCount() == 0,
         "removed node should release geometry storage entry");
  EXPECT(scene->resources().meshCount() == 0,
         "removed node should release mesh entry");
  EXPECT(scene->resources().materialCount() == 0,
         "removed node should release material entry");
  EXPECT(scene->resources().objectCount() == 0,
         "removed node should release object entry");
}

void testSceneRegistersCameraAndLightResources() {
  auto scene = Scene::create("camera_light_resource_table");
  auto cameraNode = SceneNode::create("resource_camera");
  cameraNode->addComponent<CameraComponent>();
  auto cameraBeforeRegister = cameraNode->getComponent<CameraComponent>();
  EXPECT(cameraBeforeRegister.has_value(),
         "camera node should expose camera component before registration");
  if (cameraBeforeRegister.has_value()) {
    cameraBeforeRegister->get().applyProjectionState(
        CameraType::Orthographic, 50.0f, 1.25f, 0.2f, 120.0f, -5.0f, 5.0f,
        -4.0f, 4.0f);
  }

  scene->addCamera(cameraNode);

  const auto cameraComponent = cameraNode->getComponent<CameraComponent>();
  EXPECT(cameraComponent.has_value(),
         "camera node should keep camera component");
  const CameraHandle cameraHandle = cameraComponent->get().getCameraHandle();
  EXPECT(cameraHandle.isValid(),
         "camera component should receive a camera handle");
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

  scene->removeLight(light);
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
  EXPECT(sizeof(SceneGpuMaterialRecord) == 64,
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

void testSceneResourceTableUploadViewTracksTableGeneration() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(makeMeshBuffer());
  const auto material = table.registerMaterial(makeGpuRecordMaterial());
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 0.0f}};
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
  const auto mesh = table.registerMesh(makeMeshBuffer());
  const auto materialInstance = makeGpuRecordMaterial();
  const auto material = table.registerMaterial(materialInstance);
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto firstView = table.buildUploadView();
  materialInstance->setParameter(StringID("MaterialUBO"), StringID("baseColor"),
                                 Vec4f{0.9f, 0.8f, 0.7f, 0.6f});
  const auto secondView = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep material mutation object alive");
  EXPECT(secondView.tableGeneration == firstView.tableGeneration,
         "external material mutation should not advance table mutation generation");
  EXPECT(secondView.materials.size() == 1,
         "upload view should keep one material after mutation");
  EXPECT(secondView.materials.front().baseColor.x == 0.9f &&
             secondView.materials.front().baseColor.y == 0.8f &&
             secondView.materials.front().baseColor.z == 0.7f &&
             secondView.materials.front().baseColor.w == 0.6f,
         "upload view should reflect external material parameter mutation");
}

void testSceneResourceTableUploadViewPacksMatrixColumns() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(makeMeshBuffer());
  const auto material = table.registerMaterial(makeGpuRecordMaterial());

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.objectToWorld = Mat4f::translate({2.0f, 3.0f, 4.0f});
  object.worldToObject = Mat4f::translate({-2.0f, -3.0f, -4.0f});
  object.worldBounds = BoundingBox{{2.0f, 3.0f, 4.0f},
                                   {3.0f, 4.0f, 4.0f}};
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
    EXPECT(view.objects.front().worldToObject[3].x == -2.0f &&
               view.objects.front().worldToObject[3].y == -3.0f &&
               view.objects.front().worldToObject[3].z == -4.0f &&
               view.objects.front().worldToObject[3].w == 1.0f,
           "worldToObject fourth GPU column should contain inverse translation");
  }
}

void testSceneResourceTableUploadViewUsesCompactRecordIndices() {
  SceneResourceTable table;
  const auto releasedMesh = table.registerMesh(makeMeshBuffer());
  const auto liveMesh = table.registerMesh(makeMeshBuffer());
  const auto releasedMaterial = table.registerMaterial(makeGpuRecordMaterial());
  const auto liveMaterial = table.registerMaterial(makeGpuRecordMaterial());

  ObjectResource releasedObject;
  releasedObject.mesh = releasedMesh;
  releasedObject.material = releasedMaterial;
  releasedObject.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                           {1.0f, 1.0f, 0.0f}};
  const auto releasedObjectHandle = table.registerObject(releasedObject);

  ObjectResource liveObject;
  liveObject.mesh = liveMesh;
  liveObject.material = liveMaterial;
  liveObject.worldBounds = BoundingBox{{2.0f, 0.0f, 0.0f},
                                       {3.0f, 1.0f, 0.0f}};
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
  EXPECT(view.primitives.front().materialIndex == 0,
         "primitive material index should use compact material record position");
  EXPECT(view.primitives.front().objectIndex < view.objects.size(),
         "primitive object index should point inside compact object span");
  EXPECT(view.primitives.front().objectIndex == 0,
         "primitive object index should use compact object record position");
}

void testSceneResourceTableUploadViewSkipsObjectsWithReleasedDependencies() {
  SceneResourceTable table;
  const auto releasedMesh = table.registerMesh(makeMeshBuffer());
  const auto liveMesh = table.registerMesh(makeMeshBuffer());
  const auto releasedMaterial = table.registerMaterial(makeGpuRecordMaterial());
  const auto liveMaterial = table.registerMaterial(makeGpuRecordMaterial());

  ObjectResource missingMeshObject;
  missingMeshObject.mesh = releasedMesh;
  missingMeshObject.material = liveMaterial;
  missingMeshObject.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                              {1.0f, 1.0f, 0.0f}};
  const auto missingMeshObjectHandle = table.registerObject(missingMeshObject);

  ObjectResource missingMaterialObject;
  missingMaterialObject.mesh = liveMesh;
  missingMaterialObject.material = releasedMaterial;
  missingMaterialObject.worldBounds = BoundingBox{{2.0f, 0.0f, 0.0f},
                                                 {3.0f, 1.0f, 0.0f}};
  const auto missingMaterialObjectHandle =
      table.registerObject(missingMaterialObject);

  table.release(releasedMesh);
  table.release(releasedMaterial);

  const auto view = table.buildUploadView();
  EXPECT(view.meshes.size() == 1,
         "upload view should still expose independent live mesh records");
  EXPECT(view.materials.size() == 1,
         "upload view should still expose independent live material records");
  EXPECT(table.isAlive(missingMeshObjectHandle),
         "test setup should keep object with released mesh alive");
  EXPECT(table.isAlive(missingMaterialObjectHandle),
         "test setup should keep object with released material alive");
  EXPECT(view.objects.empty(),
         "upload view should skip objects whose mesh or material was released");
  EXPECT(view.primitives.empty(),
         "upload view should skip primitives whose mesh or material was released");
}

void testSceneResourceTableUploadViewSkipsObjectsWithStaleDependencies() {
  SceneResourceTable table;
  const auto staleMesh = table.registerMesh(makeMeshBuffer());
  const auto staleMaterial = table.registerMaterial(makeGpuRecordMaterial());

  ObjectResource staleObject;
  staleObject.mesh = staleMesh;
  staleObject.material = staleMaterial;
  staleObject.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                        {1.0f, 1.0f, 0.0f}};
  const auto staleObjectHandle = table.registerObject(staleObject);

  table.release(staleMesh);
  table.release(staleMaterial);
  const auto replacementMesh = table.registerMesh(makeMeshBuffer());
  const auto replacementMaterial = table.registerMaterial(makeGpuRecordMaterial());

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
  EXPECT(view.materials.size() == 1,
         "upload view should expose replacement live material record");
  EXPECT(view.objects.empty(),
         "upload view should skip object with stale mesh/material handles");
  EXPECT(view.primitives.empty(),
         "upload view should skip primitive with stale mesh/material handles");
}

void testSceneResourceTableUploadViewEmitsPrimitivePerTriangle() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(makeTwoTriangleMeshBuffer());
  const auto material = table.registerMaterial(makeGpuRecordMaterial());

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 0.0f}};
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
           "primitive material indices should use compact material record position");
    EXPECT(view.primitives[0].objectIndex == 0 &&
               view.primitives[1].objectIndex == 0,
           "primitive object indices should use compact object record position");
  }
}

void testSceneResourceTableUploadViewPacksGlobalCompactVertexIndices() {
  SceneResourceTable table;
  const auto baseMesh = table.registerMesh(makeMeshBuffer());
  const auto mesh = table.registerMesh(makeOffsetMeshBuffer());
  const auto material = table.registerMaterial(makeGpuRecordMaterial());
  (void)baseMesh;

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep offset mesh object alive");
  EXPECT(view.vertices.size() == 6,
         "upload view should keep vertices from both compact mesh slices");
  EXPECT(view.indices.size() == 6,
         "upload view should keep indices from both compact mesh slices");
  if (view.indices.size() == 6) {
    EXPECT(view.indices[3] == 3 && view.indices[4] == 4 &&
               view.indices[5] == 5,
           "offset mesh indices should point directly into compact vertex span");
  }
  if (!view.primitives.empty()) {
    EXPECT(view.primitives[0].indexOffset == 3,
           "offset mesh primitive should point at its global index slice");
  }
}

void testSceneResourceTableUploadViewSkipsInvalidMeshIndexRanges() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(makeInvalidIndexRangeMeshBuffer());
  const auto material = table.registerMaterial(makeGpuRecordMaterial());

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 0.0f}};
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
  EXPECT(view.primitives.empty(),
         "invalid mesh index range should not emit dependent primitive records");
  EXPECT(view.materials.size() == 1,
         "invalid mesh index range should not suppress independent materials");
}

void testSceneResourceTableUploadViewSkipsUnsupportedMeshTopology() {
  SceneResourceTable table;
  const auto mesh =
      table.registerMesh(makeLineListMeshBufferWithTriangleSizedIndexCount());
  const auto material = table.registerMaterial(makeGpuRecordMaterial());

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep object with unsupported topology alive");
  EXPECT(view.meshes.empty(),
         "non-triangle-list mesh should not emit a mesh record");
  EXPECT(view.vertices.empty(),
         "non-triangle-list mesh should not emit vertices");
  EXPECT(view.indices.empty(), "non-triangle-list mesh should not emit indices");
  EXPECT(view.objects.empty(),
         "non-triangle-list mesh should not emit dependent object records");
  EXPECT(view.primitives.empty(),
         "non-triangle-list mesh should not emit primitive records");
  EXPECT(view.materials.size() == 1,
         "unsupported mesh topology should not suppress independent materials");
}

} // namespace

int main() {
  testGeometryStorageAndMeshBufferContract();
  testHandleGenerationInvalidatesStaleMeshHandle();
  testSceneRegistersRenderableComponentResources();
  testSceneRegistersCameraAndLightResources();
  testSceneGpuRecordLayoutContract();
  testSceneResourceTableUploadViewTracksTableGeneration();
  testSceneResourceTableUploadViewReflectsMaterialMutationAfterBuild();
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
