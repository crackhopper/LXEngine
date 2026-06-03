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
  if (uploadView.vertices.empty()) {
    throw std::runtime_error("offline render scene has no upload vertices");
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
  if (uploadView.materials.empty()) {
    throw std::runtime_error("offline render scene has no upload materials");
  }

  validateCountFitsU32(uploadView.vertices.size(), "vertex");
  validateCountFitsU32(uploadView.indices.size(), "index");
  validateCountFitsU32(uploadView.meshes.size(), "mesh");
  validateCountFitsU32(uploadView.primitives.size(), "primitive");
  validateCountFitsU32(uploadView.objects.size(), "object");
  validateCountFitsU32(uploadView.materials.size(), "material");
}

void validateOfflineRenderJob(const OfflineRenderJob &job) {
  if (job.output.width == 0 || job.output.height == 0) {
    throw std::runtime_error(
        "offline render output width/height must be positive");
  }
  if (!hasActiveCamera(job.scene)) {
    throw std::runtime_error("offline render scene has no active camera");
  }

  const SceneResourceTableUploadView uploadView = job.scene.buildUploadView();
  validateOfflineUploadView(uploadView);
  const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(uploadView);
  if (bvh.nodes().empty()) {
    throw std::runtime_error("offline render scene produced no BVH nodes");
  }
  validateCountFitsU32(bvh.nodes().size(), "BVH node");
}

} // namespace LX_core::offline
