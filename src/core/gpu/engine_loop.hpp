#pragma once

#include "core/platform/window.hpp"
#include "core/rhi/renderer.hpp"
#include "core/time/clock.hpp"

#include <optional>
#include <functional>

namespace LX_core::gpu {

class EngineLoop {
public:
  using UpdateHook = std::function<void(Scene &, const Clock &)>;

  void initialize(WindowSharedPtr window, RendererSharedPtr renderer);
  void startScene(SceneSharedPtr scene,
                  std::optional<LiveRenderView> liveRenderView = std::nullopt);
  void setLiveRenderView(std::optional<LiveRenderView> view);
  [[nodiscard]] LiveRenderSubmissionStats liveRenderSubmissionStats() const;
  void setUpdateHook(UpdateHook hook);
  void requestSceneRebuild();
  void tickFrame();
  void run();
  void stop();

  const Clock &getClock() const { return m_clock; }

private:
  void rebuildSceneIfRequested();
  void validateInitialized() const;

  WindowSharedPtr m_window;
  RendererSharedPtr m_renderer;
  SceneSharedPtr m_scene;
  Clock m_clock;
  UpdateHook m_updateHook;
  std::optional<LiveRenderView> m_liveRenderView;
  bool m_running = false;
  bool m_rebuildRequested = false;
};

} // namespace LX_core::gpu
