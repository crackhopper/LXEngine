#pragma once

#include "core/platform/window.hpp"
#include "core/rhi/renderer.hpp"
#include "core/time/clock.hpp"

#include <functional>

namespace LX_core::gpu {

class EngineLoop {
public:
  using UpdateHook = std::function<void(Scene &, const Clock &)>;

  void initialize(WindowSharedPtr window, RendererSharedPtr renderer);
  void startScene(SceneSharedPtr scene);
  void setUpdateHook(UpdateHook hook);
  void requestSceneRebuild();
  void tickFrame();
  void run();
  void stop();

  const Clock &getClock() const { return m_clock; }
  SceneSharedPtr getScene() const { return m_scene; }

private:
  void rebuildSceneIfRequested();
  void validateInitialized() const;

  WindowSharedPtr m_window;
  RendererSharedPtr m_renderer;
  SceneSharedPtr m_scene;
  Clock m_clock;
  UpdateHook m_updateHook;
  bool m_running = false;
  bool m_rebuildRequested = false;
};

} // namespace LX_core::gpu
