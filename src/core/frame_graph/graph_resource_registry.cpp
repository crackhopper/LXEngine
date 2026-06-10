#include "core/frame_graph/graph_resource_registry.hpp"

namespace LX_core {

GraphResourceRegistry GraphResourceRegistry::makeDefault() {
  GraphResourceRegistry registry;
  for (const char *name : {
           "depth.main",
           "gbuffer.albedo",
           "gbuffer.normal",
           "gbuffer.material",
           "gbuffer.emissive",
           "hdr.color",
           "ldr.color",
           "swapchain.color",
           "shadow.main",
           "environment.radiance",
           "geometry.vertex",
           "geometry.index",
           "material.bsdf",
           "camera.ubo",
           "scene.lights",
           "scene.bvh",
           "scene.environment",
       }) {
    registry.registerResource(name);
  }
  return registry;
}

void GraphResourceRegistry::registerResource(std::string name) {
  m_resources.insert(std::move(name));
}

bool GraphResourceRegistry::contains(std::string_view name) const {
  return m_resources.find(std::string(name)) != m_resources.end();
}

} // namespace LX_core
