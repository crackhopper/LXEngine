#pragma once

#include "core/asset/render_effect.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"

#include <optional>
#include <string>

namespace LX_core {

class SceneResourceTable;

struct RenderFeatureDerivedResourceRequest final {
  const RenderFeature *feature = nullptr;
  const RenderFeatureResourceRequirement *resource = nullptr;
  const SceneResourceTable *sceneResources = nullptr;
};

struct RenderFeatureDerivedResourceResult final {
  DescriptorResourceList descriptorResources;
};

class RenderFeatureDerivedResourceProducerRegistry final {
public:
  [[nodiscard]] static std::optional<RenderFeatureDerivedResourceResult>
  build(const RenderFeatureDerivedResourceRequest &request,
        std::string &diagnostic);
};

} // namespace LX_core
