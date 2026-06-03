#pragma once

#include "core/scene/scene.hpp"

#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace LX_core {

namespace offline {
struct OfflineRenderJob;
} // namespace offline

class RenderWorkBuildContext final {
public:
  [[nodiscard]] static RenderWorkBuildContext realtime(const Scene &scene);
  [[nodiscard]] static RenderWorkBuildContext
  realtime(const Scene &scene,
           std::vector<IGpuResourceSharedPtr> sceneResources,
           VisibilityLayerMask visibleMask);
  [[nodiscard]] static RenderWorkBuildContext
  offline(const offline::OfflineRenderJob &job);
  [[nodiscard]] static RenderWorkBuildContext
  offline(const offline::OfflineRenderJob &job, IShaderSharedPtr shader);

  [[nodiscard]] RenderDomain domain() const;
  [[nodiscard]] const Scene &realtimeScene() const;
  [[nodiscard]] bool hasRealtimeOverrides() const;
  [[nodiscard]] const std::vector<IGpuResourceSharedPtr> &
  realtimeSceneResources() const;
  [[nodiscard]] VisibilityLayerMask realtimeVisibleMask() const;
  [[nodiscard]] const offline::OfflineRenderJob &offlineJob() const;
  [[nodiscard]] IShaderSharedPtr offlineShader() const;

private:
  using RealtimeSource = std::reference_wrapper<const Scene>;
  using OfflineSource = std::reference_wrapper<const offline::OfflineRenderJob>;

  explicit RenderWorkBuildContext(RealtimeSource scene);
  RenderWorkBuildContext(RealtimeSource scene,
                         std::vector<IGpuResourceSharedPtr> sceneResources,
                         VisibilityLayerMask visibleMask);
  explicit RenderWorkBuildContext(OfflineSource job);
  RenderWorkBuildContext(OfflineSource job, IShaderSharedPtr shader);

  std::variant<RealtimeSource, OfflineSource> m_source;
  std::optional<std::vector<IGpuResourceSharedPtr>> m_sceneResources;
  std::optional<VisibilityLayerMask> m_visibleMask;
  IShaderSharedPtr m_offlineShader;
};

} // namespace LX_core
