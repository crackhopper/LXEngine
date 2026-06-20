#include "core/offline/offline_render_validation.hpp"

#include "core/raytracing/software_bvh.hpp"

#include <stdexcept>
#include <string>

namespace LX_core::offline {
namespace {

[[nodiscard]] bool hasActiveCamera(const SceneResourceTable &scene) {
  const RenderSceneSnapshot snapshot = scene.buildSnapshot();
  for (const CameraHandle handle : snapshot.cameraHandles) {
    const auto camera = scene.resolve(handle);
    if (camera.has_value() && camera->get().active) {
      return true;
    }
  }
  return false;
}

void validateCountFitsU32(const usize count, const char *label) {
  if (count > static_cast<usize>(u32_max)) {
    throw std::runtime_error("offline render scene " + std::string(label) +
                             " count exceeds u32 range");
  }
}

} // namespace

void validateOfflineUploadView(const SceneResourceTableUploadView &uploadView) {
  if (uploadView.positions.empty()) {
    throw std::runtime_error("offline render scene has no upload positions");
  }
  if (uploadView.indices.empty()) {
    throw std::runtime_error("offline render scene has no upload indices");
  }
  if (uploadView.meshes.empty()) {
    throw std::runtime_error("offline render scene has no upload meshes");
  }
  if (uploadView.primitives.empty()) {
    throw std::runtime_error("offline render scene has no renderable primitives");
  }
  if (uploadView.objects.empty()) {
    throw std::runtime_error("offline render scene has no renderable objects");
  }
  if (uploadView.materials.empty() && uploadView.materialRefs.empty()) {
    throw std::runtime_error("offline render scene has no upload materials");
  }

  validateCountFitsU32(uploadView.positions.size(), "position");
  validateCountFitsU32(uploadView.indices.size(), "index");
  validateCountFitsU32(uploadView.meshes.size(), "mesh");
  validateCountFitsU32(uploadView.primitives.size(), "primitive");
  validateCountFitsU32(uploadView.objects.size(), "object");
  validateCountFitsU32(uploadView.materials.size(), "material");
  validateCountFitsU32(uploadView.materialRefs.size(), "material ref");
  validateCountFitsU32(uploadView.sourceMaterialRecords.size(),
                       "source material record");
}

void validateOfflineRenderInputs(const SceneResourceTable &scene,
                                 const OutputProfile &output) {
  if (output.width == 0 || output.height == 0) {
    throw std::runtime_error(
        "offline render output width/height must be positive");
  }
  if (!hasActiveCamera(scene)) {
    throw std::runtime_error("offline render scene has no active camera");
  }

  const SceneResourceTableUploadView uploadView = scene.buildUploadView();
  validateOfflineUploadView(uploadView);
  const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(uploadView);
  if (bvh.nodes().empty()) {
    throw std::runtime_error("offline render scene produced no BVH nodes");
  }
  validateCountFitsU32(bvh.nodes().size(), "BVH node");
}

} // namespace LX_core::offline
