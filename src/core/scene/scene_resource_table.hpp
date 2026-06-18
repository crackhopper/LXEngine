#pragma once

#include "core/asset/render_effect.hpp"
#include "core/asset/shader.hpp"
#include "core/math/bounds.hpp"
#include "core/math/mat.hpp"
#include "core/platform/types.hpp"
#include "core/resource/resource_metadata.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/ibl_bake_keys.hpp"
#include "core/scene/ibl_environment.hpp"
#include "core/scene/light.hpp"
#include "core/scene/scene_environment_node.hpp"
#include "core/scene/scene_gpu_records.hpp"
#include "core/scene/scene_resource_handles.hpp"
#include "core/scene/scene_resource_table_upload_view.hpp"
#include "core/scene/visibility_mask.hpp"
#include "core/utils/string_table.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
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

struct ShaderResourceMetadata final {
  ResourceUri uri;
  ResourceUri canonicalUri;
  std::vector<ResourceUri> sourceUris;
  IShaderSharedPtr payload;
  struct MaterialSourceVariant final {
    StringID materialTypeVariant;
    StringID renderPathNodeSignature;
    ShaderProgramSet shaderProgram;
  };
  std::vector<MaterialSourceVariant> materialSourceVariants;
  bool sourceResolved = false;
  bool requiresMaterialSourceVariant = false;
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

struct SceneResourceGraphExport final {
  std::vector<ResourceIdentityHandle> handles;
  std::vector<ResourceMetadata> resources;

  [[nodiscard]] u32 handleToIndex(ResourceIdentityHandle handle) const {
    for (u32 i = 0; i < handles.size(); ++i) {
      if (handles[i] == handle) {
        return i;
      }
    }
    return u32_max;
  }
};

struct PassFeatureSpecializationValue final {
  std::string parameterName;
  ShaderStage stage = ShaderStage::None;
  u32 constantId = 0;
  ShaderSpecializationValueType type = ShaderSpecializationValueType::Bool;
  u32 valueU32 = 0;
};

struct PassFeatureData final {
  std::string featureName;
  ResourceUri shaderUri;
  std::vector<PassFeatureSpecializationValue> specializationValues;
};

struct SceneEnvironmentRuntimeState final {
  RenderFeatureHandle feature;
  bool nodePresent = false;
  bool bakeRequested = false;
  u64 generation = 0;
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
  SceneResourceTable();
  ~SceneResourceTable();
  SceneResourceTable(const SceneResourceTable &) = delete;
  SceneResourceTable &operator=(const SceneResourceTable &) = delete;
  SceneResourceTable(SceneResourceTable &&) noexcept;
  SceneResourceTable &operator=(SceneResourceTable &&) noexcept;

