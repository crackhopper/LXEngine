#pragma once

#include "core/math/bounds.hpp"
#include "core/math/mat.hpp"
#include "core/platform/types.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/ibl_environment.hpp"
#include "core/scene/light.hpp"
#include "core/scene/scene_gpu_records.hpp"
#include "core/scene/scene_resource_handles.hpp"
#include "core/scene/scene_resource_table_upload_view.hpp"
#include "core/scene/visibility_mask.hpp"
#include "core/utils/string_table.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace LX_core {

class CameraComponent;
class GeometryStorage;
class LightBase;
class MaterialInstance;
class MeshBuffer;
class Skeleton;

using GeometryStorageUniquePtr = std::unique_ptr<GeometryStorage>;
using MeshBufferUniquePtr = std::unique_ptr<MeshBuffer>;
using MaterialInstanceUniquePtr = std::unique_ptr<MaterialInstance>;
using CombinedTextureSamplerUniquePtr = std::unique_ptr<CombinedTextureSampler>;
using LightBaseUniquePtr = std::unique_ptr<LightBase>;

enum class SceneResourceEntryState : u8 {
  Empty = 0,
  Alive,
  PendingRelease,
};

struct ObjectResource final {
  MeshHandle mesh;
  MaterialHandle material;
  Mat4f objectToWorld = Mat4f::identity();
  Mat4f worldToObject = Mat4f::identity();
  BoundingBox worldBounds;
  VisibilityLayerMask visibilityMask = VisibilityMask_All;
  StringID debugId;
  bool visible = true;
  bool debugOnly = false;
};

struct CameraResource final {
  CameraPose pose;
  CameraProjection projection;
  Mat4f view = Mat4f::identity();
  Mat4f proj = Mat4f::identity();
  VisibilityLayerMask cullingMask = VisibilityMask_All;
  bool active = true;
};

struct ObjectInstanceView final {
  u32 meshIndex = 0;
  u32 materialIndex = 0;
  Mat4f objectToWorld = Mat4f::identity();
  Mat4f worldToObject = Mat4f::identity();
  BoundingBox worldBounds;
  VisibilityLayerMask visibilityMask = VisibilityMask_All;
  bool visible = true;
};

struct RenderSceneSnapshot final {
  std::vector<GeometryStorageHandle> geometryStorageHandles;
  std::vector<MeshHandle> meshHandles;
  std::vector<MaterialHandle> materialHandles;
  std::vector<TextureHandle> textureHandles;
  std::vector<LightHandle> lightHandles;
  std::vector<SkeletonHandle> skeletonHandles;
  std::vector<CameraHandle> cameraHandles;
  std::vector<ObjectHandle> objectHandles;
  std::vector<ObjectInstanceView> objects;
};

/*
@source_analysis.section SceneResourceTable 统一持有场景渲染资源
`SceneResourceTable` 是 bindless-ready 资源模型的入口。它给长期资源分配带
generation 的 handle，并把资源生命周期集中到 scene 表中。首版保留对现有
`MeshBuffer`、`MaterialInstance` 等类的复用：table entry 是管理壳，不重新定义
mesh/material/texture 的长期数据模型。

外部对象可以保存 handle；访问资源必须通过 `resolve*()` 回到 table，避免 stale
index 在 slot 复用后静默命中新资源。
*/
class SceneResourceTable final {
public:
  [[nodiscard]] GeometryStorageHandle
  registerGeometryStorage(GeometryStorageUniquePtr storage);
  [[nodiscard]] MeshHandle registerMesh(MeshBufferUniquePtr mesh);
  [[nodiscard]] MaterialHandle
  registerMaterial(MaterialInstanceUniquePtr material);
  [[nodiscard]] TextureHandle
  registerTexture(CombinedTextureSamplerUniquePtr texture);
  [[nodiscard]] LightHandle registerLight(LightBaseUniquePtr light);
  [[nodiscard]] SkeletonHandle registerSkeleton(std::unique_ptr<Skeleton> skeleton);
  [[nodiscard]] ObjectHandle registerObject(ObjectResource object);
  [[nodiscard]] CameraHandle registerCamera(CameraResource camera);
  void updateObject(ObjectHandle handle, ObjectResource object);
  void updateCamera(CameraHandle handle, CameraResource camera);

