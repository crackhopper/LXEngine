#include "core/offline/offline_ray_scene.hpp"

#include "core/asset/mesh.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace LX_core::offline {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr u32 LeafNodeFlag = 0x80000000u;

struct Bounds final {
  Vec3f min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
  Vec3f max{-std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max()};

  void include(const Vec3f &p) {
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    min.z = std::min(min.z, p.z);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
    max.z = std::max(max.z, p.z);
  }

  void include(const Bounds &b) {
    include(b.min);
    include(b.max);
  }

  [[nodiscard]] Vec3f extent() const { return max - min; }
};

[[nodiscard]] Vec4f vec4(const Vec3f &v, float w) {
  return Vec4f{v.x, v.y, v.z, w};
}

[[nodiscard]] Vec4f vec4(const Vec2f &v, float z, float w) {
  return Vec4f{v.x, v.y, z, w};
}

[[nodiscard]] float uintAsFloat(u32 value) {
  return std::bit_cast<float>(value);
}

[[nodiscard]] std::array<Vec4f, 4> matrixColumns(const Mat4f &m) {
  return {
      Vec4f{m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3]},
      Vec4f{m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3]},
      Vec4f{m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3]},
      Vec4f{m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]},
  };
}

[[nodiscard]] Mat4f matrixFromColumns(const std::array<Vec4f, 4> &columns) {
  Mat4f m;
  for (int c = 0; c < 4; ++c) {
    m.m[c][0] = columns[c].x;
    m.m[c][1] = columns[c].y;
    m.m[c][2] = columns[c].z;
    m.m[c][3] = columns[c].w;
  }
  return m;
}

[[nodiscard]] Mat4f inverseAffine(const Mat4f &m) {
  const float a00 = m(0, 0);
  const float a01 = m(0, 1);
  const float a02 = m(0, 2);
  const float a10 = m(1, 0);
  const float a11 = m(1, 1);
  const float a12 = m(1, 2);
  const float a20 = m(2, 0);
  const float a21 = m(2, 1);
  const float a22 = m(2, 2);

  const float c00 = a11 * a22 - a12 * a21;
  const float c01 = -(a10 * a22 - a12 * a20);
  const float c02 = a10 * a21 - a11 * a20;
  const float c10 = -(a01 * a22 - a02 * a21);
  const float c11 = a00 * a22 - a02 * a20;
  const float c12 = -(a00 * a21 - a01 * a20);
  const float c20 = a01 * a12 - a02 * a11;
  const float c21 = -(a00 * a12 - a02 * a10);
  const float c22 = a00 * a11 - a01 * a10;
  const float det = a00 * c00 + a01 * c01 + a02 * c02;
  if (std::abs(det) < 1.0e-8f) {
    return Mat4f::identity();
  }
  const float invDet = 1.0f / det;

  Mat4f inverse = Mat4f::identity();
  inverse(0, 0) = c00 * invDet;
  inverse(0, 1) = c10 * invDet;
  inverse(0, 2) = c20 * invDet;
  inverse(1, 0) = c01 * invDet;
  inverse(1, 1) = c11 * invDet;
  inverse(1, 2) = c21 * invDet;
  inverse(2, 0) = c02 * invDet;
  inverse(2, 1) = c12 * invDet;
  inverse(2, 2) = c22 * invDet;

  const Vec3f t{m(0, 3), m(1, 3), m(2, 3)};
  inverse(0, 3) =
      -(inverse(0, 0) * t.x + inverse(0, 1) * t.y + inverse(0, 2) * t.z);
  inverse(1, 3) =
      -(inverse(1, 0) * t.x + inverse(1, 1) * t.y + inverse(1, 2) * t.z);
  inverse(2, 3) =
      -(inverse(2, 0) * t.x + inverse(2, 1) * t.y + inverse(2, 2) * t.z);
  return inverse;
}

[[nodiscard]] u32 findOffset(const VertexLayout &layout, const char *name) {
  for (const auto &item : layout.getItems()) {
    if (item.name == name) {
      return item.offset;
    }
  }
  throw std::runtime_error(std::string("offline mesh missing vertex attribute: ") +
                           name);
}

template <typename T>
[[nodiscard]] T readAttribute(const std::byte *vertex, u32 offset) {
  T value{};
  std::memcpy(&value, vertex + offset, sizeof(T));
  return value;
}

