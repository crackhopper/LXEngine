#pragma once

#include "core/asset/shader.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <vector>

namespace LX_core {

class Scene;
class MaterialInstance;
[[nodiscard]] DescriptorResourceList
buildSceneMaterialDescriptorResources(const SceneResourceTable &resources,
                                      MaterialHandle material,
                                      const IShaderSharedPtr &shader);

struct SceneDescriptorResourceContext final {
  const Scene &scene;
  const ValidatedRenderablePassData &renderable;
  StringID pass;
  RenderTarget target;
  DescriptorResourceList sceneResources;
};

[[nodiscard]] DescriptorResourceList
buildSceneDescriptorResources(const SceneDescriptorResourceContext &context);

} // namespace LX_core
