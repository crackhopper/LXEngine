#include "core/scene/scene_resource_table.hpp"

#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/texture.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"

#include <cassert>
#include <cstring>
#include <utility>

namespace LX_core {
namespace {

[[nodiscard]] u32 nextGeneration(const u32 current) {
  const u32 next = current + 1;
  return next == 0 ? 1 : next;
}

[[nodiscard]] u64 nextGeneration(const u64 current) {
  const u64 next = current + 1;
  return next == 0 ? 1 : next;
}

struct CompactRecordIndex final {
  u32 generation = 0;
  u32 uploadIndex = u32_max;
};

[[nodiscard]] u32 findCompactRecordIndex(
    const std::vector<CompactRecordIndex> &indices,
    const ResourceHandleBase &handle) {
  if (!handle.isValid() || handle.index >= indices.size()) {
    return u32_max;
  }
  const auto &entry = indices[handle.index];
  if (entry.generation != handle.generation) {
    return u32_max;
  }
  return entry.uploadIndex;
}

[[nodiscard]] const VertexLayoutItem *
findVertexLayoutItem(const VertexLayout &layout, const char *name,
                     const u32 fallbackLocation) {
  for (const auto &item : layout.getItems()) {
    if (item.name == name) {
      return &item;
    }
  }
  for (const auto &item : layout.getItems()) {
    if (item.location == fallbackLocation) {
      return &item;
    }
  }
  return nullptr;
}

[[nodiscard]] Vec4f readVertexAttribute(const u8 *vertex,
                                        const VertexLayoutItem &item,
                                        Vec4f fallback) {
  const auto *attribute = vertex + item.offset;
  switch (item.type) {
  case DataType::Float1: {
    f32 x = 0.0f;
    std::memcpy(&x, attribute, sizeof(f32));
    fallback.x = x;
    return fallback;
  }
  case DataType::Float2: {
    f32 values[2]{};
    std::memcpy(values, attribute, sizeof(values));
    fallback.x = values[0];
    fallback.y = values[1];
    return fallback;
  }
  case DataType::Float3: {
    f32 values[3]{};
    std::memcpy(values, attribute, sizeof(values));
    fallback.x = values[0];
    fallback.y = values[1];
    fallback.z = values[2];
    return fallback;
  }
  case DataType::Float4: {
    f32 values[4]{};
    std::memcpy(values, attribute, sizeof(values));
    return {values[0], values[1], values[2], values[3]};
  }
  case DataType::Int4:
    return fallback;
  }
  return fallback;
}

[[nodiscard]] SceneGpuVertexRecord makeGpuVertexRecord(
    const u8 *vertex, const VertexLayout &layout) {
  SceneGpuVertexRecord record;
  record.position = {0.0f, 0.0f, 0.0f, 1.0f};
  record.normal = {0.0f, 0.0f, 1.0f, 0.0f};
  record.uvTangentSign = {0.0f, 0.0f, 1.0f, 0.0f};
  record.tangent = {1.0f, 0.0f, 0.0f, 1.0f};

  if (const auto *item = findVertexLayoutItem(layout, "inPos", 0)) {
    record.position = readVertexAttribute(vertex, *item, record.position);
    record.position.w = 1.0f;
  }
  if (const auto *item = findVertexLayoutItem(layout, "inNormal", 1)) {
    record.normal = readVertexAttribute(vertex, *item, record.normal);
    record.normal.w = 0.0f;
  }
  if (const auto *item = findVertexLayoutItem(layout, "inUV", 2)) {
    record.uvTangentSign =
        readVertexAttribute(vertex, *item, record.uvTangentSign);
  }
  if (const auto *item = findVertexLayoutItem(layout, "inTangent", 3)) {
    record.tangent = readVertexAttribute(vertex, *item, record.tangent);
    record.uvTangentSign.z = record.tangent.w;
  }
  return record;
}

void appendMeshGeometryRecords(const MeshBuffer &mesh,
                               std::vector<SceneGpuVertexRecord> &vertices,
                               std::vector<u32> &indices) {
  const auto &vertexBuffer = *mesh.getVertexBuffer();
  const auto &layout = vertexBuffer.getLayout();
  const auto stride = layout.getStride();
  const auto *rawVertices =
      static_cast<const u8 *>(vertexBuffer.getRawData());
  if (rawVertices != nullptr && stride != 0) {
    const u32 firstVertex = mesh.getVertexOffset();
    const u32 vertexCount = mesh.getVertexCount();
    vertices.reserve(vertices.size() + vertexCount);
    for (u32 i = 0; i < vertexCount; ++i) {
      const auto *vertex = rawVertices + (firstVertex + i) * stride;
      vertices.push_back(makeGpuVertexRecord(vertex, layout));
    }
  }

  const auto &indexBuffer = *mesh.getIndexBuffer();
  const auto *rawIndices = static_cast<const u32 *>(indexBuffer.getRawData());
  if (rawIndices != nullptr) {
    const u32 firstIndex = mesh.getIndexOffset();
    const u32 indexCount = mesh.getIndexCount();
    const u32 firstVertex = mesh.getVertexOffset();
    indices.reserve(indices.size() + indexCount);
    for (u32 i = 0; i < indexCount; ++i) {
      indices.push_back(rawIndices[firstIndex + i] - firstVertex);
    }
  }
}

[[nodiscard]] bool isMeshSliceValid(const MeshBuffer &mesh) {
  const auto &vertexBuffer = *mesh.getVertexBuffer();
  const auto &indexBuffer = *mesh.getIndexBuffer();
  const u32 vertexOffset = mesh.getVertexOffset();
  const u32 vertexCount = mesh.getVertexCount();
  const u32 indexOffset = mesh.getIndexOffset();
  const u32 indexCount = mesh.getIndexCount();
  if (indexBuffer.getTopology() != PrimitiveTopology::TriangleList) {
    return false;
  }
  if (vertexBuffer.getRawData() == nullptr ||
      indexBuffer.getRawData() == nullptr ||
      vertexBuffer.getLayout().getStride() == 0 || vertexCount == 0 ||
      indexCount == 0 || indexCount % 3 != 0) {
    return false;
  }

  const auto totalVertexCount = vertexBuffer.getVertexCount();
  if (vertexOffset > totalVertexCount ||
      static_cast<usize>(vertexCount) > totalVertexCount - vertexOffset) {
    return false;
  }

  const auto totalIndexCount = indexBuffer.indexCount();
  if (indexOffset > totalIndexCount ||
      static_cast<usize>(indexCount) > totalIndexCount - indexOffset) {
    return false;
  }

  const auto *rawIndices = static_cast<const u32 *>(indexBuffer.getRawData());
  const u32 vertexEnd = vertexOffset + vertexCount;
  for (u32 i = 0; i < indexCount; ++i) {
    const u32 index = rawIndices[indexOffset + i];
    if (index < vertexOffset || index >= vertexEnd) {
      return false;
    }
  }
  return true;
}

} // namespace

template <typename Resource, typename Handle>
Handle SceneResourceTable::add(std::vector<Entry<Resource>> &entries,
                               std::shared_ptr<Resource> resource) {
  assert(resource && "SceneResourceTable cannot register null resource");
  for (u32 i = 0; i < entries.size(); ++i) {
    auto &entry = entries[i];
    if (entry.state == SceneResourceEntryState::Alive) {
      continue;
    }
    entry.resource = std::move(resource);
    entry.generation = nextGeneration(entry.generation);
    entry.state = SceneResourceEntryState::Alive;
    Handle handle;
    handle.index = i;
    handle.generation = entry.generation;
    return handle;
  }

  Entry<Resource> entry;
  entry.resource = std::move(resource);
  entry.generation = 1;
  entry.state = SceneResourceEntryState::Alive;
  entries.push_back(std::move(entry));

  Handle handle;
  handle.index = static_cast<u32>(entries.size() - 1);
  handle.generation = 1;
  return handle;
}

template <typename Resource, typename Handle>
void SceneResourceTable::release(std::vector<Entry<Resource>> &entries,
                                 Handle handle) {
  if (!isAlive(entries, handle)) {
    return;
  }
  auto &entry = entries[handle.index];
  entry.resource.reset();
  entry.generation = nextGeneration(entry.generation);
  entry.state = SceneResourceEntryState::PendingRelease;
}

template <typename Resource, typename Handle>
std::optional<std::reference_wrapper<Resource>>
SceneResourceTable::resolveMutable(std::vector<Entry<Resource>> &entries,
                                   Handle handle) {
  if (!isAlive(entries, handle)) {
    return std::nullopt;
  }
  return std::ref(*entries[handle.index].resource);
}

template <typename Resource, typename Handle>
std::optional<std::reference_wrapper<const Resource>>
SceneResourceTable::resolveConst(const std::vector<Entry<Resource>> &entries,
                                 Handle handle) const {
  if (!isAlive(entries, handle)) {
    return std::nullopt;
  }
  return std::cref(*entries[handle.index].resource);
}

template <typename Resource, typename Handle>
bool SceneResourceTable::isAlive(const std::vector<Entry<Resource>> &entries,
                                 Handle handle) const {
  if (!handle.isValid() || handle.index >= entries.size()) {
    return false;
  }
  const auto &entry = entries[handle.index];
  return entry.state == SceneResourceEntryState::Alive &&
         entry.generation == handle.generation && entry.resource;
}

template <typename Resource>
usize SceneResourceTable::aliveCount(
    const std::vector<Entry<Resource>> &entries) const {
  usize count = 0;
  for (const auto &entry : entries) {
    if (entry.state == SceneResourceEntryState::Alive && entry.resource) {
      ++count;
    }
  }
  return count;
}

void SceneResourceTable::advanceUploadGeneration() {
  m_generation = nextGeneration(m_generation);
}

GeometryStorageHandle SceneResourceTable::registerGeometryStorage(
    GeometryStorageSharedPtr storage) {
  auto handle = add<GeometryStorage, GeometryStorageHandle>(m_geometryStorage,
                                                           std::move(storage));
  advanceUploadGeneration();
  return handle;
}

MeshHandle SceneResourceTable::registerMesh(MeshBufferSharedPtr mesh) {
  auto handle = add<MeshBuffer, MeshHandle>(m_meshes, std::move(mesh));
  advanceUploadGeneration();
  return handle;
}

MaterialHandle
SceneResourceTable::registerMaterial(MaterialInstanceSharedPtr material) {
  auto handle =
      add<MaterialInstance, MaterialHandle>(m_materials, std::move(material));
  advanceUploadGeneration();
  return handle;
}

TextureHandle SceneResourceTable::registerTexture(TextureSharedPtr texture) {
  auto handle = add<Texture, TextureHandle>(m_textures, std::move(texture));
  advanceUploadGeneration();
  return handle;
}

LightHandle SceneResourceTable::registerLight(LightBaseSharedPtr light) {
  auto handle = add<LightBase, LightHandle>(m_lights, std::move(light));
  advanceUploadGeneration();
  return handle;
}

ObjectHandle SceneResourceTable::registerObject(ObjectResource object) {
  auto handle = add<ObjectResource, ObjectHandle>(
      m_objects, std::make_shared<ObjectResource>(std::move(object)));
  advanceUploadGeneration();
  return handle;
}

CameraHandle SceneResourceTable::registerCamera(CameraResource camera) {
  auto handle = add<CameraResource, CameraHandle>(
      m_cameras, std::make_shared<CameraResource>(std::move(camera)));
  advanceUploadGeneration();
  return handle;
}

void SceneResourceTable::updateObject(ObjectHandle handle,
                                      ObjectResource object) {
  auto resolved = resolve(handle);
  if (!resolved.has_value()) {
    return;
  }
  resolved->get() = std::move(object);
  advanceUploadGeneration();
}

void SceneResourceTable::updateCamera(CameraHandle handle,
                                      CameraResource camera) {
  auto resolved = resolve(handle);
  if (!resolved.has_value()) {
    return;
  }
  resolved->get() = std::move(camera);
  advanceUploadGeneration();
}

void SceneResourceTable::release(GeometryStorageHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<GeometryStorage, GeometryStorageHandle>(m_geometryStorage, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(MeshHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<MeshBuffer, MeshHandle>(m_meshes, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(MaterialHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<MaterialInstance, MaterialHandle>(m_materials, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(TextureHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<Texture, TextureHandle>(m_textures, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(LightHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<LightBase, LightHandle>(m_lights, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(ObjectHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<ObjectResource, ObjectHandle>(m_objects, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(CameraHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<CameraResource, CameraHandle>(m_cameras, handle);
  advanceUploadGeneration();
}

std::optional<std::reference_wrapper<GeometryStorage>>
SceneResourceTable::resolve(GeometryStorageHandle handle) {
  return resolveMutable<GeometryStorage, GeometryStorageHandle>(
      m_geometryStorage, handle);
}

std::optional<std::reference_wrapper<const GeometryStorage>>
SceneResourceTable::resolve(GeometryStorageHandle handle) const {
  return resolveConst<GeometryStorage, GeometryStorageHandle>(m_geometryStorage,
                                                             handle);
}

std::optional<std::reference_wrapper<MeshBuffer>>
SceneResourceTable::resolve(MeshHandle handle) {
  return resolveMutable<MeshBuffer, MeshHandle>(m_meshes, handle);
}

std::optional<std::reference_wrapper<const MeshBuffer>>
SceneResourceTable::resolve(MeshHandle handle) const {
  return resolveConst<MeshBuffer, MeshHandle>(m_meshes, handle);
}

std::optional<std::reference_wrapper<MaterialInstance>>
SceneResourceTable::resolve(MaterialHandle handle) {
  return resolveMutable<MaterialInstance, MaterialHandle>(m_materials, handle);
}

std::optional<std::reference_wrapper<const MaterialInstance>>
SceneResourceTable::resolve(MaterialHandle handle) const {
  return resolveConst<MaterialInstance, MaterialHandle>(m_materials, handle);
}

std::optional<std::reference_wrapper<Texture>>
SceneResourceTable::resolve(TextureHandle handle) {
  return resolveMutable<Texture, TextureHandle>(m_textures, handle);
}

std::optional<std::reference_wrapper<const Texture>>
SceneResourceTable::resolve(TextureHandle handle) const {
  return resolveConst<Texture, TextureHandle>(m_textures, handle);
}

std::optional<std::reference_wrapper<LightBase>>
SceneResourceTable::resolve(LightHandle handle) {
  return resolveMutable<LightBase, LightHandle>(m_lights, handle);
}

std::optional<std::reference_wrapper<const LightBase>>
SceneResourceTable::resolve(LightHandle handle) const {
  return resolveConst<LightBase, LightHandle>(m_lights, handle);
}

std::optional<std::reference_wrapper<ObjectResource>>
SceneResourceTable::resolve(ObjectHandle handle) {
  return resolveMutable<ObjectResource, ObjectHandle>(m_objects, handle);
}

std::optional<std::reference_wrapper<const ObjectResource>>
SceneResourceTable::resolve(ObjectHandle handle) const {
  return resolveConst<ObjectResource, ObjectHandle>(m_objects, handle);
}

std::optional<std::reference_wrapper<CameraResource>>
SceneResourceTable::resolve(CameraHandle handle) {
  return resolveMutable<CameraResource, CameraHandle>(m_cameras, handle);
}

std::optional<std::reference_wrapper<const CameraResource>>
SceneResourceTable::resolve(CameraHandle handle) const {
  return resolveConst<CameraResource, CameraHandle>(m_cameras, handle);
}

bool SceneResourceTable::isAlive(GeometryStorageHandle handle) const {
  return isAlive<GeometryStorage, GeometryStorageHandle>(m_geometryStorage,
                                                        handle);
}

bool SceneResourceTable::isAlive(MeshHandle handle) const {
  return isAlive<MeshBuffer, MeshHandle>(m_meshes, handle);
}

bool SceneResourceTable::isAlive(MaterialHandle handle) const {
  return isAlive<MaterialInstance, MaterialHandle>(m_materials, handle);
}

bool SceneResourceTable::isAlive(TextureHandle handle) const {
  return isAlive<Texture, TextureHandle>(m_textures, handle);
}

bool SceneResourceTable::isAlive(LightHandle handle) const {
  return isAlive<LightBase, LightHandle>(m_lights, handle);
}

bool SceneResourceTable::isAlive(ObjectHandle handle) const {
  return isAlive<ObjectResource, ObjectHandle>(m_objects, handle);
}

bool SceneResourceTable::isAlive(CameraHandle handle) const {
  return isAlive<CameraResource, CameraHandle>(m_cameras, handle);
}

usize SceneResourceTable::geometryStorageCount() const {
  return aliveCount(m_geometryStorage);
}

usize SceneResourceTable::meshCount() const { return aliveCount(m_meshes); }

usize SceneResourceTable::materialCount() const {
  return aliveCount(m_materials);
}

usize SceneResourceTable::textureCount() const {
  return aliveCount(m_textures);
}

usize SceneResourceTable::lightCount() const { return aliveCount(m_lights); }

usize SceneResourceTable::objectCount() const { return aliveCount(m_objects); }

usize SceneResourceTable::cameraCount() const { return aliveCount(m_cameras); }

RenderSceneSnapshot SceneResourceTable::buildSnapshot() const {
  RenderSceneSnapshot snapshot;

  for (u32 i = 0; i < m_geometryStorage.size(); ++i) {
    const auto &entry = m_geometryStorage[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    GeometryStorageHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.geometryStorageHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_meshes.size(); ++i) {
    const auto &entry = m_meshes[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    MeshHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.meshHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_materials.size(); ++i) {
    const auto &entry = m_materials[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    MaterialHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.materialHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_textures.size(); ++i) {
    const auto &entry = m_textures[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    TextureHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.textureHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_lights.size(); ++i) {
    const auto &entry = m_lights[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    LightHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.lightHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_cameras.size(); ++i) {
    const auto &entry = m_cameras[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    CameraHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.cameraHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_objects.size(); ++i) {
    const auto &entry = m_objects[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    ObjectHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.objectHandles.push_back(handle);

    const auto &object = *entry.resource;
    snapshot.objects.push_back(ObjectInstanceView{
        .meshIndex = object.mesh.index,
        .materialIndex = object.material.index,
        .objectToWorld = object.objectToWorld,
        .worldToObject = object.worldToObject,
        .worldBounds = object.worldBounds,
        .visibilityMask = object.visibilityMask,
        .visible = object.visible,
    });
  }

  return snapshot;
}

SceneResourceTableUploadView SceneResourceTable::buildUploadView() const {
  const auto makeView = [this]() {
    return SceneResourceTableUploadView{
        .tableGeneration = m_generation,
        .vertices = m_gpuVertices,
        .indices = m_gpuIndices,
        .meshes = m_gpuMeshes,
        .primitives = m_gpuPrimitives,
        .objects = m_gpuObjects,
        .materials = m_gpuMaterials,
    };
  };

  m_gpuVertices.clear();
  m_gpuIndices.clear();
  m_gpuMeshes.clear();
  m_gpuPrimitives.clear();
  m_gpuObjects.clear();
  m_gpuMaterials.clear();

  std::vector<CompactRecordIndex> meshIndexToGpuRecord(m_meshes.size());

  for (u32 i = 0; i < m_meshes.size(); ++i) {
    const auto &entry = m_meshes[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }

    const auto &mesh = *entry.resource;
    if (!isMeshSliceValid(mesh)) {
      continue;
    }
    const SceneGpuMeshRecord record{
        .vertexOffset = static_cast<u32>(m_gpuVertices.size()),
        .indexOffset = static_cast<u32>(m_gpuIndices.size()),
        .indexCount = mesh.getIndexCount(),
        .geometryIndex = i,
    };
    appendMeshGeometryRecords(mesh, m_gpuVertices, m_gpuIndices);
    meshIndexToGpuRecord[i] = CompactRecordIndex{
        .generation = entry.generation,
        .uploadIndex = static_cast<u32>(m_gpuMeshes.size()),
    };
    m_gpuMeshes.push_back(record);
  }

  m_gpuMaterials.reserve(aliveCount(m_materials));
  std::vector<CompactRecordIndex> materialIndexToGpuRecord(m_materials.size());
  for (u32 i = 0; i < m_materials.size(); ++i) {
    const auto &entry = m_materials[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    materialIndexToGpuRecord[i] = CompactRecordIndex{
        .generation = entry.generation,
        .uploadIndex = static_cast<u32>(m_gpuMaterials.size()),
    };
    m_gpuMaterials.push_back(toGpuMaterialRecord(*entry.resource));
  }

  m_gpuObjects.reserve(aliveCount(m_objects));
  for (u32 i = 0; i < m_objects.size(); ++i) {
    const auto &entry = m_objects[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }

    const auto &object = *entry.resource;
    const u32 meshRecordIndex =
        findCompactRecordIndex(meshIndexToGpuRecord, object.mesh);
    const u32 materialRecordIndex =
        findCompactRecordIndex(materialIndexToGpuRecord, object.material);
    if (meshRecordIndex == u32_max || materialRecordIndex == u32_max) {
      continue;
    }
    const u32 objectRecordIndex = static_cast<u32>(m_gpuObjects.size());

    SceneGpuObjectRecord objectRecord;
    objectRecord.objectToWorld = toGpuColumns(object.objectToWorld);
    objectRecord.worldToObject = toGpuColumns(object.worldToObject);
    objectRecord.boundsMin = toGpuBoundsMin(object.worldBounds);
    objectRecord.boundsMax = toGpuBoundsMax(object.worldBounds);
    objectRecord.visible = object.visible ? 1u : 0u;
    objectRecord.flags = object.debugOnly ? 1u : 0u;
    objectRecord.visibilityMask = object.visibilityMask;
    objectRecord.debugId = object.debugId.id;
    m_gpuObjects.push_back(objectRecord);

    const auto &meshRecord = m_gpuMeshes[meshRecordIndex];
    for (u32 triangleIndexOffset = 0;
         triangleIndexOffset + 2 < meshRecord.indexCount;
         triangleIndexOffset += 3) {
      SceneGpuPrimitiveRecord primitiveRecord;
      primitiveRecord.indexOffset = meshRecord.indexOffset + triangleIndexOffset;
      primitiveRecord.meshIndex = meshRecordIndex;
      primitiveRecord.materialIndex = materialRecordIndex;
      primitiveRecord.objectIndex = objectRecordIndex;
      m_gpuPrimitives.push_back(primitiveRecord);
    }
  }

  return makeView();
}

} // namespace LX_core
