#include "core/frame_graph/graph_resource_registry.hpp"

namespace LX_core {

GraphResourceRegistry GraphResourceRegistry::makeDefault() {
  GraphResourceRegistry registry;
  for (const char *name : {
           "depth.main",
           "gbuffer.albedoAlpha",
           "gbuffer.normalRoughness",
           "gbuffer.albedo",
           "gbuffer.normal",
           "gbuffer.material",
           "gbuffer.emissive",
           "hdr.color",
           "ldr.color",
           "bloom.threshold",
           "bloom.blurH",
           "bloom.blur",
           "swapchain.color",
           "debug.overlay",
           "debug.ldr.linear",
           "debug.final.srgb",
           "debug.final.unorm_manual_srgb",
           "debug.ramp.srgb",
           "debug.ramp.unorm_manual_srgb",
           "bake.environment.cubemap",
           "bake.environment.diffuse_sh9",
           "bake.environment.specular_prefilter",
           "bake.material.brdf_lut",
           "shadow.main",
           "shadow.cascade0",
           "shadow.cascade1",
           "shadow.cascade2",
           "shadow.cascade3",
           "environment.radiance",
       }) {
    registry.registerResource(name);
  }
  for (const char *name : {
           "geometry.vertex",
           "geometry.index",
           "material.bsdf",
           "camera.ubo",
           "scene.camera",
           "scene.lights",
           "scene.bvh",
           "scene.environment",
           "scene.environmentBake",
           "scene.materialIblBake",
           "bake.environment.source",
           "bake.material.source",
       }) {
    registry.registerImportedResource(name);
  }
  registry.allowWriteMode("hdr.color", "append");
  registry.allowWriteMode("hdr.color", "blend");
  registry.allowWriteMode("debug.overlay", "append");
  return registry;
}

void GraphResourceRegistry::registerResource(std::string name) {
  m_resources.insert(std::move(name));
}

void GraphResourceRegistry::registerImportedResource(std::string name) {
  m_importedResources.insert(name);
  m_resources.insert(std::move(name));
}

void GraphResourceRegistry::allowWriteMode(std::string resourceName,
                                           std::string writeMode) {
  registerResource(resourceName);
  m_allowedWriteModes.insert(std::move(resourceName) + "\n" +
                             std::move(writeMode));
}

bool GraphResourceRegistry::contains(std::string_view name) const {
  if (name.rfind("feature.", 0) == 0) {
    return true;
  }
  return m_resources.find(std::string(name)) != m_resources.end();
}

bool GraphResourceRegistry::isImported(std::string_view name) const {
  if (name.rfind("feature.", 0) == 0) {
    return true;
  }
  return m_importedResources.find(std::string(name)) !=
         m_importedResources.end();
}

bool GraphResourceRegistry::allowsWriteMode(std::string_view name,
                                            std::string_view writeMode) const {
  return m_allowedWriteModes.find(std::string(name) + "\n" +
                                  std::string(writeMode)) !=
         m_allowedWriteModes.end();
}

} // namespace LX_core
