#include "core/asset/material_instance.hpp"
#include "core/asset/material_pass_definition.hpp"
#include "core/asset/mesh.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <iostream>
#include <memory>
#include <vector>

using namespace LX_core;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

struct SceneVertex final {
  Vec3f position;

  static VertexLayout getLayout() {
    return VertexLayout(
        std::vector<VertexLayoutItem>{
            VertexLayoutItem{"position", 0, DataType::Float3,
                             sizeof(Vec3f), 0}},
        sizeof(SceneVertex));
  }
};

MeshBufferUniquePtr makeTriangleMesh() {
  auto vertices = std::vector<SceneVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto vb = VertexBuffer<SceneVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create({0, 1, 2});
  return MeshBuffer::create(
             vb, ib,
             BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}})
      ->cloneUnique();
}

GeometryStorageUniquePtr
makeGeometry(std::vector<SceneVertex> vertices, std::vector<u32> indices,
             PrimitiveTopology topology = PrimitiveTopology::TriangleList) {
  auto vb = VertexBuffer<SceneVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices), topology);
  return GeometryStorage::create(std::move(vb), std::move(ib))->cloneUnique();
}

MeshBufferUniquePtr makeRegisteredMesh(GeometryStorageHandle storage,
                                       u32 vertexOffset, u32 indexOffset,
                                       u32 vertexCount, u32 indexCount) {
  return MeshBuffer::createRegistered(
      storage, vertexOffset, indexOffset, vertexCount, indexCount,
      BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MeshHandle registerTriangleMesh(SceneResourceTable &table) {
  const GeometryStorageHandle storage = table.registerGeometryStorage(
      makeGeometry({{{0.0f, 0.0f, 0.0f}},
                    {{1.0f, 0.0f, 0.0f}},
                    {{0.0f, 1.0f, 0.0f}}},
                   {0, 1, 2}));
  return table.registerMesh(makeRegisteredMesh(storage, 0, 0, 3, 3));
}

MeshHandle registerTwoTriangleMesh(SceneResourceTable &table) {
  const GeometryStorageHandle storage = table.registerGeometryStorage(
      makeGeometry({{{0.0f, 0.0f, 0.0f}},
                    {{1.0f, 0.0f, 0.0f}},
                    {{0.0f, 1.0f, 0.0f}},
                    {{1.0f, 1.0f, 0.0f}}},
                   {0, 1, 2, 1, 3, 2}));
  return table.registerMesh(makeRegisteredMesh(storage, 0, 0, 4, 6));
}

MaterialInstanceUniquePtr makeMaterial() {
  auto material = MaterialInstance::createUnique(MaterialTemplate::create("matte"));
  material->setBsdfType("matte");
  material->syncGpuData();
  return material;
}

MaterialInstanceUniquePtr makeMaterialWithForwardCull(CullMode cullMode) {
  auto tmpl = MaterialTemplate::create("matte");
  MaterialPassDefinition passDefinition;
  passDefinition.renderState.cullMode = cullMode;
  tmpl->setPassDefinition(Pass_Forward, std::move(passDefinition));
  auto material = MaterialInstance::createUnique(std::move(tmpl));
  material->setBsdfType("matte");
  material->syncGpuData();
  return material;
}

ObjectHandle registerObject(SceneResourceTable &table, MeshHandle mesh,
                            MaterialHandle material) {
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds =
      BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  return table.registerObject(object);
}

void testSceneResourceTableRegistersObjectGraph() {
  SceneResourceTable table;
  const MeshHandle mesh = registerTriangleMesh(table);
  const MaterialHandle material = table.registerMaterial(makeMaterial());

  const ObjectHandle objectHandle = registerObject(table, mesh, material);

  EXPECT(mesh.isValid(), "mesh handle should be valid");
  EXPECT(material.isValid(), "material handle should be valid");
  EXPECT(objectHandle.isValid(), "object handle should be valid");
  EXPECT(table.resolve(mesh).has_value(), "mesh should resolve");
  EXPECT(table.resolve(material).has_value(), "material should resolve");
  EXPECT(table.resolve(objectHandle).has_value(), "object should resolve");
}

void testUploadViewCarriesRegisteredSceneRecords() {
  SceneResourceTable table;
  const MeshHandle mesh = registerTriangleMesh(table);
  const MaterialHandle material = table.registerMaterial(makeMaterial());
  (void)registerObject(table, mesh, material);

  const SceneResourceTableUploadView view = table.buildUploadView();

  EXPECT(!view.meshes.empty(), "upload view should include mesh records");
  EXPECT(!view.materials.empty(),
         "upload view should include material records");
  EXPECT(!view.objects.empty(), "upload view should include object records");
  EXPECT(!view.indices.empty(), "upload view should include index data");
}

void testReleasedAndStaleHandlesDoNotResolve() {
  SceneResourceTable table;
  const MeshHandle releasedMesh = registerTriangleMesh(table);
  table.release(releasedMesh);
  const MeshHandle replacementMesh = registerTriangleMesh(table);
  const MaterialHandle releasedMaterial = table.registerMaterial(makeMaterial());
  table.release(releasedMaterial);

  EXPECT(!table.resolve(releasedMesh).has_value(),
         "released mesh handle should not resolve");
  EXPECT(!table.resolve(releasedMaterial).has_value(),
         "released material handle should not resolve");
  EXPECT(!table.resolve(MeshHandle{releasedMesh.index, releasedMesh.generation})
              .has_value(),
         "stale mesh handle should not resolve after slot reuse");
  EXPECT(table.resolve(replacementMesh).has_value(),
         "replacement mesh handle should resolve");
}

void testUploadViewSkipsObjectsWithReleasedOrStaleDependencies() {
  SceneResourceTable table;
  const MeshHandle liveMesh = registerTriangleMesh(table);
  const MaterialHandle liveMaterial = table.registerMaterial(makeMaterial());
  (void)registerObject(table, liveMesh, liveMaterial);

  const MeshHandle releasedMesh = registerTriangleMesh(table);
  table.release(releasedMesh);
  (void)registerObject(table, releasedMesh, liveMaterial);

  const MeshHandle staleMesh = registerTriangleMesh(table);
  table.release(staleMesh);
  (void)registerTriangleMesh(table);
  (void)registerObject(table, staleMesh, liveMaterial);

  const MaterialHandle releasedMaterial = table.registerMaterial(makeMaterial());
  table.release(releasedMaterial);
  (void)registerObject(table, liveMesh, releasedMaterial);

  const SceneResourceTableUploadView view = table.buildUploadView();

  EXPECT(view.objects.size() == 1,
         "upload view should skip objects with released or stale deps");
  EXPECT(view.draws.size() == 1,
         "upload view should only emit draw for valid object deps");
}

void testUploadViewEmitsOnePrimitivePerTriangle() {
  SceneResourceTable table;
  const MeshHandle mesh = registerTwoTriangleMesh(table);
  const MaterialHandle material = table.registerMaterial(makeMaterial());
  (void)registerObject(table, mesh, material);

  const SceneResourceTableUploadView view = table.buildUploadView();

  EXPECT(view.meshes.size() == 1, "valid mesh should upload once");
  EXPECT(view.draws.size() == 1, "valid object should emit one draw");
  EXPECT(view.primitives.size() == 2,
         "two triangle mesh should emit two primitive records");
}

void testUploadViewSkipsInvalidMeshRangesAndUnsupportedTopology() {
  SceneResourceTable table;
  const MaterialHandle material = table.registerMaterial(makeMaterial());
  const GeometryStorageHandle invalidRangeStorage =
      table.registerGeometryStorage(makeGeometry({{{0.0f, 0.0f, 0.0f}},
                                                  {{1.0f, 0.0f, 0.0f}},
                                                  {{0.0f, 1.0f, 0.0f}}},
                                                 {0, 1, 5}));
  const MeshHandle invalidRangeMesh = table.registerMesh(
      makeRegisteredMesh(invalidRangeStorage, 0, 0, 3, 3));
  (void)registerObject(table, invalidRangeMesh, material);

  const GeometryStorageHandle lineStorage =
      table.registerGeometryStorage(makeGeometry({{{0.0f, 0.0f, 0.0f}},
                                                  {{1.0f, 0.0f, 0.0f}}},
                                                 {0, 1},
                                                 PrimitiveTopology::LineList));
  const MeshHandle lineMesh =
      table.registerMesh(makeRegisteredMesh(lineStorage, 0, 0, 2, 2));
  (void)registerObject(table, lineMesh, material);

  const SceneResourceTableUploadView view = table.buildUploadView();

  EXPECT(view.meshes.empty(),
         "upload view should skip invalid range and non-triangle meshes");
  EXPECT(view.objects.empty(),
         "objects depending on skipped meshes should not upload");
  EXPECT(view.draws.empty(), "skipped mesh objects should not emit draws");
}

void testUploadViewExportsTypedHandleMappings() {
  SceneResourceTable table;
  const MeshHandle mesh = registerTriangleMesh(table);
  const MaterialHandle material = table.registerMaterial(makeMaterial());
  const ObjectHandle object = registerObject(table, mesh, material);

  const SceneResourceTableUploadView view = table.buildUploadView();

  EXPECT(view.meshIndexByHandle.size() == 1,
         "mesh upload mapping should include live mesh");
  EXPECT(view.meshIndexByHandle.front().handle == mesh,
         "mesh mapping should preserve handle");
  EXPECT(view.meshIndexByHandle.front().typedIndex == 0,
         "mesh mapping should point at compact mesh index");
  EXPECT(view.materialIndexByHandle.size() == 1,
         "material upload mapping should include simple material");
  EXPECT(view.materialIndexByHandle.front().handle == material,
         "material mapping should preserve handle");
  EXPECT(view.objectIndexByHandle.size() == 1,
         "object upload mapping should include valid object");
  EXPECT(view.objectIndexByHandle.front().handle == object,
         "object mapping should preserve handle");
  EXPECT(view.draws.front().meshIndex == view.meshIndexByHandle.front().typedIndex,
         "draw should use compact mesh index");
  EXPECT(view.draws.front().materialIndex ==
             view.materialIndexByHandle.front().typedIndex,
         "draw should use compact material index");
}

void testUploadViewExportsForwardCullModeMaterialRecord() {
  SceneResourceTable table;
  const MeshHandle mesh = registerTriangleMesh(table);
  const MaterialHandle material =
      table.registerMaterial(makeMaterialWithForwardCull(CullMode::Front));
  (void)registerObject(table, mesh, material);

  const SceneResourceTableUploadView view = table.buildUploadView();

  EXPECT(view.materials.size() == 1,
         "upload view should emit one material record");
  EXPECT((view.materials.front().flags & kSceneGpuMaterialCullModeMask) ==
             kSceneGpuMaterialCullModeFront,
         "material record should export Forward pass cull mode");
}

} // namespace

int main() {
  testSceneResourceTableRegistersObjectGraph();
  testUploadViewCarriesRegisteredSceneRecords();
  testReleasedAndStaleHandlesDoNotResolve();
  testUploadViewSkipsObjectsWithReleasedOrStaleDependencies();
  testUploadViewEmitsOnePrimitivePerTriangle();
  testUploadViewSkipsInvalidMeshRangesAndUnsupportedTopology();
  testUploadViewExportsTypedHandleMappings();
  testUploadViewExportsForwardCullModeMaterialRecord();
  if (g_failures != 0) {
    std::cerr << g_failures << " scene resource table checks failed\n";
    return 1;
  }
  return 0;
}