  void release(GeometryStorageHandle handle);
  void release(MeshHandle handle);
  void release(MaterialHandle handle);
  void release(TextureHandle handle);
  void release(LightHandle handle);
  void release(SkeletonHandle handle);
  void release(ObjectHandle handle);
  void release(CameraHandle handle);

  [[nodiscard]] std::optional<std::reference_wrapper<GeometryStorage>>
  resolve(GeometryStorageHandle handle);
  [[nodiscard]] std::optional<std::reference_wrapper<const GeometryStorage>>
  resolve(GeometryStorageHandle handle) const;
  [[nodiscard]] std::optional<std::reference_wrapper<MeshBuffer>>
  resolve(MeshHandle handle);
  [[nodiscard]] std::optional<std::reference_wrapper<const MeshBuffer>>
  resolve(MeshHandle handle) const;
  [[nodiscard]] std::optional<std::reference_wrapper<MaterialInstance>>
  resolve(MaterialHandle handle);
  [[nodiscard]] std::optional<std::reference_wrapper<const MaterialInstance>>
  resolve(MaterialHandle handle) const;
  [[nodiscard]] std::optional<std::reference_wrapper<CombinedTextureSampler>>
  resolve(TextureHandle handle);
  [[nodiscard]] std::optional<
      std::reference_wrapper<const CombinedTextureSampler>>
  resolve(TextureHandle handle) const;
  [[nodiscard]] std::optional<std::reference_wrapper<LightBase>>
  resolve(LightHandle handle);
  [[nodiscard]] std::optional<std::reference_wrapper<const LightBase>>
  resolve(LightHandle handle) const;
  [[nodiscard]] std::optional<std::reference_wrapper<Skeleton>>
  resolve(SkeletonHandle handle);
  [[nodiscard]] std::optional<std::reference_wrapper<const Skeleton>>
  resolve(SkeletonHandle handle) const;
  [[nodiscard]] std::optional<std::reference_wrapper<ObjectResource>>
  resolve(ObjectHandle handle);
  [[nodiscard]] std::optional<std::reference_wrapper<const ObjectResource>>
  resolve(ObjectHandle handle) const;
  [[nodiscard]] std::optional<std::reference_wrapper<CameraResource>>
  resolve(CameraHandle handle);
  [[nodiscard]] std::optional<std::reference_wrapper<const CameraResource>>
  resolve(CameraHandle handle) const;
  [[nodiscard]] GpuResourceRef getCameraUboResource(CameraHandle handle) const;
  [[nodiscard]] GpuResourceRef
  buildRenderCameraUboResource(const CameraResource &camera) const;
  [[nodiscard]] GpuResourceRef
  buildSceneLightsUboResource(const std::vector<LightHandle> &lightHandles,
                              StringID pass) const;
  void setIblEnvironmentResources(IblEnvironmentResources resources);
  [[nodiscard]] const IblEnvironmentResources *
  getIblEnvironmentResourceSet() const;
  [[nodiscard]] IblEnvironmentResources *getMutableIblEnvironmentResources();
  [[nodiscard]] std::vector<GpuResourceRef> getIblEnvironmentResources() const;
  void beginRenderResourceScope();
  [[nodiscard]] MaterialHandle addRenderMaterial(MaterialInstanceUniquePtr material);
  [[nodiscard]] GpuResourceRef
  addRenderGpuResource(std::unique_ptr<IGpuResource> resource) const;
  [[nodiscard]] TextureSamplerRef addRenderTextureSampler(
      CombinedTextureSamplerUniquePtr sampler) const;

  [[nodiscard]] bool isAlive(GeometryStorageHandle handle) const;
  [[nodiscard]] bool isAlive(MeshHandle handle) const;
  [[nodiscard]] bool isAlive(MaterialHandle handle) const;
  [[nodiscard]] bool isAlive(TextureHandle handle) const;
  [[nodiscard]] bool isAlive(LightHandle handle) const;
  [[nodiscard]] bool isAlive(SkeletonHandle handle) const;
  [[nodiscard]] bool isAlive(ObjectHandle handle) const;
  [[nodiscard]] bool isAlive(CameraHandle handle) const;

