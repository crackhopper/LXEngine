#pragma once

#include "core/scene/scene.hpp"

#include <functional>
#include <variant>

namespace LX_core {

namespace offline {
struct OfflineRenderJob;
}

class RenderWorkBuildContext final {
public:
  [[nodiscard]] static RenderWorkBuildContext realtime(const Scene &scene);
  [[nodiscard]] static RenderWorkBuildContext
  offline(const offline::OfflineRenderJob &job);

  [[nodiscard]] RenderDomain domain() const;
  [[nodiscard]] const Scene &realtimeScene() const;
  [[nodiscard]] const offline::OfflineRenderJob &offlineJob() const;

private:
  using RealtimeSource = std::reference_wrapper<const Scene>;
  using OfflineSource = std::reference_wrapper<const offline::OfflineRenderJob>;

  explicit RenderWorkBuildContext(RealtimeSource scene);
  explicit RenderWorkBuildContext(OfflineSource job);

  std::variant<RealtimeSource, OfflineSource> m_source;
};

} // namespace LX_core
