#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/raytracing/software_bvh.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <array>
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

[[nodiscard]] u32 packedU32(float value) { return std::bit_cast<u32>(value); }

[[nodiscard]] bool isLeaf(const SceneSoftwareBvhNode &node) {
  return (packedU32(node.boundsMaxCount.w) & LeafNodeFlag) != 0;
}

[[nodiscard]] u32 leafFirst(const SceneSoftwareBvhNode &node) {
  return packedU32(node.boundsMinLeftFirst.w);
}

[[nodiscard]] u32 leafCount(const SceneSoftwareBvhNode &node) {
  return packedU32(node.boundsMaxCount.w) & ~LeafNodeFlag;
}

[[nodiscard]] SceneGpuVertexRecord makeGpuVertex(float x, float y, float z) {
  SceneGpuVertexRecord vertex;
  vertex.position = Vec4f{x, y, z, 1.0f};
  return vertex;
}

[[nodiscard]] SceneGpuObjectRecord makeIdentityObject() {
  SceneGpuObjectRecord object;
  object.objectToWorld = {
      Vec4f{1.0f, 0.0f, 0.0f, 0.0f},
      Vec4f{0.0f, 1.0f, 0.0f, 0.0f},
      Vec4f{0.0f, 0.0f, 1.0f, 0.0f},
      Vec4f{0.0f, 0.0f, 0.0f, 1.0f},
  };
  return object;
}

struct ManualUploadView final {
  std::vector<SceneGpuVertexRecord> vertices;
  std::vector<u32> indices;
  std::vector<SceneGpuPrimitiveRecord> primitives;
  std::vector<SceneGpuObjectRecord> objects;

  [[nodiscard]] SceneResourceTableUploadView view() const {
    return SceneResourceTableUploadView{
        .vertices = vertices,
        .indices = indices,
        .primitives = primitives,
        .objects = objects,
    };
  }
};

[[nodiscard]] ManualUploadView makeSingleTriangleUploadView() {
  ManualUploadView upload;
  upload.vertices = {
      makeGpuVertex(0.0f, 0.0f, 0.0f),
      makeGpuVertex(1.0f, 0.0f, 0.0f),
      makeGpuVertex(0.0f, 1.0f, 0.0f),
  };
  upload.indices = {0, 1, 2};
  upload.objects = {makeIdentityObject()};
  upload.primitives.push_back(SceneGpuPrimitiveRecord{
      .indexOffset = 0,
      .meshIndex = 0,
      .materialIndex = 0,
      .objectIndex = 0,
  });
  return upload;
}

[[nodiscard]] ManualUploadView makeSeparatedTriangleUploadView(u32 count) {
  ManualUploadView upload;
  upload.objects = {makeIdentityObject()};
  upload.vertices.reserve(static_cast<usize>(count) * 3);
  upload.indices.reserve(static_cast<usize>(count) * 3);
  upload.primitives.reserve(count);
  for (u32 i = 0; i < count; ++i) {
    const float x = static_cast<float>(i) * 10.0f;
    const u32 vertexOffset = static_cast<u32>(upload.vertices.size());
    upload.vertices.push_back(makeGpuVertex(x, 0.0f, 0.0f));
    upload.vertices.push_back(makeGpuVertex(x + 1.0f, 0.0f, 0.0f));
    upload.vertices.push_back(makeGpuVertex(x, 1.0f, 0.0f));
    upload.indices.push_back(vertexOffset);
    upload.indices.push_back(vertexOffset + 1);
    upload.indices.push_back(vertexOffset + 2);
    upload.primitives.push_back(SceneGpuPrimitiveRecord{
        .indexOffset = static_cast<u32>(upload.indices.size() - 3),
        .meshIndex = i,
        .materialIndex = 0,
        .objectIndex = 0,
    });
  }
  return upload;
}

void expectBvhBuildThrows(const SceneResourceTableUploadView &view,
                          const std::string &messageFragment) {
  bool threw = false;
  try {
    (void)SceneSoftwareBvh::build(view);
  } catch (const std::runtime_error &error) {
    threw = std::string(error.what()).find(messageFragment) !=
            std::string::npos;
  }
  EXPECT(threw, "malformed software BVH upload view should fail clearly");
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
  const u32 packedRootCount = packedU32(bvh.nodes().front().boundsMaxCount.w);
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

void testSoftwareBvhRejectsMalformedUploadViewReferences() {
  {
    auto upload = makeSingleTriangleUploadView();
    upload.primitives.front().objectIndex = 1;
    expectBvhBuildThrows(upload.view(), "object index");
  }
  {
    auto upload = makeSingleTriangleUploadView();
    upload.primitives.front().indexOffset = 1;
    expectBvhBuildThrows(upload.view(), "index range");
  }
  {
    auto upload = makeSingleTriangleUploadView();
    upload.indices.front() = 3;
    expectBvhBuildThrows(upload.view(), "vertex index");
  }
}

void testSoftwareBvhLeafRangesReferenceReorderedPrimitives() {
  const auto upload = makeSeparatedTriangleUploadView(5);
  const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(upload.view());

  EXPECT(bvh.nodes().size() > 1,
         "five separated triangles should create internal BVH nodes");
  const auto &root = bvh.nodes().front();
  EXPECT(!isLeaf(root),
         "root node should be internal for more than four primitives");

  const u32 leftIndex = packedU32(root.boundsMinLeftFirst.w);
  const u32 rightIndex = packedU32(root.boundsMaxCount.w);
  EXPECT(leftIndex < bvh.nodes().size(), "left child index should be valid");
  EXPECT(rightIndex < bvh.nodes().size(), "right child index should be valid");

  std::array<bool, 5> visited{};
  for (const u32 childIndex : {leftIndex, rightIndex}) {
    if (childIndex >= bvh.nodes().size()) {
      continue;
    }
    const auto &child = bvh.nodes()[childIndex];
    EXPECT(isLeaf(child), "root children should be leaves for five primitives");
    const u32 first = leafFirst(child);
    const u32 count = leafCount(child);
    EXPECT(count > 0, "leaf should reference at least one primitive");
    EXPECT(static_cast<usize>(first) + count <= bvh.primitives().size(),
           "leaf primitive range should stay inside reordered primitive array");
    for (u32 i = 0; i < count &&
                    static_cast<usize>(first) + i < bvh.primitives().size();
         ++i) {
      const auto primitive = bvh.primitives()[first + i];
      EXPECT(primitive.primitiveIndex < visited.size(),
             "leaf range should map through reordered primitive references");
      if (primitive.primitiveIndex < visited.size()) {
        visited[primitive.primitiveIndex] = true;
      }
    }
  }
  for (bool wasVisited : visited) {
    EXPECT(wasVisited,
           "each upload primitive should appear in one BVH leaf range");
  }
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
  testSoftwareBvhRejectsMalformedUploadViewReferences();
  testSoftwareBvhLeafRangesReferenceReorderedPrimitives();
  if (failures != 0) {
    std::cerr << "test_offline_gpu_scene failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_gpu_scene passed\n";
  return 0;
}
