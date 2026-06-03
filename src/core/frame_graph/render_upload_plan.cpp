#include "core/frame_graph/render_upload_plan.hpp"

#include "core/asset/texture.hpp"

#include <unordered_set>

namespace LX_core {
namespace {

void appendUniqueResource(std::vector<IGpuResourceSharedPtr> &resources,
                          std::unordered_set<ResourceCacheIdentity> &seen,
                          const IGpuResourceSharedPtr &resource) {
  if (!resource) {
    return;
  }
  if (!seen.insert(resource->getBackendCacheIdentity()).second) {
    return;
  }
  resources.push_back(resource);
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

    for (const IGpuResourceSharedPtr &resource : item.descriptorResources) {
      appendUniqueResource(plan.resources, seenResources, resource);
      if (const auto textureArray =
              std::dynamic_pointer_cast<SampledTextureArrayResource>(
                  resource)) {
        for (const CombinedTextureSamplerSharedPtr &texture :
             textureArray->textures()) {
          appendUniqueResource(plan.resources, seenResources, texture);
        }
      }
    }
  }

  return plan;
}

} // namespace LX_core
