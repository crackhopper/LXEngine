#include "core/frame_graph/render_upload_plan.hpp"

#include "core/asset/texture.hpp"

#include <unordered_set>

namespace LX_core {
namespace {

void appendUniqueResource(std::vector<GpuResourceRef> &resources,
                          std::unordered_set<ResourceCacheIdentity> &seen,
                          const GpuResourceRef &resource) {
  if (!resource.isValid()) {
    return;
  }
  if (!seen.insert(resource.getBackendCacheIdentity()).second) {
    return;
  }
  resources.push_back(resource);
}

void appendUniqueDescriptorResource(
    std::vector<GpuResourceRef> &resources,
    std::unordered_set<ResourceCacheIdentity> &seen,
    const DescriptorResourceRef &resource) {
  if (resource.isTextureArray()) {
    for (const TextureSamplerRef &texture : resource.textures()) {
      if (!texture.isValid()) {
        continue;
      }
      const ResourceCacheIdentity identity = texture.getBackendCacheIdentity();
      if (!seen.insert(identity).second) {
        continue;
      }
      resources.emplace_back(texture.get());
    }
    return;
  }

  if (!resource.resource().isValid()) {
    return;
  }
  const ResourceCacheIdentity identity =
      resource.resource().getBackendCacheIdentity();
  if (!seen.insert(identity).second) {
    return;
  }
  resources.push_back(resource.resource());
}

} // namespace

RenderUploadPlan buildRenderUploadPlan(const RenderWorkQueue &queue) {
  RenderUploadPlan plan;
  std::unordered_set<ResourceCacheIdentity> seenResources;

  for (const RenderWorkItem &item : queue.getItems()) {
    plan.domain = item.domain;
    if (item.kind == RenderWorkKind::DirectRasterPass) {
      appendUniqueResource(plan.resources, seenResources,
                           item.directRaster.vertexBuffer);
      appendUniqueResource(plan.resources, seenResources,
                           item.directRaster.indexBuffer);
    }

    for (const DescriptorResourceRef &resource : item.descriptorResources) {
      appendUniqueDescriptorResource(plan.resources, seenResources, resource);
    }
  }
  if (queue.nodeContext().has_value()) {
    appendUniqueResource(plan.resources, seenResources,
                         queue.nodeContext()->batchGeometryResources
                             .vertexBuffer);
    appendUniqueResource(plan.resources, seenResources,
                         queue.nodeContext()->batchGeometryResources
                             .indexBuffer);
    for (const DescriptorResourceRef &resource :
         queue.nodeContext()->sceneResources) {
      appendUniqueDescriptorResource(plan.resources, seenResources, resource);
    }
  }

  return plan;
}

} // namespace LX_core
