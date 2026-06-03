#pragma once

#include "core/scene/scene.hpp"

#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace LX_core {

namespace offline {
struct OfflineRenderJob;
}

class RenderWorkBuildContext final {
public:
  [[nodiscard]] static RenderWorkBuildContext realtime(const Scene &scene);
  [[nodiscard]] static RenderWorkBuildContext realtime(
      const Scene &scene, std::vector<IGpuResourceSharedPtr> sceneResources,
      VisibilityLayerMask visibleMask);
  [[nodiscard]] static RenderWorkBuildContext
  offline(const offline::OfflineRenderJob &job);

  [[nodiscard]] RenderDomain domain() const;
  [[nodiscard]] const Scene &realtimeScene() const;
  [[nodiscard]] bool hasRealtimeOverrides() const;
  [[nodiscard]] const std::vector<IGpuResourceSharedPtr> &
  realtimeSceneResources() const;
  [[nodiscard]] VisibilityLayerMask realtimeVisibleMask() const;
  [[nodiscard]] const offline::OfflineRenderJob &offlineJob() const;

private:
  using RealtimeSource = std::reference_wrapper<const Scene>;
  using OfflineSource = std::reference_wrapper<const offline::OfflineRenderJob>;

  explicit RenderWorkBuildContext(RealtimeSource scene);
  RenderWorkBuildContext(RealtimeSource scene,
                         std::vector<IGpuResourceSharedPtr> sceneResources,
                         VisibilityLayerMask visibleMask);
  explicit RenderWorkBuildContext(OfflineSource job);

  std::variant<RealtimeSource, OfflineSource> m_source;
  std::optional<std::vector<IGpuResourceSharedPtr>> m_sceneResources;
  std::optional<VisibilityLayerMask> m_visibleMask;
};

} // namespace LX_core
