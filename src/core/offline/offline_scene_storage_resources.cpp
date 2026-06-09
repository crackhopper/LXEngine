#include "core/offline/offline_scene_storage_resources.hpp"

#include "core/math/vec.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "core/offline/offline_render_validation.hpp"
#include "core/raytracing/software_bvh.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/scene/scene_resource_table_upload_view.hpp"

#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <typeinfo>

namespace LX_core::offline {
namespace {

constexpr usize kOfflineSceneTextureDescriptorCount = 256;

struct DirectionalLightParams final {
  Vec3f direction{0.0f, -1.0f, 0.0f};
  Vec3f color{0.0f, 0.0f, 0.0f};
  float intensity = 0.0f;
};

class OfflineStorageBufferResource final : public IGpuResource {
public:
  OfflineStorageBufferResource(StringID bindingName,
                               std::vector<std::byte> bytes)
      : m_bindingName(bindingName), m_bytes(std::move(bytes)) {
    setDirty();
  }

  ResourceType getType() const override { return ResourceType::StorageBuffer; }
  const void *getRawData() const override { return m_bytes.data(); }
  u32 getByteSize() const override { return static_cast<u32>(m_bytes.size()); }
  StringID getBindingName() const override { return m_bindingName; }

private:
  StringID m_bindingName;
  std::vector<std::byte> m_bytes;
};

template <typename T>
std::vector<std::byte> copyBytes(std::span<const T> values) {
  std::vector<std::byte> bytes(sizeof(T) * values.size());
  if (!bytes.empty()) {
    std::memcpy(bytes.data(), values.data(), bytes.size());
  }
  return bytes;
}

template <typename T>
std::vector<std::byte> copyBytes(const std::vector<T> &values) {
  return copyBytes(std::span<const T>{values.data(), values.size()});
}

template <typename T>
std::vector<std::byte> copyObjectBytes(const T &value) {
  std::vector<std::byte> bytes(sizeof(T));
  std::memcpy(bytes.data(), &value, sizeof(T));
  return bytes;
}

std::vector<std::byte> zeroBytes(usize byteSize) {
  return std::vector<std::byte>(byteSize);
}

std::unique_ptr<IGpuResource> makeStorageBuffer(StringID bindingName,
                                                std::vector<std::byte> bytes) {
  return std::make_unique<OfflineStorageBufferResource>(bindingName,
                                                        std::move(bytes));
}

DescriptorResourceRef makeSceneTextureArray(
    std::span<const std::reference_wrapper<const CombinedTextureSampler>>
        uploadTextures,
    const CombinedTextureSampler &paddingTexture) {
  if (uploadTextures.size() > kOfflineSceneTextureDescriptorCount) {
    throw std::runtime_error("offline PBR scene texture descriptor array "
                             "supports at most 256 textures");
  }

  std::vector<TextureSamplerRef> textures;
  textures.reserve(kOfflineSceneTextureDescriptorCount);
  for (const auto &texture : uploadTextures) {
    textures.emplace_back(texture.get());
  }
  while (textures.size() < kOfflineSceneTextureDescriptorCount) {
    textures.emplace_back(paddingTexture);
  }

  return DescriptorResourceRef::textureArray(StringID("SceneTextures"),
                                             std::move(textures));
}

void appendStorageDescriptor(OfflineSceneStorageResources &resources,
                             const SceneResourceTable &scene,
                             StringID bindingName,
                             std::vector<std::byte> bytes) {
  const GpuResourceRef resource =
      scene.addRenderGpuResource(makeStorageBuffer(bindingName,
                                                   std::move(bytes)));
  if (resource.isValid()) {
    resources.descriptorResources.emplace_back(resource.get());
  }
}

[[nodiscard]] Vec4f vec4(const Vec3f &value, const float w) {
  return Vec4f{value.x, value.y, value.z, w};
}

[[nodiscard]] std::optional<std::reference_wrapper<const CameraResource>>
findActiveCamera(const SceneResourceTable &scene) {
  const RenderSceneSnapshot snapshot = scene.buildSnapshot();
  for (const CameraHandle handle : snapshot.cameraHandles) {
    auto camera = scene.resolve(handle);
    if (camera.has_value() && camera->get().active) {
      return std::cref(camera->get());
    }
  }
  return std::nullopt;
}

[[nodiscard]] CameraRayFrame
makeRayFrameFromCameraResource(const CameraResource &camera,
                               const OutputProfile &output) {
  const CameraProjection projection =
      resolveOutputCameraProjection(camera.projection, output);
  return makeCameraRayFrame(camera.pose, projection);
}

[[nodiscard]] DirectionalLightParams
findFirstDirectionalLight(const SceneResourceTable &scene) {
  const RenderSceneSnapshot snapshot = scene.buildSnapshot();
  for (const LightHandle handle : snapshot.lightHandles) {
    auto light = scene.resolve(handle);
    if (!light.has_value()) {
      continue;
    }
    try {
      const auto &directional =
          dynamic_cast<const DirectionalLight &>(light->get());
      return DirectionalLightParams{
          .direction = directional.getDirection().normalized(),
          .color = directional.getColor(),
          .intensity = directional.getIntensity(),
      };
    } catch (const std::bad_cast &) {}
  }
  return {};
}

[[nodiscard]] std::vector<SceneGpuPrimitiveRecord>
makeShaderPrimitives(const SceneResourceTableUploadView &uploadView,
                     const SceneSoftwareBvh &bvh) {
  std::vector<SceneGpuPrimitiveRecord> primitives;
  primitives.reserve(bvh.primitives().size());
  for (const SceneSoftwareBvhPrimitive &primitive : bvh.primitives()) {
    if (primitive.primitiveIndex >= uploadView.primitives.size()) {
      throw std::runtime_error(
          "software BVH primitive references invalid upload primitive");
    }
    primitives.push_back(uploadView.primitives[primitive.primitiveIndex]);
  }
  return primitives;
}

[[nodiscard]] SceneGpuFrameParams
makeShaderParams(const OfflineRenderJob &job,
                 const SceneResourceTableUploadView &uploadView,
                 const SceneSoftwareBvh &bvh) {
  const auto camera = findActiveCamera(job.scene);
  if (!camera.has_value()) {
    throw std::runtime_error("offline render scene has no active camera");
  }
  const CameraRayFrame rayFrame =
      makeRayFrameFromCameraResource(camera->get(), job.output);
  const DirectionalLightParams light = findFirstDirectionalLight(job.scene);

  SceneGpuFrameParams params;
  params.eye = vec4(rayFrame.eye, 0.0f);
  params.cameraRight = vec4(rayFrame.right, 0.0f);
  params.cameraUp = vec4(rayFrame.up, 0.0f);
  params.cameraForward = vec4(rayFrame.forward, 0.0f);
  params.lightDirectionIntensity =
      vec4(light.direction.normalized(), light.intensity);
  params.lightColorEnvironment =
      Vec4f{light.color.x, light.color.y, light.color.z, 0.0f};
  params.backgroundColor =
      Vec4f{job.output.backgroundColor.x, job.output.backgroundColor.y,
            job.output.backgroundColor.z, 1.0f};
  params.width = job.output.width;
  params.height = job.output.height;
  params.samples = job.offline.samples;
  params.seed = job.offline.seed;
  params.primitiveCount = static_cast<u32>(bvh.primitiveCount());
  params.bvhNodeCount = static_cast<u32>(bvh.nodes().size());
  params.materialCount = static_cast<u32>(uploadView.materials.size());
  params.maxBounce = job.offline.maxBounce;
  params.shadowsEnabled = job.offline.shadows ? 1u : 0u;
  params.compareMode = job.offline.compareMode == "albedo" ? 1u : 0u;
  return params;
}

} // namespace

OfflineSceneStorageResources
buildOfflineSceneStorageResources(OfflineRenderJob &job) {
  const SceneResourceTableUploadView uploadView = job.scene.buildUploadView();
  validateOfflineUploadView(uploadView);
  const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(uploadView);
  const std::vector<SceneGpuPrimitiveRecord> shaderPrimitives =
      makeShaderPrimitives(uploadView, bvh);
  const SceneGpuFrameParams params = makeShaderParams(job, uploadView, bvh);

  OfflineSceneStorageResources resources;
  job.scene.beginRenderResourceScope();
  resources.descriptorResources.reserve(10);
  appendStorageDescriptor(resources, job.scene, StringID("SceneVertices"),
                          copyBytes(uploadView.vertices));
  appendStorageDescriptor(resources, job.scene, StringID("SceneIndices"),
                          copyBytes(uploadView.indices));
  appendStorageDescriptor(resources, job.scene, StringID("SceneMeshes"),
                          copyBytes(uploadView.meshes));
  appendStorageDescriptor(resources, job.scene, StringID("ScenePrimitives"),
                          copyBytes(shaderPrimitives));
  appendStorageDescriptor(resources, job.scene, StringID("SceneObjects"),
                          copyBytes(uploadView.objects));
  appendStorageDescriptor(resources, job.scene, StringID("SceneMaterials"),
                          copyBytes(uploadView.materials));
  appendStorageDescriptor(resources, job.scene, StringID("SceneBvhNodes"),
                          copyBytes(bvh.nodes()));
  appendStorageDescriptor(resources, job.scene, StringID("SceneFrameParams"),
                          copyObjectBytes(params));

  const usize outputSize = static_cast<usize>(job.output.width) *
                           static_cast<usize>(job.output.height) *
                           sizeof(Vec4f);
  resources.outputPixels =
      job.scene.addRenderGpuResource(makeStorageBuffer(
          StringID("OutputPixels"), zeroBytes(outputSize)));
  if (resources.outputPixels.isValid()) {
    resources.descriptorResources.emplace_back(resources.outputPixels.get());
  }
  TextureSamplerRef paddingTexture;
  if (!uploadView.textures.empty()) {
    paddingTexture = TextureSamplerRef{uploadView.textures.front().get()};
  } else {
    paddingTexture = job.scene.addRenderTextureSampler(
        std::make_unique<CombinedTextureSampler>(createWhiteTexture()));
  }
  if (!paddingTexture.isValid()) {
    throw std::runtime_error("offline scene texture descriptor padding missing");
  }
  resources.descriptorResources.push_back(
      makeSceneTextureArray(uploadView.textures, paddingTexture.get()));
  return resources;
}

} // namespace LX_core::offline
