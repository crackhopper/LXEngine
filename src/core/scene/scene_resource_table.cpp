#include "core/scene/scene_resource_table.hpp"

#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/texture.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"

#include <cassert>
#include <utility>

namespace LX_core {
namespace {

[[nodiscard]] u32 nextGeneration(const u32 current) {
  const u32 next = current + 1;
  return next == 0 ? 1 : next;
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

GeometryStorageHandle SceneResourceTable::registerGeometryStorage(
    GeometryStorageSharedPtr storage) {
  return add<GeometryStorage, GeometryStorageHandle>(m_geometryStorage,
                                                     std::move(storage));
}

MeshHandle SceneResourceTable::registerMesh(MeshBufferSharedPtr mesh) {
  return add<MeshBuffer, MeshHandle>(m_meshes, std::move(mesh));
}

MaterialHandle
SceneResourceTable::registerMaterial(MaterialInstanceSharedPtr material) {
  return add<MaterialInstance, MaterialHandle>(m_materials, std::move(material));
}

TextureHandle SceneResourceTable::registerTexture(TextureSharedPtr texture) {
  return add<Texture, TextureHandle>(m_textures, std::move(texture));
}

LightHandle SceneResourceTable::registerLight(LightBaseSharedPtr light) {
  return add<LightBase, LightHandle>(m_lights, std::move(light));
}

ObjectHandle SceneResourceTable::registerObject(ObjectResource object) {
  return add<ObjectResource, ObjectHandle>(
      m_objects, std::make_shared<ObjectResource>(std::move(object)));
}

CameraHandle SceneResourceTable::registerCamera(CameraResource camera) {
  return add<CameraResource, CameraHandle>(
      m_cameras, std::make_shared<CameraResource>(std::move(camera)));
}

void SceneResourceTable::updateObject(ObjectHandle handle,
                                      ObjectResource object) {
  auto resolved = resolve(handle);
  if (!resolved.has_value()) {
    return;
  }
  resolved->get() = std::move(object);
}

void SceneResourceTable::updateCamera(CameraHandle handle,
                                      CameraResource camera) {
  auto resolved = resolve(handle);
  if (!resolved.has_value()) {
    return;
  }
  resolved->get() = std::move(camera);
}

void SceneResourceTable::release(GeometryStorageHandle handle) {
  release<GeometryStorage, GeometryStorageHandle>(m_geometryStorage, handle);
}

void SceneResourceTable::release(MeshHandle handle) {
  release<MeshBuffer, MeshHandle>(m_meshes, handle);
}

void SceneResourceTable::release(MaterialHandle handle) {
  release<MaterialInstance, MaterialHandle>(m_materials, handle);
}

void SceneResourceTable::release(TextureHandle handle) {
  release<Texture, TextureHandle>(m_textures, handle);
}

void SceneResourceTable::release(LightHandle handle) {
  release<LightBase, LightHandle>(m_lights, handle);
}

void SceneResourceTable::release(ObjectHandle handle) {
  release<ObjectResource, ObjectHandle>(m_objects, handle);
}

void SceneResourceTable::release(CameraHandle handle) {
  release<CameraResource, CameraHandle>(m_cameras, handle);
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

} // namespace LX_core
