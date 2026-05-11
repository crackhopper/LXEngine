// REQ-019: default integration demo.
//
// Wires:
//   runtime asset root -> Window -> VulkanRenderer -> SceneRuntime
//   -> EngineLoop -> ImGui editor panels / overlay -> run().

#include "backend/vulkan/vulkan_renderer.hpp"
#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/console_panel.hpp"
#include "core/editor/editor_state.hpp"
#include "core/editor/inspector_panel.hpp"
#include "core/editor/scene_tree_panel.hpp"
#include "core/editor/viewport_overlay.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"

#include "camera_rig.hpp"
#include "scene_runtime.hpp"
#include "ui_overlay.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <imgui.h>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

using LX_core::backend::VulkanRenderer;
using LX_core::gpu::EngineLoop;

namespace demo = LX_demo::scene_viewer;

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

[[nodiscard]] std::string jsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

[[nodiscard]] LX_core::CommandResult makeCommandError(std::string message) {
  return LX_core::CommandResult{false, std::move(message), {}};
}

[[nodiscard]] LX_core::CommandResult makeCommandOk(std::string message,
                                                   std::string structured = {}) {
  return LX_core::CommandResult{true, std::move(message), std::move(structured)};
}

[[nodiscard]] std::reference_wrapper<LX_core::CameraComponent>
requireCameraComponent(const LX_core::SceneNodeSharedPtr& node,
                       const char* nodeLabel) {
  if (!node) {
    throw std::runtime_error(std::string("[scene_viewer] missing scene node: ") +
                             nodeLabel);
  }

  const auto camera = node->getComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    throw std::runtime_error(
        std::string("[scene_viewer] missing camera component: ") + nodeLabel);
  }
  return camera->get();
}

class SceneViewerSession final {
public:
  SceneViewerSession(demo::CameraRig& rig, demo::UiOverlay& ui,
                     LX_core::EditorState& editorState)
      : m_rig(rig), m_ui(ui), m_editorState(editorState) {}

  void initialize() {
    m_runtime.loadDefaultDocument();
    rebuildBindings();
  }

  [[nodiscard]] LX_core::SceneSharedPtr scene() const { return m_runtime.scene(); }

  [[nodiscard]] LX_core::ViewportOverlay& viewportOverlay() const {
    return *m_viewportOverlay;
  }

  [[nodiscard]] LX_core::CameraComponent& editorCamera() const {
    return requireCameraComponent(m_runtime.editorCameraNode(), "editor_camera")
        .get();
  }

  [[nodiscard]] LX_core::CameraComponent& gameCamera() const {
    return requireCameraComponent(m_runtime.gameCameraNode(), "game_camera")
        .get();
  }

  void flushPendingSceneLoad(EngineLoop& loop) {
    if (!m_pendingRuntime.has_value()) {
      return;
    }

    m_runtime = std::move(*m_pendingRuntime);
    m_pendingRuntime.reset();
    rebuildBindings();
    loop.startScene(m_runtime.scene());
  }

private:
  [[nodiscard]] LX_core::CommandResult
  queueSceneLoad(const std::string& path) {
    try {
      demo::SceneRuntime loaded;
      loaded.loadFromDocumentPath(path);
      const std::filesystem::path loadedPath = loaded.documentPath();
      m_pendingRuntime = std::move(loaded);
      return makeCommandOk(
          "queued scene load for next update tick: " + loadedPath.string(),
          "{\"path\":\"" + jsonEscape(loadedPath.string()) +
              "\",\"status\":\"queued\",\"deferredUntil\":\"next_update_tick\"}");
    } catch (const std::exception& e) {
      return makeCommandError(e.what());
    }
  }

  [[nodiscard]] LX_core::CommandResult
  saveScene(const std::optional<std::string>& path) {
    try {
      if (path.has_value()) {
        m_runtime.saveToDocumentPath(*path);
      } else {
        m_runtime.saveToCurrentDocumentPath();
      }

      const std::filesystem::path savedPath = m_runtime.documentPath();
      return makeCommandOk(
          "saved scene " + savedPath.string(),
          "{\"path\":\"" + jsonEscape(savedPath.string()) + "\"}");
    } catch (const std::exception& e) {
      return makeCommandError(e.what());
    }
  }