[[nodiscard]] OfflineVertexRecord readVertex(const IVertexBuffer &buffer,
                                             u32 vertexIndex) {
  const VertexLayout &layout = buffer.getLayout();
  const u32 posOffset = findOffset(layout, "inPos");
  const u32 normalOffset = findOffset(layout, "inNormal");
  const u32 uvOffset = findOffset(layout, "inUV");
  const u32 tangentOffset = findOffset(layout, "inTangent");
  const auto *bytes = static_cast<const std::byte *>(buffer.getRawData());
  const std::byte *vertex =
      bytes + static_cast<usize>(vertexIndex) * layout.getStride();

  const Vec3f position = readAttribute<Vec3f>(vertex, posOffset);
  const Vec3f normal = readAttribute<Vec3f>(vertex, normalOffset).normalized();
  const Vec2f uv = readAttribute<Vec2f>(vertex, uvOffset);
  const Vec4f tangent = readAttribute<Vec4f>(vertex, tangentOffset);
  return OfflineVertexRecord{
      .position = vec4(position, 1.0f),
      .normal = vec4(normal, 0.0f),
      .uvTangentSign = vec4(uv, tangent.w, 0.0f),
      .tangent = tangent,
  };
}

[[nodiscard]] BoundingBox boundsForMesh(const OfflineMeshIR &mesh) {
  BoundingBox bounds;
  for (const auto &vertex : mesh.vertices) {
    bounds.merge(vertex.position);
  }
  return bounds;
}

[[nodiscard]] Bounds primitiveBounds(const OfflineRayScene &scene,
                                     const OfflinePrimitiveRecord &primitive) {
  const OfflineMeshRecord &mesh = scene.meshes.at(primitive.meshIndex);
  const OfflineObjectRecord &object = scene.objects.at(primitive.objectIndex);
  const Mat4f objectToWorld = matrixFromColumns(object.objectToWorld);
  Bounds bounds;
  for (u32 i = 0; i < 3; ++i) {
    const u32 index = scene.indices.at(primitive.indexOffset + i);
    const OfflineVertexRecord &vertex =
        scene.vertices.at(mesh.vertexOffset + index);
    bounds.include(transformPoint(objectToWorld, vertex.position.toVec3()));
  }
  return bounds;
}

[[nodiscard]] Vec3f primitiveCentroid(const OfflineRayScene &scene,
                                      const OfflinePrimitiveRecord &primitive) {
  const Bounds bounds = primitiveBounds(scene, primitive);
  return (bounds.min + bounds.max) * 0.5f;
}

u32 buildNode(OfflineRayScene &scene, u32 first, u32 count) {
  const u32 nodeIndex = static_cast<u32>(scene.bvhNodes.size());
  scene.bvhNodes.push_back({});

  Bounds bounds;
  Bounds centroidBounds;
  for (u32 i = first; i < first + count; ++i) {
    bounds.include(primitiveBounds(scene, scene.primitives[i]));
    centroidBounds.include(primitiveCentroid(scene, scene.primitives[i]));
  }

  if (count <= 4) {
    scene.bvhNodes[nodeIndex].boundsMinLeftFirst =
        Vec4f{bounds.min.x, bounds.min.y, bounds.min.z, uintAsFloat(first)};
    scene.bvhNodes[nodeIndex].boundsMaxCount =
        Vec4f{bounds.max.x, bounds.max.y, bounds.max.z,
              uintAsFloat(LeafNodeFlag | count)};
    return nodeIndex;
  }

  const Vec3f extent = centroidBounds.extent();
  int axis = 0;
  if (extent.y > extent.x && extent.y >= extent.z) {
    axis = 1;
  } else if (extent.z > extent.x && extent.z > extent.y) {
    axis = 2;
  }

  const u32 mid = first + count / 2;
  std::nth_element(scene.primitives.begin() + first,
                   scene.primitives.begin() + mid,
                   scene.primitives.begin() + first + count,
                   [&scene, axis](const OfflinePrimitiveRecord &a,
                                  const OfflinePrimitiveRecord &b) {
                     return primitiveCentroid(scene, a)[axis] <
                            primitiveCentroid(scene, b)[axis];
                   });

  const u32 left = buildNode(scene, first, mid - first);
  const u32 right = buildNode(scene, mid, first + count - mid);
  scene.bvhNodes[nodeIndex].boundsMinLeftFirst =
      Vec4f{bounds.min.x, bounds.min.y, bounds.min.z, uintAsFloat(left)};
  scene.bvhNodes[nodeIndex].boundsMaxCount =
      Vec4f{bounds.max.x, bounds.max.y, bounds.max.z, uintAsFloat(right)};
  return nodeIndex;
}

