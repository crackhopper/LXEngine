#pragma once

#include "core/asset/texture.hpp"
#include "core/scene/scene_gpu_records.hpp"
#include "core/scene/scene_resource_handles.hpp"

#include <functional>
#include <span>

namespace LX_core {

struct CameraResource;
class LightBase;

struct SceneResourceMeshUploadIndex final {
  MeshHandle handle;
  u32 typedIndex = u32_max;
};

struct SceneResourceMaterialUploadIndex final {
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
  std::span<const SceneGpuObjectRecord> objects;
  std::span<const SceneGpuMaterialRecord> materials;
  std::span<const std::reference_wrapper<const CombinedTextureSampler>>
      textures;
  std::span<const std::reference_wrapper<const CameraResource>> cameras;
  std::span<const std::reference_wrapper<const LightBase>> lights;
  std::span<const SceneResourceMeshUploadIndex> meshIndexByHandle;
  std::span<const SceneResourceMaterialUploadIndex> materialIndexByHandle;
  std::span<const SceneResourceTextureUploadIndex> textureIndexByHandle;
  std::span<const SceneResourceObjectUploadIndex> objectIndexByHandle;
  std::span<const SceneResourceCameraUploadIndex> cameraIndexByHandle;
  std::span<const SceneResourceLightUploadIndex> lightIndexByHandle;
};

} // namespace LX_core
