#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/raytracing/software_bvh.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <bit>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace LX_core;

namespace {

int failures = 0;
constexpr u32 LeafNodeFlag = 0x80000000u;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                          \
      ++failures;                                                              \
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

void testSoftwareBvhBuildsFromSceneResourceTable() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(makeMeshBuffer());
  const auto material =
      table.registerMaterial(MaterialInstance::create(
          MaterialTemplate::create("software_bvh_material")));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);
  (void)objectHandle;

  const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(table.buildUploadView());
  EXPECT(!bvh.nodes().empty(), "software BVH should contain nodes");
  EXPECT(bvh.primitiveCount() == 1,
         "one indexed triangle should produce one BVH primitive");
  const u32 packedRootCount =
      std::bit_cast<u32>(bvh.nodes().front().boundsMaxCount.w);
  EXPECT((packedRootCount & ~LeafNodeFlag) == 1,
         "single-triangle BVH root should reference one primitive");
}

void testSoftwareBvhThrowsForEmptyPrimitiveList() {
  bool threw = false;
  try {
    (void)SceneSoftwareBvh::build(SceneResourceTable{}.buildUploadView());
  } catch (const std::runtime_error &error) {
    threw = std::string(error.what()).find("empty primitive list") !=
            std::string::npos;
  }
  EXPECT(threw, "empty software BVH build should fail clearly");
}

void testSoftwareBvhUsesCompactUploadIndicesAndObjectTransform() {
  SceneResourceTable table;
  const auto baseMesh = table.registerMesh(makeMeshBuffer());
  const auto liveMesh = table.registerMesh(makeOffsetMeshBuffer());
  const auto material =
      table.registerMaterial(MaterialInstance::create(
          MaterialTemplate::create("software_bvh_transform_material")));
  (void)baseMesh;

  Mat4f objectToWorld = Mat4f::translate(Vec3f{2.0f, 3.0f, 4.0f});
  ObjectResource object;
  object.mesh = liveMesh;
  object.material = material;
  object.objectToWorld = objectToWorld;
  object.worldBounds = BoundingBox{{2.0f, 3.0f, 4.0f},
                                   {3.0f, 4.0f, 4.0f}};
  const auto objectHandle = table.registerObject(object);
  (void)objectHandle;

  const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(table.buildUploadView());
  EXPECT(bvh.primitiveCount() == 1,
         "one indexed offset triangle should produce one BVH primitive");
  EXPECT(bvh.primitives().front().meshIndex == 1,
         "BVH should preserve compact mesh upload index");
  EXPECT(bvh.nodes().front().boundsMinLeftFirst.x == 2.0f,
         "root bounds should include object-space vertices transformed to world");
  EXPECT(bvh.nodes().front().boundsMinLeftFirst.y == 3.0f,
         "root bounds should include translated y minimum");
  EXPECT(bvh.nodes().front().boundsMinLeftFirst.z == 4.0f,
         "root bounds should include translated z minimum");
}

void testSoftwareBvhLayoutContract() {
  EXPECT(sizeof(SceneSoftwareBvhNode) == 32,
         "SceneSoftwareBvhNode std430 contract should stay stable");
  EXPECT(sizeof(SceneSoftwareBvhPrimitive) == 12,
         "SceneSoftwareBvhPrimitive should only store derived BVH references");
}

} // namespace

int main() {
  testSoftwareBvhLayoutContract();
  testSoftwareBvhThrowsForEmptyPrimitiveList();
  testSoftwareBvhBuildsFromSceneResourceTable();
  testSoftwareBvhUsesCompactUploadIndicesAndObjectTransform();
  if (failures != 0) {
    std::cerr << "test_offline_gpu_scene failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_gpu_scene passed\n";
  return 0;
}
