#include "core/asset/mesh.hpp"
#include "core/asset/material_instance.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
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

} // namespace

int main() {
  testGeometryStorageAndMeshBufferContract();
  testHandleGenerationInvalidatesStaleMeshHandle();
  testSceneRegistersRenderableComponentResources();
  testSceneRegistersCameraAndLightResources();

  if (s_failures != 0) {
    std::cerr << "test_scene_resource_table failed: " << s_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_scene_resource_table passed\n";
  return 0;
}