  [[nodiscard]] GeometryStorageHandle
  registerGeometryStorage(GeometryStorageUniquePtr storage);
  [[nodiscard]] MeshHandle registerMesh(MeshBufferUniquePtr mesh);
  [[nodiscard]] MeshHandle registerMesh(const ResourceUri &uri,
                                        MeshBufferUniquePtr mesh);
  [[nodiscard]] MaterialHandle
  registerMaterial(MaterialInstanceUniquePtr material);
  [[nodiscard]] MaterialHandle
  registerMaterialInstance(const ResourceUri &uri,
                           MaterialInstanceUniquePtr material);
  [[nodiscard]] TextureHandle
  registerTexture(CombinedTextureSamplerUniquePtr texture);
  [[nodiscard]] TextureHandle
  registerTexture(const ResourceUri &uri,
                  CombinedTextureSamplerUniquePtr texture);
  [[nodiscard]] LightHandle registerLight(LightBaseUniquePtr light);
  [[nodiscard]] SkeletonHandle
  registerSkeleton(std::unique_ptr<Skeleton> skeleton);
  [[nodiscard]] ObjectHandle registerObject(ObjectResource object);
  [[nodiscard]] CameraHandle registerCamera(CameraResource camera);
  [[nodiscard]] RenderPathGraphHandle
  registerRenderPathGraph(const ResourceUri &uri, RenderPathGraph graph);
  [[nodiscard]] RenderFeatureHandle
  registerRenderFeature(const ResourceUri &uri, RenderFeature feature);
  [[nodiscard]] ShaderHandle registerShaderResource(
      const ResourceUri &uri, std::vector<ResourceUri> sourceUris,
      IShaderSharedPtr payload, bool requiresMaterialSourceVariant = false);
  void registerMaterialSourceShaderVariant(const ResourceUri &shaderUri,
                                           StringID materialTypeVariant,
                                           StringID renderPathNodeSignature,
                                           ShaderProgramSet shaderProgram);
  void forEachMaterialInstance(
      const std::function<void(MaterialHandle, const MaterialInstance &,
                               const ResourceUri &)> &callback) const;
  void forEachMaterialInstanceMutable(
      const std::function<void(MaterialHandle, MaterialInstance &,
                               const ResourceUri &)> &callback);
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
  void release(RenderPathGraphHandle handle);
  void release(RenderFeatureHandle handle);
  void release(ShaderHandle handle);

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
  [[nodiscard]] std::optional<std::reference_wrapper<RenderPathGraph>>
  resolve(RenderPathGraphHandle handle);
  [[nodiscard]] std::optional<std::reference_wrapper<const RenderPathGraph>>
  resolve(RenderPathGraphHandle handle) const;
  [[nodiscard]] std::optional<std::reference_wrapper<RenderFeature>>
  resolve(RenderFeatureHandle handle);
  [[nodiscard]] std::optional<std::reference_wrapper<const RenderFeature>>
  resolve(RenderFeatureHandle handle) const;
  [[nodiscard]] std::optional<std::reference_wrapper<ShaderResourceMetadata>>
  resolve(ShaderHandle handle);
  [[nodiscard]] std::optional<
      std::reference_wrapper<const ShaderResourceMetadata>>
  resolve(ShaderHandle handle) const;
  [[nodiscard]] const MeshBuffer &mesh(MeshHandle handle) const;
  [[nodiscard]] const MaterialInstance &
  materialInstance(MaterialHandle handle) const;
  [[nodiscard]] const CombinedTextureSampler &
  texture(TextureHandle handle) const;
  [[nodiscard]] bool hasMesh(MeshHandle handle) const;
  [[nodiscard]] bool hasTexture(TextureHandle handle) const;
  [[nodiscard]] std::optional<MeshHandle>
  findMesh(const ResourceUri &uri) const;
  [[nodiscard]] std::optional<TextureHandle>
  findTexture(const ResourceUri &uri) const;
  [[nodiscard]] std::optional<RenderFeatureHandle>
  findRenderFeatureByFeatureName(std::string_view feature) const;
  [[nodiscard]] std::optional<RenderFeatureHandle>
  findRenderFeatureByMetadataHandle(ResourceIdentityHandle handle) const;
  [[nodiscard]] const PassFeatureData *
  findPassFeatureDataByFeatureName(std::string_view feature) const;
  [[nodiscard]] GpuResourceRef getCameraUboResource(CameraHandle handle) const;
  [[nodiscard]] GpuResourceRef
  buildRenderCameraUboResource(const CameraResource &camera) const;
  [[nodiscard]] GpuResourceRef
  buildSceneLightsUboResource(const std::vector<LightHandle> &lightHandles,
                              StringID pass) const;
  void setIblEnvironmentResources(IblEnvironmentResources resources);
  [[nodiscard]] IblEnvironmentActivationResult
  activateIblEnvironment(IblEnvironmentActivationPayload payload);
  [[nodiscard]] std::optional<ActiveIblEnvironmentResources>
  activeIblEnvironment() const;
  [[nodiscard]] const IblEnvironmentResources *
  getIblEnvironmentResourceSet() const;
  [[nodiscard]] IblEnvironmentResources *getMutableIblEnvironmentResources();
  [[nodiscard]] std::vector<GpuResourceRef> getIblEnvironmentResources() const;
  void registerEnvironmentLightingResources(const RenderFeature &feature);
  [[nodiscard]] std::vector<GpuResourceRef>
  getEnvironmentLightingResources() const;
  void setEnvironmentRuntimeState(SceneEnvironmentRuntimeState state);
  [[nodiscard]] std::optional<SceneEnvironmentRuntimeState>
  environmentRuntimeState() const;
  [[nodiscard]] bool hasEnvironmentNode() const;
  void addEnvironmentIblBakeRequest(RenderFeatureHandle feature);
  void setObjectIblBakeMarker(ObjectHandle handle, SceneIblBakeMarker marker);
  [[nodiscard]] IblBakeItemCollection
  collectIblBakeItems(ResourceUri bakeRenderPathUri = ResourceUri{}) const;
  void registerToneMappingResources(const RenderFeature &feature);
  [[nodiscard]] std::vector<GpuResourceRef> getToneMappingResources() const;
  void registerBloomResources(const RenderFeature &feature);
  [[nodiscard]] std::vector<GpuResourceRef> getBloomResources() const;
  void beginRenderResourceScope();
  [[nodiscard]] MaterialHandle
  addRenderMaterial(MaterialInstanceUniquePtr material);
  [[nodiscard]] GpuResourceRef
  addRenderGpuResource(std::unique_ptr<IGpuResource> resource) const;
  [[nodiscard]] TextureSamplerRef
  addRenderTextureSampler(CombinedTextureSamplerUniquePtr sampler) const;

