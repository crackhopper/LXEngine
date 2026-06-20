#pragma once

#include "core/asset/render_effect.hpp"
#include "core/frame_graph/render_input.hpp"
#include "core/asset/shader.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"
#include "core/scene/scene.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace LX_core {

class RenderWorkBuildContext final {
public:
  struct RuntimeExtent final {
    StringID key;
    Vec3u extent{1u, 1u, 1u};
  };

  struct PassPreparationFacts final {
    StringID pass;
    StringID pipelineVariantKey;
    ShaderProgramSet shaderProgram;
    IShaderSharedPtr shaderInfo;
    RenderState renderState;
    DescriptorResourceList descriptorResources;
  };

  struct Options final {
    std::optional<RenderTarget> sceneResourceTarget;
    std::optional<CameraResource> cameraResource;
    std::optional<VisibilityLayerMask> visibleMask;
    Vec3f outputBackgroundColor{0.0f, 0.0f, 0.0f};
    std::vector<RuntimeExtent> runtimeExtents;
    std::vector<RenderFeatureVolatileValue> featureValues;
    std::vector<PassPreparationFacts> passPreparationFacts;
  };

  [[nodiscard]] static RenderWorkBuildContext empty(
      RenderDomain domain = RenderDomain::Realtime);
  [[nodiscard]] static RenderWorkBuildContext forScene(RenderDomain domain,
                                                       const Scene &scene);
  [[nodiscard]] static RenderWorkBuildContext forScene(RenderDomain domain,
                                                       const Scene &scene,
                                                       Options options);

  [[nodiscard]] RenderDomain domain() const;
  [[nodiscard]] bool hasScene() const;
  [[nodiscard]] const Scene &scene() const;
  [[nodiscard]] const SceneResourceTable &resourceTable() const;
  [[nodiscard]] const Options &options() const;
  [[nodiscard]] std::optional<Vec3u> findRuntimeExtent(StringID key) const;
  [[nodiscard]] std::optional<std::reference_wrapper<const RenderFeatureVolatileValue>>
  findFeatureValue(StringID key) const;
  [[nodiscard]] std::optional<std::reference_wrapper<const PassPreparationFacts>>
  findPassPreparationFacts(StringID pass) const;

private:
  RenderWorkBuildContext(RenderDomain domain, const Scene *scene,
                         Options options);

  RenderDomain m_domain = RenderDomain::Realtime;
  const Scene *m_scene = nullptr;
  Options m_options;
};

} // namespace LX_core
