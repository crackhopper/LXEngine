#include "core/asset/material_instance.hpp"
#include "core/asset/shader.hpp"
#include "core/offline/offline_render_job.hpp"
#include "core/offline/offline_render_work_graph.hpp"
#include "core/offline/offline_render_validation.hpp"
#include "core/asset/mesh.hpp"
#include "core/raytracing/software_bvh.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <filesystem>
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

[[nodiscard]] std::filesystem::path findOfflineShaderSourcePath() {
  std::filesystem::path probe = std::filesystem::current_path();
  for (int i = 0; i < 8; ++i) {
    const auto candidate =
        probe / "assets" / "shaders" / "glsl" / "offline_primary_ray.comp";
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    const auto parent = probe.parent_path();
    if (parent == probe) {
      break;
    }
    probe = parent;
  }
  return {};
}

[[nodiscard]] bool
hasStorageBuffer(const std::vector<ShaderResourceBinding> &bindings,
                 const std::string &name) {
  for (const auto &binding : bindings) {
    if (binding.name == name &&
        binding.type == ShaderPropertyType::StorageBuffer) {
      return true;
    }
  }
  return false;
}

void testOfflineShaderUsesUnifiedSceneBuffers() {
  const auto shaderPath = findOfflineShaderSourcePath();
  EXPECT(!shaderPath.empty(),
         "offline shader source should be discoverable for reflection test");
  if (shaderPath.empty()) {
    return;
  }

  const auto compileResult = LX_infra::ShaderCompiler::compileFile(shaderPath);
  EXPECT(compileResult.success,
         "offline shader should compile before reflection");
  if (!compileResult.success) {
    std::cerr << compileResult.errorMessage << '\n';
    return;
  }

  const auto bindings = LX_infra::ShaderReflector::reflect(compileResult.stages);
  EXPECT(hasStorageBuffer(bindings, "SceneVertices"),
         "offline shader should use unified SceneVertices SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneIndices"),
         "offline shader should use unified SceneIndices SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneMeshes"),
         "offline shader should use unified SceneMeshes SSBO");
  EXPECT(hasStorageBuffer(bindings, "ScenePrimitives"),
         "offline shader should use unified ScenePrimitives SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneObjects"),
         "offline shader should use unified SceneObjects SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneMaterials"),
         "offline shader should use unified SceneMaterials SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneBvhNodes"),
         "offline shader should use unified SceneBvhNodes SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneFrameParams"),
         "offline shader should use unified SceneFrameParams SSBO");
  EXPECT(hasStorageBuffer(bindings, "OutputPixels"),
         "offline shader should use OutputPixels SSBO");
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

void expectInvalidOfflineJobThrows(
    const offline::OfflineRenderJob &job,
    const std::string &messageFragment) {
  bool threw = false;
  try {
    offline::validateOfflineRenderJob(job);
  } catch (const std::runtime_error &error) {
    threw = std::string(error.what()).find(messageFragment) !=
            std::string::npos;
  }
  EXPECT(threw, "invalid offline render job should fail before Vulkan setup");
}

[[nodiscard]] offline::OfflineRenderJob makeRenderableJobWithoutCamera() {
  offline::OfflineRenderJob job;
  job.output.width = 1;
  job.output.height = 1;
  const auto mesh = job.scene.registerMesh(makeMeshBuffer());
  const auto material =
      job.scene.registerMaterial(MaterialInstance::create(
          MaterialTemplate::create("offline_validation_material")));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = job.scene.registerObject(object);
  (void)objectHandle;
  return job;
}

[[nodiscard]] CameraResource makeValidationCameraResource() {
  const CameraPose pose = makeCameraPose(Vec3f{0.0f, 0.0f, 3.0f},
                                         Vec3f{0.0f, 0.0f, -1.0f},
                                         Vec3f{0.0f, 1.0f, 0.0f});
  const CameraProjection projection;
  return CameraResource{
      .pose = pose,
      .projection = projection,
      .view = makeCameraViewMatrix(pose),
      .proj = makeCameraProjectionMatrix(projection),
      .active = true,
  };
}

[[nodiscard]] offline::OfflineRenderJob makeRenderableJobWithCamera() {
  offline::OfflineRenderJob job = makeRenderableJobWithoutCamera();
  const auto camera = job.scene.registerCamera(makeValidationCameraResource());
  (void)camera;
  return job;
}

void testOfflineRenderWorkGraphBuildsRayTracePass() {
  offline::OfflineRenderJob job = makeRenderableJobWithCamera();
  job.output.width = 17;
  job.output.height = 9;

  const FrameGraph graph = offline::buildOfflineRenderWorkGraph(job);
  EXPECT(graph.getPasses().size() == 1,
         "offline default graph should have one ray trace pass");
  if (graph.getPasses().empty()) {
    return;
  }

  const FramePass &pass = graph.getPasses().front();
  EXPECT(pass.name == Pass_OfflineRayTrace,
         "offline graph pass should use OfflineRayTrace identity");
  EXPECT(pass.queue.getItems().size() == 1,
         "offline ray trace pass should submit one compute work item");
  if (pass.queue.getItems().empty()) {
    return;
  }

  const RenderWorkItem &item = pass.queue.getItems().front();
  EXPECT(item.domain == RenderDomain::Offline,
         "offline work item should mark offline domain");
  EXPECT(item.kind == RenderWorkKind::ComputeDispatch,
         "offline ray trace pass should execute as compute dispatch");
  EXPECT(item.compute.groupCountX == 3 && item.compute.groupCountY == 2 &&
             item.compute.groupCountZ == 1,
         "offline dispatch groups should round output dimensions to 8x8 groups");
}

void testOfflineRenderJobValidationRejectsZeroDimensions() {
  offline::OfflineRenderJob job = makeRenderableJobWithoutCamera();
  job.output.width = 0;
  expectInvalidOfflineJobThrows(job, "width/height must be positive");
}

void testOfflineRenderJobValidationRejectsMissingCamera() {
  const offline::OfflineRenderJob job = makeRenderableJobWithoutCamera();
  expectInvalidOfflineJobThrows(job, "active camera");
}

void testOfflineRenderJobValidationRejectsNonRenderableScene() {
  offline::OfflineRenderJob job;
  job.output.width = 1;
  job.output.height = 1;
  const auto camera = job.scene.registerCamera(makeValidationCameraResource());
  (void)camera;
  expectInvalidOfflineJobThrows(job, "no upload vertices");
}

} // namespace

int main() {
  testSoftwareBvhLayoutContract();
  testOfflineShaderUsesUnifiedSceneBuffers();
  testOfflineRenderJobValidationRejectsZeroDimensions();
  testOfflineRenderJobValidationRejectsMissingCamera();
  testOfflineRenderJobValidationRejectsNonRenderableScene();
  testOfflineRenderWorkGraphBuildsRayTracePass();
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
