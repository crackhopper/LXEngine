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

void appendUniquePushConstant(std::vector<PerDrawDataSharedPtr> &pushConstants,
                              std::unordered_set<const PerDrawData *> &seen,
                              const PerDrawDataSharedPtr &pushConstant) {
  if (!pushConstant) {
    return;
  }
  if (!seen.insert(pushConstant.get()).second) {
    return;
  }
  pushConstants.push_back(pushConstant);
}

} // namespace

RenderUploadPlan buildRenderUploadPlan(const RenderWorkQueue &queue) {
  RenderUploadPlan plan;
  std::unordered_set<ResourceCacheIdentity> seenResources;
  std::unordered_set<const PerDrawData *> seenPushConstants;

  for (const RenderWorkItem &item : queue.getItems()) {
    plan.domain = item.domain;
    if (item.kind == RenderWorkKind::RasterDraw ||
        item.kind == RenderWorkKind::RasterBatch) {
      appendUniqueResource(plan.resources, seenResources,
                           item.raster.vertexBuffer);
      appendUniqueResource(plan.resources, seenResources,
                           item.raster.indexBuffer);
      appendUniquePushConstant(plan.pushConstants, seenPushConstants,
                               item.raster.drawData);
    }

    for (const DescriptorResourceRef &resource : item.descriptorResources) {
      appendUniqueDescriptorResource(plan.resources, seenResources, resource);
    }
  }

  return plan;
}

} // namespace LX_core