  [[nodiscard]] usize geometryStorageCount() const;
  [[nodiscard]] usize meshCount() const;
  [[nodiscard]] usize materialCount() const;
  [[nodiscard]] usize textureCount() const;
  [[nodiscard]] usize lightCount() const;
  [[nodiscard]] usize skeletonCount() const;
  [[nodiscard]] usize objectCount() const;
  [[nodiscard]] usize cameraCount() const;
  [[nodiscard]] RenderSceneSnapshot buildSnapshot() const;
  // Returned spans are backed by this table's cached GPU record storage.
  // The view is valid until the next mutating SceneResourceTable call or the
  // next buildUploadView() call. Resources stored in the table can be mutated
  // through table resolution, so buildUploadView() rebuilds records every call
  // even when the table mutation generation is unchanged.
  [[nodiscard]] SceneResourceTableUploadView buildUploadView() const;

private:
  template <typename Resource>
  struct Entry {
    std::unique_ptr<Resource> resource;
    u32 generation = 0;
    SceneResourceEntryState state = SceneResourceEntryState::Empty;
  };

  template <typename Resource, typename Handle>
  [[nodiscard]] Handle add(std::vector<Entry<Resource>> &entries,
                           std::unique_ptr<Resource> resource);

  template <typename Resource, typename Handle>
  void release(std::vector<Entry<Resource>> &entries, Handle handle);

  template <typename Resource, typename Handle>
  [[nodiscard]] std::optional<std::reference_wrapper<Resource>>
  resolveMutable(std::vector<Entry<Resource>> &entries, Handle handle);

  template <typename Resource, typename Handle>
  [[nodiscard]] std::optional<std::reference_wrapper<const Resource>>
  resolveConst(const std::vector<Entry<Resource>> &entries,
               Handle handle) const;

  template <typename Resource, typename Handle>
  [[nodiscard]] bool isAlive(const std::vector<Entry<Resource>> &entries,
                             Handle handle) const;

  template <typename Resource>
  [[nodiscard]] usize
  aliveCount(const std::vector<Entry<Resource>> &entries) const;

  [[nodiscard]] u32 registerUploadTexture(TextureHandle texture) const;

  void advanceUploadGeneration();

  std::vector<Entry<GeometryStorage>> m_geometryStorage;
  std::vector<Entry<MeshBuffer>> m_meshes;
  std::vector<Entry<MaterialInstance>> m_materials;
  std::vector<Entry<CombinedTextureSampler>> m_textures;
  std::vector<Entry<LightBase>> m_lights;
  std::vector<Entry<Skeleton>> m_skeletons;
  std::vector<Entry<ObjectResource>> m_objects;
  std::vector<Entry<CameraResource>> m_cameras;
  mutable std::optional<IblEnvironmentResources> m_iblEnvironmentResources;
  std::vector<CameraDataUniquePtr> m_cameraUbos;
  mutable std::unique_ptr<SceneLightsData> m_sceneLightsUbo =
      std::make_unique<SceneLightsData>();
  u64 m_generation = 0;
  mutable std::vector<SceneGpuVertexRecord> m_gpuVertices;
  mutable std::vector<u32> m_gpuIndices;
  mutable std::vector<SceneGpuMeshRecord> m_gpuMeshes;
  mutable std::vector<SceneGpuPrimitiveRecord> m_gpuPrimitives;
  mutable std::vector<SceneGpuObjectRecord> m_gpuObjects;
  mutable std::vector<SceneGpuMaterialRecord> m_gpuMaterials;
  mutable std::vector<std::reference_wrapper<const CombinedTextureSampler>>
      m_gpuTextures;
  std::vector<MaterialHandle> m_renderMaterialHandles;
  mutable std::vector<std::unique_ptr<IGpuResource>> m_renderGpuResources;
  mutable std::vector<CombinedTextureSamplerUniquePtr>
      m_renderTextureSamplers;
};

} // namespace LX_core
