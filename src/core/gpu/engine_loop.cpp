#include "core/gpu/engine_loop.hpp"

#include <stdexcept>
#include <utility>

namespace LX_core::gpu {

void EngineLoop::initialize(WindowPtr window, RendererPtr renderer) {
  m_window = std::move(window);
  m_renderer = std::move(renderer);
  m_scene.reset();
  m_clock = Clock{};
  m_updateHook = {};
  m_running = false;
  m_rebuildRequested = false;
}

void EngineLoop::startScene(ScenePtr scene) {
  validateInitialized();
  m_scene = std::move(scene);
  if (!m_scene) {
    throw std::runtime_error("EngineLoop::startScene requires a valid scene");
  }
  m_renderer->initScene(m_scene);
  m_rebuildRequested = false;
}

void EngineLoop::setUpdateHook(UpdateHook hook) {
  m_updateHook = std::move(hook);
}

void EngineLoop::requestSceneRebuild() {
  if (m_scene) {
    m_rebuildRequested = true;
  }
}

void EngineLoop::tickFrame() {
  validateInitialized();
  if (!m_scene) {
    throw std::runtime_error(
        "EngineLoop::tickFrame requires startScene() before execution");
  }

  rebuildSceneIfRequested();
  m_clock.tick();
  if (m_updateHook) {
    m_updateHook(*m_scene, m_clock);
  }
  m_renderer->uploadData();
  m_renderer->draw();
}

void EngineLoop::run() {
  validateInitialized();
  if (!m_scene) {
    throw std::runtime_error(
        "EngineLoop::run requires startScene() before execution");
  }

  m_running = true;
  while (m_running) {
    if (m_window && m_window->shouldClose()) {
      m_running = false;
      break;
    }
    tickFrame();
  }
}

void EngineLoop::stop() { m_running = false; }

void EngineLoop::rebuildSceneIfRequested() {
  if (!m_rebuildRequested || !m_scene) {
    return;
  }
  m_renderer->initScene(m_scene);
  m_rebuildRequested = false;
}

void EngineLoop::validateInitialized() const {
  if (!m_window || !m_renderer) {
    throw std::runtime_error(
        "EngineLoop::initialize must be called with window and renderer");
  }
}

} // namespace LX_core::gpu