  void rebuildBindings() {
    const bool previewEnabled = m_editorState.isPreviewEnabled();
    m_editorState.deselect();
    m_editorState.setEditorCamera(m_runtime.editorCameraNode());
    m_editorState.setPreviewCamera(m_runtime.gameCameraNode());
    m_editorState.setPreviewEnabled(previewEnabled);
    (void)m_editorState.syncActiveCamera(*m_runtime.scene());

    m_rig.attach(editorCamera());

    m_commandBus = std::make_unique<LX_core::CommandBus>();
    LX_core::registerBuiltinCommands(
        *m_commandBus, m_editorState, *m_runtime.scene(),
        LX_core::SceneIoContext{
            .load = [this](const std::string& path) {
              return queueSceneLoad(path);
            },
            .save = [this](const std::optional<std::string>& path) {
              return saveScene(path);
            },
        });
    m_consolePanel = std::make_unique<LX_core::ConsolePanel>(*m_commandBus);
    m_sceneTreePanel = std::make_unique<LX_core::SceneTreePanel>(
        *m_commandBus, m_editorState, *m_runtime.scene());
    m_inspectorPanel =
        std::make_unique<LX_core::InspectorPanel>(*m_commandBus, m_editorState);
    m_viewportOverlay = std::make_unique<LX_core::ViewportOverlay>(
        *m_commandBus, m_editorState, *m_runtime.scene());

    m_ui.attach(m_rig, *m_commandBus, *m_sceneTreePanel, *m_inspectorPanel,
                *m_consolePanel, *m_viewportOverlay);
  }

  demo::CameraRig& m_rig;
  demo::UiOverlay& m_ui;
  LX_core::EditorState& m_editorState;
  demo::SceneRuntime m_runtime;
  std::optional<demo::SceneRuntime> m_pendingRuntime;
  std::unique_ptr<LX_core::CommandBus> m_commandBus;
  std::unique_ptr<LX_core::ConsolePanel> m_consolePanel;
  std::unique_ptr<LX_core::SceneTreePanel> m_sceneTreePanel;
  std::unique_ptr<LX_core::InspectorPanel> m_inspectorPanel;
  std::unique_ptr<LX_core::ViewportOverlay> m_viewportOverlay;
};

} // namespace

int main() {
  expSetEnvVK();
  if (!initializeRuntimeAssetRoot()) {
    std::cerr << "[scene_viewer] failed to initialize runtime asset root\n";
    return 1;
  }

  try {
    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>(
        "demo_scene_viewer", kWindowWidth, kWindowHeight);

    auto vulkanRenderer =
        std::make_shared<VulkanRenderer>(VulkanRenderer::Token{});
    LX_core::gpu::RendererSharedPtr renderer = vulkanRenderer;
    renderer->initialize(window, "demo_scene_viewer");

    demo::CameraRig rig;
    LX_core::EditorState editorState;
    demo::UiOverlay ui;
    SceneViewerSession session(rig, ui, editorState);
    session.initialize();

    vulkanRenderer->setDrawUiCallback([&] { ui.drawFrame(); });

    EngineLoop loop;
    loop.initialize(window, renderer);
    loop.startScene(session.scene());

    ui.attachClock(loop.getClock());

    auto input = window->getInputState();

    loop.setUpdateHook([&](LX_core::Scene&, const LX_core::Clock& clock) {
      session.flushPendingSceneLoad(loop);

      const bool imguiReady = ImGui::GetCurrentContext() != nullptr;
      const auto io =
          imguiReady
              ? std::optional<std::reference_wrapper<const ImGuiIO>>(
                    std::cref(ImGui::GetIO()))
              : std::nullopt;
      const bool wantsKeyboard = io && io->get().WantCaptureKeyboard;
      const bool wantsMouse = io && io->get().WantCaptureMouse;

      if (!wantsKeyboard) {
        ui.handleHotkeys(*input);
      }

      session.viewportOverlay().enqueueDebugDraw();

      const int windowWidth = window->getWidth();
      const int windowHeight = window->getHeight();
      const bool hasValidExtent = windowWidth > 0 && windowHeight > 0;
      const float aspect =
          hasValidExtent
              ? static_cast<float>(windowWidth) / static_cast<float>(windowHeight)
              : session.editorCamera().aspect;
      if (hasValidExtent) {
        session.editorCamera().aspect = aspect;
        session.gameCamera().aspect = aspect;
      }
      session.gameCamera().updateMatrices();

      if (!wantsKeyboard && !wantsMouse) {
        rig.update(*input, clock.deltaTime());
      } else {
        session.editorCamera().updateMatrices();
      }
      input->nextFrame();
    });

    loop.run();
    renderer->shutdown();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "[scene_viewer] fatal: " << e.what() << "\n";
    return 2;
  }
}