[[nodiscard]] OfflineSceneParams buildParams(const OfflineSceneIR &scene,
                                             const OutputProfile &output,
                                             const OfflineRenderSettings &offline,
                                             u32 materialCount) {
  const auto &camera = scene.camera;
  Vec3f forward = (camera.target - camera.eye).normalized();
  if (forward.length2() == 0.0f) {
    forward = Vec3f{0.0f, 0.0f, -1.0f};
  }
  Vec3f right = forward.cross(camera.up).normalized();
  if (right.length2() == 0.0f) {
    right = Vec3f{1.0f, 0.0f, 0.0f};
  }
  const Vec3f up = right.cross(forward).normalized();
  const float aspect =
      output.height == 0
          ? camera.aspect
          : static_cast<float>(output.width) / static_cast<float>(output.height);
  const float tanHalfFov =
      std::tan(camera.fovYDegrees * (kPi / 180.0f) * 0.5f);

  const auto light =
      scene.directionalLights.empty() ? OfflineDirectionalLightIR{}
                                      : scene.directionalLights.front();
  OfflineSceneParams params;
  params.eye = vec4(camera.eye, 0.0f);
  params.cameraRight = vec4(right * (tanHalfFov * aspect), 0.0f);
  params.cameraUp = vec4(up * tanHalfFov, 0.0f);
  params.cameraForward = vec4(forward, 0.0f);
  params.lightDirectionIntensity =
      vec4(light.direction.normalized(), light.intensity);
  params.lightColorEnvironment =
      Vec4f{light.color.x, light.color.y, light.color.z,
            scene.environment.enabled ? scene.environment.intensity : 0.35f};
  params.width = output.width;
  params.height = output.height;
  params.samples = offline.samples;
  params.seed = offline.seed;
  params.materialCount = materialCount;
  params.maxBounce = offline.maxBounce;
  params.shadowsEnabled = offline.shadows ? 1u : 0u;
  return params;
}

} // namespace

void OfflineBvhBuilder::build(OfflineRayScene &scene) const {
  if (scene.primitives.empty()) {
    throw std::runtime_error("cannot build offline BVH for empty primitive list");
  }
  scene.bvhNodes.clear();
  buildNode(scene, 0, static_cast<u32>(scene.primitives.size()));
  scene.params.primitiveCount = static_cast<u32>(scene.primitives.size());
  scene.params.bvhNodeCount = static_cast<u32>(scene.bvhNodes.size());
}

