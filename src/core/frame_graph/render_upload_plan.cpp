#include "core/frame_graph/render_upload_plan.hpp"

#include "core/asset/texture.hpp"

#include <memory>
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

void appendTypedInputResources(std::vector<GpuResourceRef> &resources,
                               std::unordered_set<ResourceCacheIdentity> &seen,
                               const RenderInput &input) {
  if (const auto *draw = dynamic_cast<const RenderDrawInput *>(&input)) {
    appendUniqueResource(resources, seen, draw->vertexBuffer);
    appendUniqueResource(resources, seen, draw->indexBuffer);
  }
}

} // namespace

RenderUploadPlan
buildRenderUploadPlan(const std::vector<std::unique_ptr<RenderInput>> &inputs,
                      const std::vector<RenderInputDesc> &descs) {
  RenderUploadPlan plan;
  std::unordered_set<ResourceCacheIdentity> seenResources;

  for (const RenderInputDesc &desc : descs) {
    if (!desc.accepted()) {
      continue;
    }
    for (const DescriptorResourceRef &resource :
         desc.bindingPlan.descriptors) {
      appendUniqueDescriptorResource(plan.resources, seenResources, resource);
    }
    for (const GpuResourceRef &resource : desc.resourceDependencies) {
      appendUniqueResource(plan.resources, seenResources, resource);
    }
    if (desc.inputIndex < inputs.size() && inputs[desc.inputIndex]) {
      appendTypedInputResources(plan.resources, seenResources,
                                *inputs[desc.inputIndex]);
    }
  }

  return plan;
}

} // namespace LX_core
