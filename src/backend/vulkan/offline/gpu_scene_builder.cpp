#include "backend/vulkan/offline/gpu_scene_builder.hpp"

#include <cmath>
#include <stdexcept>

namespace LX_core::backend::offline {
namespace {

constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] Vec4f vec4(const Vec3f &v, float w) {
  return Vec4f{v.x, v.y, v.z, w};
}

} // namespace

GpuSceneData GpuSceneBuilder::build(
    const LX_core::offline::OfflineSceneIR &scene,
    const LX_core::offline::OfflineRenderProfile &profile) const {
  if (scene.materials.empty()) {
    throw std::runtime_error("offline GPU scene requires at least one material");
  }

  GpuSceneData out;
  out.materials.reserve(scene.materials.size());
  for (const auto &material : scene.materials) {
    GpuMaterial gpuMaterial;
    gpuMaterial.baseColor =
        Vec4f{material.baseColor.x, material.baseColor.y, material.baseColor.z,
              1.0f};
    gpuMaterial.params = Vec4f{material.metallic, material.roughness, 0.0f, 0.0f};
    gpuMaterial.emissive =
        Vec4f{material.emissive.x, material.emissive.y, material.emissive.z,
              0.0f};
    out.materials.push_back(gpuMaterial);
  }

  for (u32 instanceIndex = 0; instanceIndex < scene.instances.size();
       ++instanceIndex) {
    const auto &instance = scene.instances[instanceIndex];
    if (!instance.visible) {
      continue;
    }
    if (instance.meshIndex >= scene.meshes.size() ||
        instance.materialIndex >= scene.materials.size()) {
      throw std::runtime_error("offline instance references invalid mesh/material");
    }
    const auto &mesh = scene.meshes[instance.meshIndex];
    if ((mesh.indices.size() % 3) != 0) {
      throw std::runtime_error("offline mesh index buffer is not triangle aligned");
    }
    for (usize i = 0; i < mesh.indices.size(); i += 3) {
      const auto &a = mesh.vertices.at(mesh.indices[i + 0]);
      const auto &b = mesh.vertices.at(mesh.indices[i + 1]);
      const auto &c = mesh.vertices.at(mesh.indices[i + 2]);
      const Vec3f v0 =
          LX_core::offline::transformPoint(instance.worldTransform, a.position);
      const Vec3f v1 =
          LX_core::offline::transformPoint(instance.worldTransform, b.position);
      const Vec3f v2 =
          LX_core::offline::transformPoint(instance.worldTransform, c.position);
      Vec3f normal = (v1 - v0).cross(v2 - v0).normalized();
      if (normal.length2() == 0.0f) {
        normal = LX_core::offline::transformVector(instance.worldTransform,
                                                   a.normal)
                     .normalized();
      }
      out.triangles.push_back(GpuTriangle{
          .v0 = vec4(v0, 0.0f),
          .v1 = vec4(v1, 0.0f),
          .v2 = vec4(v2, 0.0f),
          .normal = vec4(normal, 0.0f),
          .materialIndex = instance.materialIndex,
          .objectIndex = instanceIndex,
      });
    }
  }
  if (out.triangles.empty()) {
    throw std::runtime_error("offline GPU scene has no visible triangles");
  }

  const auto &camera = scene.camera;
  Vec3f forward = (camera.target - camera.eye).normalized();
  if (forward.length2() == 0.0f) {
    forward = Vec3f{0.0f, 0.0f, -1.0f};
  }
  Vec3f right = forward.cross(camera.up).normalized();
  if (right.length2() == 0.0f) {
    right = Vec3f{1.0f, 0.0f, 0.0f};
  }
  const Vec3f up = right.cross(forward).normalized();
  const float aspect =
      profile.height == 0
          ? camera.aspect
          : static_cast<float>(profile.width) / static_cast<float>(profile.height);
  const float tanHalfFov =
      std::tan(camera.fovYDegrees * (kPi / 180.0f) * 0.5f);

  const auto light =
      scene.directionalLights.empty()
          ? LX_core::offline::OfflineDirectionalLightIR{}
          : scene.directionalLights.front();
  out.params.eye = vec4(camera.eye, 0.0f);
  out.params.cameraRight = vec4(right * (tanHalfFov * aspect), 0.0f);
  out.params.cameraUp = vec4(up * tanHalfFov, 0.0f);
  out.params.cameraForward = vec4(forward, 0.0f);
  out.params.lightDirectionIntensity =
      vec4(light.direction.normalized(), light.intensity);
  out.params.lightColorEnvironment =
      Vec4f{light.color.x, light.color.y, light.color.z,
            scene.environment.enabled ? scene.environment.intensity : 0.35f};
  out.params.width = profile.width;
  out.params.height = profile.height;
  out.params.samples = profile.samples;
  out.params.seed = profile.seed;
  out.params.triangleCount = static_cast<u32>(out.triangles.size());
  out.params.materialCount = static_cast<u32>(out.materials.size());
  return out;
}

} // namespace LX_core::backend::offline
