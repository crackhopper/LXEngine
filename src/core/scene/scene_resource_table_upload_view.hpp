#pragma once

#include "core/asset/texture.hpp"
#include "core/scene/scene_gpu_records.hpp"

#include <span>

namespace LX_core {

// Non-owning view over SceneResourceTable's cached GPU records. Spans remain
// valid until the table mutates or a later buildUploadView() call rebuilds the
// cache. buildUploadView() rebuilds records on each call because shared mesh
// and material resources can mutate without changing the table mutation
// generation.
struct SceneResourceTableUploadView final {
  u64 tableGeneration = 0;
  std::span<const SceneGpuVertexRecord> vertices;
  // Indices are global compact indices into vertices, not mesh-local indices.
  std::span<const u32> indices;
  std::span<const SceneGpuMeshRecord> meshes;
  std::span<const SceneGpuPrimitiveRecord> primitives;
  std::span<const SceneGpuObjectRecord> objects;
  std::span<const SceneGpuMaterialRecord> materials;
  std::span<const CombinedTextureSamplerSharedPtr> textures;
};

} // namespace LX_core
