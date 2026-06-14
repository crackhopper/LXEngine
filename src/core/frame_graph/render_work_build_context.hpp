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
  struct RealtimeOptions final {
    std::optional<RenderTarget> sceneResourceTarget;
    std::optional<CameraResource> cameraResource;
    std::optional<VisibilityLayerMask> visibleMask;
  };

  [[nodiscard]] static RenderWorkBuildContext realtimeEmpty();
  [[nodiscard]] static RenderWorkBuildContext realtime(const Scene &scene);
  [[nodiscard]] static RenderWorkBuildContext realtime(const Scene &scene,
                                                       RealtimeOptions options);
  [[nodiscard]] static RenderWorkBuildContext
  offline(offline::OfflineRenderJob &job);

  [[nodiscard]] RenderDomain domain() const;
  [[nodiscard]] bool hasRealtimeScene() const;
  [[nodiscard]] const Scene &realtimeScene() const;
  [[nodiscard]] const RealtimeOptions &realtimeOptions() const;
  [[nodiscard]] offline::OfflineRenderJob &offlineJob() const;

private:
  using EmptyRealtimeSource = std::monostate;
  using RealtimeSource = std::reference_wrapper<const Scene>;
  using OfflineSource = std::reference_wrapper<offline::OfflineRenderJob>;

  RenderWorkBuildContext();
  explicit RenderWorkBuildContext(RealtimeSource scene);
  RenderWorkBuildContext(RealtimeSource scene, RealtimeOptions options);
  explicit RenderWorkBuildContext(OfflineSource job);

  std::variant<EmptyRealtimeSource, RealtimeSource, OfflineSource> m_source;
  RealtimeOptions m_realtimeOptions;
};

} // namespace LX_core