OfflineRayScene OfflineRaySceneBuilder::build(
    const OfflineSceneIR &scene, const OutputProfile &output,
    const OfflineRenderSettings &offline) const {
  if (scene.materials.empty()) {
    throw std::runtime_error("offline ray scene requires at least one material");
  }

  OfflineRayScene out;
  out.materials.reserve(scene.materials.size());
  for (const auto &material : scene.materials) {
    out.materials.push_back(OfflineMaterialRecord{
        .baseColor =
            Vec4f{material.baseColor.x, material.baseColor.y, material.baseColor.z,
                  1.0f},
        .params = Vec4f{material.metallic, material.roughness, 0.0f, 0.0f},
        .emissive =
            Vec4f{material.emissive.x, material.emissive.y, material.emissive.z,
                  0.0f},
    });
  }

  std::vector<MeshHandle> meshHandles;
  meshHandles.reserve(scene.meshes.size());
  for (const auto &mesh : scene.meshes) {
    std::vector<VertexPBR> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const auto &vertex : mesh.vertices) {
      vertices.push_back(VertexPBR{
          .pos = vertex.position,
          .normal = vertex.normal,
          .uv = vertex.uv,
          .tangent = Vec4f{1.0f, 0.0f, 0.0f, 1.0f},
      });
    }

    auto vertexBuffer = VertexBuffer<VertexPBR>::create(std::move(vertices));
    auto indexBuffer = IndexBuffer::create(std::vector<u32>(mesh.indices));
    auto storage = GeometryStorage::create(vertexBuffer, indexBuffer);
    const GeometryStorageHandle storageHandle =
        out.resourceTable.registerGeometryStorage(storage);
    (void)storageHandle;
    auto meshBuffer = MeshBuffer::create(storage, 0, 0, std::nullopt,
                                         std::nullopt, boundsForMesh(mesh), true);
    meshHandles.push_back(out.resourceTable.registerMesh(std::move(meshBuffer)));
  }

  for (const auto &instance : scene.instances) {
    if (!instance.visible) {
      continue;
    }
    if (instance.meshIndex >= meshHandles.size() ||
        instance.materialIndex >= out.materials.size()) {
      throw std::runtime_error("offline instance references invalid mesh/material");
    }

    MaterialHandle materialHandle;
    materialHandle.index = instance.materialIndex;
    materialHandle.generation = 1;
    const MeshBuffer &mesh =
        out.resourceTable.resolve(meshHandles[instance.meshIndex])->get();
    const Mat4f worldToObject = inverseAffine(instance.worldTransform);
    const ObjectHandle objectHandle = out.resourceTable.registerObject(ObjectResource{
        .mesh = meshHandles[instance.meshIndex],
        .material = materialHandle,
        .objectToWorld = instance.worldTransform,
        .worldToObject = worldToObject,
        .worldBounds = mesh.getBounds().transformed(instance.worldTransform),
        .visible = true,
    });
    (void)objectHandle;
  }

  out.snapshot = out.resourceTable.buildSnapshot();
  if (out.snapshot.objects.empty()) {
    throw std::runtime_error("offline ray scene has no visible objects");
  }

  std::unordered_map<u32, u32> meshRecordByHandleIndex;
  for (const MeshHandle handle : out.snapshot.meshHandles) {
    const auto resolvedMesh = out.resourceTable.resolve(handle);
    if (!resolvedMesh.has_value()) {
      continue;
    }
    const MeshBuffer &mesh = resolvedMesh->get();
    const u32 meshRecordIndex = static_cast<u32>(out.meshes.size());
    meshRecordByHandleIndex.emplace(handle.index, meshRecordIndex);
    const u32 vertexOffset = static_cast<u32>(out.vertices.size());
    const u32 indexOffset = static_cast<u32>(out.indices.size());

    const IVertexBuffer &vertexBuffer = *mesh.getVertexBuffer();
    for (u32 i = 0; i < mesh.getVertexCount(); ++i) {
      out.vertices.push_back(readVertex(vertexBuffer, mesh.getVertexOffset() + i));
    }

    const IndexBuffer &indexBuffer = *mesh.getIndexBuffer();
    const auto *indexData = static_cast<const u32 *>(indexBuffer.getRawData());
    if ((mesh.getIndexCount() % 3) != 0) {
      throw std::runtime_error("offline mesh index buffer is not triangle aligned");
    }
    for (u32 i = 0; i < mesh.getIndexCount(); ++i) {
      out.indices.push_back(indexData[mesh.getIndexOffset() + i]);
    }

    out.meshes.push_back(OfflineMeshRecord{
        .vertexOffset = vertexOffset,
        .indexOffset = indexOffset,
        .indexCount = mesh.getIndexCount(),
        .geometryIndex = handle.index,
    });
  }

  for (const auto &object : out.snapshot.objects) {
    const u32 objectIndex = static_cast<u32>(out.objects.size());
    out.objects.push_back(OfflineObjectRecord{
        .objectToWorld = matrixColumns(object.objectToWorld),
        .worldToObject = matrixColumns(object.worldToObject),
        .boundsMin = vec4(object.worldBounds.min, 0.0f),
        .boundsMax = vec4(object.worldBounds.max, 0.0f),
        .visible = object.visible ? 1u : 0u,
    });

    const auto meshIt = meshRecordByHandleIndex.find(object.meshIndex);
    if (meshIt == meshRecordByHandleIndex.end()) {
      throw std::runtime_error("offline object references unknown mesh handle");
    }
    const u32 meshIndex = meshIt->second;
    const OfflineMeshRecord &mesh = out.meshes[meshIndex];
    for (u32 i = 0; i < mesh.indexCount; i += 3) {
      out.primitives.push_back(OfflinePrimitiveRecord{
          .indexOffset = mesh.indexOffset + i,
          .meshIndex = meshIndex,
          .materialIndex = object.materialIndex,
          .objectIndex = objectIndex,
      });
    }
  }
  if (out.primitives.empty()) {
    throw std::runtime_error("offline ray scene has no visible primitives");
  }

  out.params =
      buildParams(scene, output, offline, static_cast<u32>(out.materials.size()));
  OfflineBvhBuilder{}.build(out);
  return out;
}

} // namespace LX_core::offline
