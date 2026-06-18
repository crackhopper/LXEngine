#pragma once

#include "core/asset/material_contract_packer.hpp"
#include "core/asset/texture.hpp"
#include "core/resource/resource_metadata.hpp"
#include "core/scene/ibl_environment.hpp"
#include "core/scene/scene_gpu_records.hpp"
#include "core/scene/scene_resource_handles.hpp"
#include "core/utils/string_table.hpp"

#include <functional>
#include <span>
#include <string>

namespace LX_core {

struct CameraResource;
class LightBase;
struct RenderFeature;
struct RenderPathGraph;

struct SceneResourceMeshUploadIndex final {
  MeshHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceMaterialUploadIndex final {
  MaterialHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceMaterialRefUploadIndex final {
  MaterialHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceTextureUploadIndex final {
  TextureHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceObjectUploadIndex final {
  ObjectHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceCameraUploadIndex final {
  CameraHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceLightUploadIndex final {
  LightHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceRenderPathGraphUploadIndex final {
  RenderPathGraphHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceRenderFeatureUploadIndex final {
  RenderFeatureHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceShaderUploadIndex final {
  ShaderHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceIblDiffuseShUploadIndex final {
  IblDiffuseShHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceIblSpecularPrefilteredCubemapUploadIndex final {
  IblSpecularPrefilteredCubemapHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceStandardPbrBrdfLutUploadIndex final {
  StandardPbrBrdfLutHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneActiveIblUploadState final {
  IblDiffuseShHandle diffuseSh;
  IblSpecularPrefilteredCubemapHandle specularPrefilteredCubemap;
  StandardPbrBrdfLutHandle standardPbrBrdfLut;
};

struct SceneSourceLocalMaterialStorageView final {
  StringID sourceSignature;
  ResourceUri sourceUri;
  std::string reflectionHash;
  std::string storageAbiHash;
  u32 recordOffset = 0;
  u32 recordCount = 0;
};

struct ShaderResourceMetadata;

// Non-owning view over SceneResourceTable's cached GPU records. Spans remain
// valid until the table mutates or a later buildUploadView() call rebuilds the
// cache. buildUploadView() rebuilds records on each call because scene-owned
// resources may mutate through table resolution without changing the table
// mutation generation.
struct SceneResourceTableUploadView final {
  u64 tableGeneration = 0;
  // Bindless geometry streams: position-only vertex input plus optional
  // attribute streams addressable from mesh/draw data.
  std::span<const Vec4f> positions;
  std::span<const SceneGpuAttributeStreamRecord> attributeStreams;
  std::span<const Vec4f> attributeValues;
  // Indices are global compact indices into positions, not mesh-local indices.
  std::span<const u32> indices;
  std::span<const SceneGpuMeshRecord> meshes;
  std::span<const SceneGpuPrimitiveRecord> primitives;
  std::span<const SceneGpuDrawRecord> draws;
  std::span<const SceneGpuObjectRecord> objects;
  std::span<const SceneGpuMaterialRecord> materials;
  std::span<const SceneGpuMaterialRefRecord> materialRefs;
  std::span<const SourceLocalMaterialRecord> sourceMaterialRecords;
  std::span<const SceneSourceLocalMaterialStorageView> sourceMaterialStorages;
  std::span<const std::reference_wrapper<const CombinedTextureSampler>>
      textures;
  std::span<const std::reference_wrapper<const CameraResource>> cameras;
  std::span<const std::reference_wrapper<const LightBase>> lights;
  std::span<const std::reference_wrapper<const RenderPathGraph>>
      renderPathGraphResources;
  std::span<const std::reference_wrapper<const RenderFeature>>
      renderFeatureResources;
  std::span<const std::reference_wrapper<const ShaderResourceMetadata>>
      shaderResources;
  std::span<const std::reference_wrapper<const IblDiffuseShPayloadResource>>
      environmentDiffuseShPayloads;
  std::span<const std::reference_wrapper<const CombinedTextureSampler>>
      environmentSpecularPrefilteredCubemaps;
  std::span<const std::reference_wrapper<const CombinedTextureSampler>>
      standardPbrBrdfLuts;
  std::span<const SceneGpuRenderPathGraphRecord> renderPathGraphs;
  std::span<const SceneGpuRenderPathGraphPassRecord> renderPathGraphPasses;
  std::span<const SceneGpuRenderPathGraphFeatureRecord> renderPathGraphFeatures;
  std::span<const ResourceIdentityHandle> renderPathGraphShaders;
  std::span<const SceneResourceMeshUploadIndex> meshIndexByHandle;
  std::span<const SceneResourceMaterialUploadIndex> materialIndexByHandle;
  std::span<const SceneResourceMaterialRefUploadIndex> materialRefIndexByHandle;
  std::span<const SceneResourceTextureUploadIndex> textureIndexByHandle;
  std::span<const SceneResourceObjectUploadIndex> objectIndexByHandle;
  std::span<const SceneResourceCameraUploadIndex> cameraIndexByHandle;
  std::span<const SceneResourceLightUploadIndex> lightIndexByHandle;
  std::span<const SceneResourceRenderPathGraphUploadIndex>
      renderPathGraphIndexByHandle;
  std::span<const SceneResourceRenderFeatureUploadIndex>
      renderFeatureIndexByHandle;
  std::span<const SceneResourceShaderUploadIndex> shaderIndexByHandle;
  std::span<const SceneResourceIblDiffuseShUploadIndex>
      iblDiffuseShIndexByHandle;
  std::span<const SceneResourceIblSpecularPrefilteredCubemapUploadIndex>
      iblSpecularPrefilteredCubemapIndexByHandle;
  std::span<const SceneResourceStandardPbrBrdfLutUploadIndex>
      standardPbrBrdfLutIndexByHandle;
  u64 activeIblGeneration = 0;
  SceneActiveIblUploadState activeIbl;
};

} // namespace LX_core