  [[nodiscard]] bool isAlive(GeometryStorageHandle handle) const;
  [[nodiscard]] bool isAlive(MeshHandle handle) const;
  [[nodiscard]] bool isAlive(MaterialHandle handle) const;
  [[nodiscard]] bool isAlive(TextureHandle handle) const;
  [[nodiscard]] bool isAlive(LightHandle handle) const;
  [[nodiscard]] bool isAlive(SkeletonHandle handle) const;
  [[nodiscard]] bool isAlive(ObjectHandle handle) const;
  [[nodiscard]] bool isAlive(CameraHandle handle) const;
  [[nodiscard]] bool isAlive(RenderPathGraphHandle handle) const;
  [[nodiscard]] bool isAlive(RenderFeatureHandle handle) const;
  [[nodiscard]] bool isAlive(ShaderHandle handle) const;
  [[nodiscard]] bool isAlive(IblDiffuseShHandle handle) const;
  [[nodiscard]] bool isAlive(IblSpecularPrefilteredCubemapHandle handle) const;
  [[nodiscard]] bool isAlive(StandardPbrBrdfLutHandle handle) const;

  [[nodiscard]] usize geometryStorageCount() const;
  [[nodiscard]] usize meshCount() const;
  [[nodiscard]] usize materialCount() const;
  [[nodiscard]] usize textureCount() const;
  [[nodiscard]] usize lightCount() const;
  [[nodiscard]] usize skeletonCount() const;
  [[nodiscard]] usize objectCount() const;
  [[nodiscard]] usize cameraCount() const;
  [[nodiscard]] usize renderPathGraphCount() const;
  [[nodiscard]] usize renderFeatureCount() const;
  [[nodiscard]] usize shaderCount() const;
  [[nodiscard]] u64 graphGeneration() const;
  [[nodiscard]] u64 resourceGeneration() const;
  [[nodiscard]] u64 featureGeneration() const;
  [[nodiscard]] u64 descriptorResourceSelectionGeneration() const;
  [[nodiscard]] u64 descriptorUploadGeneration() const;
  [[nodiscard]] u64 volatileUploadGeneration() const;
  [[nodiscard]] u64 uploadGeneration() const;
  void markFeatureRuntimeDirty();
  void markBakedResourceDirty();
  void markCameraSelectionDirty();
  void markLightRuntimeDirty();
  [[nodiscard]] RenderSceneSnapshot buildSnapshot() const;
  // Returned spans are backed by this table's cached GPU record storage.
  // The view is valid until the next mutating SceneResourceTable call or the
  // next buildUploadView() call. Resources stored in the table can be mutated
  // through table resolution, so buildUploadView() rebuilds records every call
  // even when the table mutation generation is unchanged.
  [[nodiscard]] SceneResourceTableUploadView buildUploadView() const;
  [[nodiscard]] ResourceIdentityHandle
  internResourceMetadata(ResourceMetadata metadata);
  [[nodiscard]] const ResourceMetadata *
  findResourceMetadata(ResourceIdentityHandle handle) const;
  [[nodiscard]] ResourceUri resolveUri(const ResourceUri &baseUri,
                                       const ResourceUri &uri) const;
  [[nodiscard]] ResourceIdentityHandle
  loadOrGetResource(SceneResourceType type, const ResourceUri &canonicalUri);
  void registerDependency(ResourceIdentityHandle ownerHandle,
                          ResourceIdentityHandle dependencyHandle);
  void addDependency(ResourceIdentityHandle ownerHandle,
                     ResourceIdentityHandle dependencyHandle);
  void addDependency(ResourceIdentityHandle ownerHandle,
                     RenderPathGraphHandle dependencyHandle);
  void addDependency(RenderPathGraphHandle ownerHandle,
                     RenderFeatureHandle dependencyHandle);
  void addDependency(RenderPathGraphHandle ownerHandle,
                     ShaderHandle dependencyHandle);
  void addDependency(MaterialHandle ownerHandle,
                     TextureHandle dependencyHandle);
  void markDirty(ResourceIdentityHandle handle, std::string reason);
  void markDirty(TextureHandle handle, std::string reason);
  void markDirty(RenderFeatureHandle handle, std::string reason);
  void markDirty(ShaderHandle handle, std::string reason);
  [[nodiscard]] const ResourceMetadata &
  metadata(ResourceIdentityHandle handle) const;
  [[nodiscard]] const ResourceMetadata &metadata(MaterialHandle handle) const;
  [[nodiscard]] const ResourceMetadata &metadata(TextureHandle handle) const;
  [[nodiscard]] const ResourceMetadata &
  metadata(RenderPathGraphHandle handle) const;
  [[nodiscard]] const ResourceMetadata &
  metadata(RenderFeatureHandle handle) const;
  [[nodiscard]] const ResourceMetadata &metadata(ShaderHandle handle) const;
  [[nodiscard]] ResourceIdentityHandle
  metadataHandle(RenderPathGraphHandle handle) const;
  [[nodiscard]] ResourceIdentityHandle
  metadataHandle(RenderFeatureHandle handle) const;
  [[nodiscard]] ResourceIdentityHandle
  metadataHandle(ShaderHandle handle) const;
  [[nodiscard]] ResourceIdentityHandle
  internMaterialInstanceIdentity(const ResourceUri &sourceMaterialUri,
                                 std::string overrideHash);
  [[nodiscard]] SceneResourceGraphExport exportResourceGraph() const;

private:
  template <typename Resource>
  struct Entry {
    std::unique_ptr<Resource> resource;
    ResourceIdentityHandle metadataHandle;
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
  [[nodiscard]] ResourceIdentityHandle
  metadataHandleFor(MeshHandle handle) const;
  [[nodiscard]] ResourceIdentityHandle
  metadataHandleFor(MaterialHandle handle) const;
  [[nodiscard]] ResourceIdentityHandle
  metadataHandleFor(TextureHandle handle) const;
  [[nodiscard]] ResourceIdentityHandle
  metadataHandleFor(RenderPathGraphHandle handle) const;
  [[nodiscard]] ResourceIdentityHandle
  metadataHandleFor(RenderFeatureHandle handle) const;
  [[nodiscard]] ResourceIdentityHandle
  metadataHandleFor(ShaderHandle handle) const;
  [[nodiscard]] ResourceMetadata &
  mutableMetadata(ResourceIdentityHandle handle);
  [[nodiscard]] const ResourceMetadata &
  constMetadata(ResourceIdentityHandle handle) const;
  [[nodiscard]] bool
  hasLiveTypedResourceMetadata(ResourceIdentityHandle handle) const;
  [[nodiscard]] bool
  validateActiveIblEnvironment(const ActiveIblEnvironmentResources &active,
                               std::vector<std::string> &diagnostics) const;

