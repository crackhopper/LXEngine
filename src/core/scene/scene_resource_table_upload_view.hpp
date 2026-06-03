#pragma once

#include "core/scene/scene_gpu_records.hpp"

#include <span>

namespace LX_core {

// Non-owning view over SceneResourceTable's cached GPU records. Spans remain
// valid until the table mutates or a later buildUploadView() call rebuilds the
// cache.
struct SceneResourceTableUploadView final {
  u64 generation = 0;
  std::span<const SceneGpuVertexRecord> vertices;
  std::span<const u32> indices;
  std::span<const SceneGpuMeshRecord> meshes;
  std::span<const SceneGpuPrimitiveRecord> primitives;
  std::span<const SceneGpuObjectRecord> objects;
  std::span<const SceneGpuMaterialRecord> materials;
};

} // namespace LX_core