  void advanceUploadGeneration();
  void advanceDescriptorResourceSelectionGeneration();
  void advanceDescriptorUploadGeneration();
  void advanceVolatileUploadGeneration();
  void markDescriptorResourceSelectionDirty();
  void markDescriptorUploadDirty();
  void markVolatileUploadDirty();
  void advanceGraphGeneration();
  void advanceResourceGeneration();
  void advanceFeatureGeneration();
  void registerPassFeatureSpecializationData(const RenderFeature &feature,
                                             const IShader &shader);

  std::vector<Entry<GeometryStorage>> m_geometryStorage;
  std::vector<Entry<MeshBuffer>> m_meshes;
  std::vector<Entry<MaterialInstance>> m_materials;
  std::vector<Entry<CombinedTextureSampler>> m_textures;
  std::vector<Entry<LightBase>> m_lights;
  std::vector<Entry<Skeleton>> m_skeletons;
  std::vector<Entry<ObjectResource>> m_objects;
  std::vector<Entry<CameraResource>> m_cameras;
  std::vector<Entry<RenderPathGraph>> m_renderPathGraphs;
  std::vector<Entry<RenderFeature>> m_renderFeatures;
  std::vector<Entry<ShaderResourceMetadata>> m_shaders;
  std::vector<Entry<IblDiffuseShPayloadResource>> m_iblDiffuseShPayloads;
  std::vector<Entry<IblTexturePayloadResource>>
      m_iblSpecularPrefilteredCubemaps;
  std::vector<Entry<IblTexturePayloadResource>> m_standardPbrBrdfLuts;
  std::optional<ActiveIblEnvironmentResources> m_activeIblEnvironment;
  mutable std::optional<IblEnvironmentResources> m_iblEnvironmentResources;
  CombinedTextureSamplerSharedPtr m_builtinEnvironmentLightingSkyboxMap;
  std::optional<TextureHandle> m_environmentLightingTexture;
  EnvironmentLightingDataUniquePtr m_environmentLightingUbo;
  std::optional<SceneEnvironmentRuntimeState> m_environmentRuntimeState;
  std::vector<RenderFeatureHandle> m_environmentIblBakeRequests;
  std::vector<std::optional<SceneIblBakeMarker>> m_objectIblBakeMarkers;
  ToneMappingDataUniquePtr m_toneMappingUbo;
  BloomDataUniquePtr m_bloomUbo;
  std::vector<PassFeatureData> m_passFeatureData;
  std::vector<CameraDataUniquePtr> m_cameraUbos;
  mutable std::unique_ptr<SceneLightsData> m_sceneLightsUbo =
      std::make_unique<SceneLightsData>();
  u64 m_generation = 0;
  u64 m_graphGeneration = 0;
  u64 m_resourceGeneration = 0;
  u64 m_featureGeneration = 0;
  u64 m_descriptorResourceSelectionGeneration = 0;
  u64 m_descriptorUploadGeneration = 0;
  u64 m_volatileUploadGeneration = 0;
  mutable std::vector<Vec4f> m_gpuPositions;
  mutable std::vector<SceneGpuAttributeStreamRecord> m_gpuAttributeStreams;
  mutable std::vector<Vec4f> m_gpuAttributeValues;
  mutable std::vector<u32> m_gpuIndices;
  mutable std::vector<SceneGpuMeshRecord> m_gpuMeshes;
  mutable std::vector<SceneGpuPrimitiveRecord> m_gpuPrimitives;
  mutable std::vector<SceneGpuDrawRecord> m_gpuDraws;
  mutable std::vector<SceneGpuObjectRecord> m_gpuObjects;
  mutable std::vector<SceneGpuMaterialRecord> m_gpuMaterials;
  mutable std::vector<SceneGpuMaterialRefRecord> m_gpuMaterialRefs;
  mutable std::vector<SourceLocalMaterialRecord> m_gpuSourceMaterialRecords;
  mutable std::vector<SceneSourceLocalMaterialStorageView>
      m_gpuSourceMaterialStorages;
  mutable std::vector<std::reference_wrapper<const CombinedTextureSampler>>
      m_gpuTextures;
  mutable std::vector<std::reference_wrapper<const CameraResource>>
      m_gpuCameras;
  mutable std::vector<std::reference_wrapper<const LightBase>> m_gpuLights;
  mutable std::vector<std::reference_wrapper<const RenderPathGraph>>
      m_gpuRenderPathGraphResources;
  mutable std::vector<std::reference_wrapper<const RenderFeature>>
      m_gpuRenderFeatureResources;
  mutable std::vector<std::reference_wrapper<const ShaderResourceMetadata>>
      m_gpuShaderResources;
  mutable std::vector<std::reference_wrapper<const IblDiffuseShPayloadResource>>
      m_gpuIblDiffuseShPayloads;
  mutable std::vector<std::reference_wrapper<const CombinedTextureSampler>>
      m_gpuIblSpecularPrefilteredCubemaps;
  mutable std::vector<std::reference_wrapper<const CombinedTextureSampler>>
      m_gpuStandardPbrBrdfLuts;
  mutable std::vector<SceneGpuRenderPathGraphRecord> m_gpuRenderPathGraphs;
  mutable std::vector<SceneGpuRenderPathGraphPassRecord>
      m_gpuRenderPathGraphPasses;
  mutable std::vector<SceneGpuRenderPathGraphFeatureRecord>
      m_gpuRenderPathGraphFeatures;
  mutable std::vector<ResourceIdentityHandle> m_gpuRenderPathGraphShaders;
  mutable std::vector<SceneResourceMeshUploadIndex> m_gpuMeshIndexByHandle;
  mutable std::vector<SceneResourceMaterialUploadIndex>
      m_gpuMaterialIndexByHandle;
  mutable std::vector<SceneResourceMaterialRefUploadIndex>
      m_gpuMaterialRefIndexByHandle;
  mutable std::vector<SceneResourceTextureUploadIndex>
      m_gpuTextureIndexByHandle;
  mutable std::vector<SceneResourceObjectUploadIndex> m_gpuObjectIndexByHandle;
  mutable std::vector<SceneResourceCameraUploadIndex> m_gpuCameraIndexByHandle;
  mutable std::vector<SceneResourceLightUploadIndex> m_gpuLightIndexByHandle;
  mutable std::vector<SceneResourceRenderPathGraphUploadIndex>
      m_gpuRenderPathGraphIndexByHandle;
  mutable std::vector<SceneResourceRenderFeatureUploadIndex>
      m_gpuRenderFeatureIndexByHandle;
  mutable std::vector<SceneResourceShaderUploadIndex> m_gpuShaderIndexByHandle;
  mutable std::vector<SceneResourceIblDiffuseShUploadIndex>
      m_gpuIblDiffuseShIndexByHandle;
  mutable std::vector<SceneResourceIblSpecularPrefilteredCubemapUploadIndex>
      m_gpuIblSpecularPrefilteredCubemapIndexByHandle;
  mutable std::vector<SceneResourceStandardPbrBrdfLutUploadIndex>
      m_gpuStandardPbrBrdfLutIndexByHandle;
  std::vector<MaterialHandle> m_renderMaterialHandles;
  std::vector<ResourceMetadata> m_resourceMetadata;
  std::vector<u32> m_resourceMetadataGenerations;
  mutable std::vector<std::unique_ptr<IGpuResource>> m_renderGpuResources;
  mutable std::vector<CombinedTextureSamplerUniquePtr> m_renderTextureSamplers;
};

} // namespace LX_core
