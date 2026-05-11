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
#include "scene_catalog.hpp"
#include "scene_runtime.hpp"
#include "scene_session.hpp"
#include "ui_overlay.hpp"
#include "window_layout_state.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <imgui.h>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

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

[[nodiscard]] std::string currentTimestampString() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t timeNow = std::chrono::system_clock::to_time_t(now);
  std::tm tmNow{};
#if defined(_WIN32)
  localtime_s(&tmNow, &timeNow);
#else
  localtime_r(&timeNow, &tmNow);
#endif
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d-%H%M%S", &tmNow);
  return buffer;
}

[[nodiscard]] std::string sceneSourceKindName(
    const demo::SceneSourceKind kind) {
  return kind == demo::SceneSourceKind::Asset ? "asset" : "local";
}

[[nodiscard]] bool commandMarksSceneDirty(const std::string& line) {
  const auto firstSpace = line.find(' ');
  const std::string verb =
      firstSpace == std::string::npos ? line : line.substr(0, firstSpace);
  static const std::unordered_set<std::string> kMutatingVerbs = {
      "move", "rotate", "scale", "set", "add", "remove", "undo", "redo",
      "__remove_to_stash", "__restore_from_stash"};
  return kMutatingVerbs.find(verb) != kMutatingVerbs.end();
}

[[nodiscard]] bool commandRequestsSceneRebuild(
    const LX_core::CommandResult& result) {
  const auto it = result.metadata.find("scene.rebuild");
  return it != result.metadata.end() && it->second == "true";
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
      : m_rig(rig),
        m_ui(ui),
        m_editorState(editorState),
        m_catalog(demo::SceneCatalogRoots{
            .assetRoots = {resolveRuntimePath("assets/scenes")},
            .localRoots = {resolveRuntimePath("data/scenes")},
        }),
        m_session(resolveRuntimePath("data/scenes"),
                  [] { return currentTimestampString(); }) {}

  void initialize() {
    refreshCatalog();
    m_runtime.createEmptyScene();
    m_session.setCurrentDocument(std::nullopt, std::nullopt);
    m_session.setDirty(false);
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

  [[nodiscard]] bool isDirty() const { return m_session.isDirty(); }

  [[nodiscard]] LX_core::CommandResult
  saveScene(const std::optional<std::string>& path) {
    try {
      const std::optional<std::filesystem::path> explicitPath =
          path.has_value() ? std::optional<std::filesystem::path>(*path)
                           : std::nullopt;
      const demo::SaveDecision decision = m_session.decideSaveTarget(
          explicitPath, m_runtime.scene() ? m_runtime.scene()->getSceneName() : "Scene");
      m_runtime.saveToDocumentPath(decision.path);
      m_session.setCurrentDocument(decision.path, decision.kind);
      m_session.setDirty(false);
      refreshCatalog();

      std::string message = "saved scene " + decision.path.string();
      if (decision.redirectedFromAsset) {
        message = "asset is read-only; saved local copy " + decision.path.string();
      }
      return makeCommandOk(
          std::move(message),
          "{\"path\":\"" + jsonEscape(decision.path.string()) + "\",\"kind\":\"" +
              sceneSourceKindName(decision.kind) +
              "\",\"redirectedFromAsset\":" +
              std::string(decision.redirectedFromAsset ? "true" : "false") + "}");
    } catch (const std::exception& e) {
      return makeCommandError(e.what());
    }
  }

  void flushPendingSceneLoad(EngineLoop& loop) {
    if (!m_pendingRuntime.has_value()) {
      return;
    }

    m_runtime = std::move(*m_pendingRuntime);
    if (m_pendingSourceKind.has_value() && m_runtime.documentPath().has_value()) {
      m_session.setCurrentDocument(*m_runtime.documentPath(), *m_pendingSourceKind);
    } else {
      m_session.setCurrentDocument(m_runtime.documentPath(), std::nullopt);
    }
    m_session.setDirty(false);
    m_pendingRuntime.reset();
    m_pendingSourceKind.reset();
    rebuildBindings();
    loop.startScene(m_runtime.scene());
  }

  void pollCommandHistory(EngineLoop& loop) {
    if (!m_commandBus) {
      return;
    }
    const auto& history = m_commandBus->history();
    while (m_lastObservedHistoryIndex < history.size()) {
      const auto& entry = history[m_lastObservedHistoryIndex++];
      if (entry.result.ok && commandMarksSceneDirty(entry.line)) {
        m_session.setDirty(true);
      }
      if (entry.result.ok && commandRequestsSceneRebuild(entry.result)) {
        loop.requestSceneRebuild();
      }
    }
  }

private:
  void refreshCatalog() { m_catalog.refresh(); }

  [[nodiscard]] LX_core::CommandResult listScenes() {
    refreshCatalog();
    std::string message =
        "listed " + std::to_string(m_catalog.entries().size()) + " scene(s)";
    const auto& entries = m_catalog.entries();
    if (!entries.empty()) {
      message += ":\n";
      for (size_t i = 0; i < entries.size(); ++i) {
        if (i != 0) {
          message += '\n';
        }
        message += "- [";
        message += sceneSourceKindName(entries[i].kind);
        message += "] ";
        message += entries[i].id;
        message += " -> ";
        message += entries[i].path.string();
      }
    }
    std::string structured = "{\"entries\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
      if (i != 0) {
        structured += ",";
      }
      structured += "{\"id\":\"" + jsonEscape(entries[i].id) + "\",\"kind\":\"" +
                    sceneSourceKindName(entries[i].kind) + "\",\"path\":\"" +
                    jsonEscape(entries[i].path.string()) + "\"}";
    }
    structured += "]}";
    return makeCommandOk(std::move(message), std::move(structured));
  }

  [[nodiscard]] LX_core::CommandResult
  queueSceneLoad(const std::string& path) {
    try {
      refreshCatalog();
      const std::filesystem::path resolvedPath = m_catalog.resolveNameOrPath(path);
      const auto classified = m_catalog.classifyPath(resolvedPath);
      demo::SceneRuntime loaded;
      loaded.loadFromDocumentPath(resolvedPath,
                                  classified ? std::optional{classified->kind}
                                             : std::nullopt);
      const auto loadedPath = loaded.documentPath();
      if (!loadedPath.has_value()) {
        return makeCommandError("queued scene load produced no document path");
      }
      m_pendingRuntime = std::move(loaded);
      m_pendingSourceKind =
          classified ? std::optional{classified->kind} : std::nullopt;
      return makeCommandOk(
          "queued scene load for next update tick: " + loadedPath->string(),
          "{\"path\":\"" + jsonEscape(loadedPath->string()) +
              "\",\"kind\":\"" +
              jsonEscape(classified ? sceneSourceKindName(classified->kind)
                                    : std::string("external")) +
              "\",\"status\":\"queued\",\"deferredUntil\":\"next_update_tick\"}");
    } catch (const std::exception& e) {
      return makeCommandError(e.what());
    }
  }

  [[nodiscard]] LX_core::CommandResult setAdmin(const bool enabled) {
    m_session.setPermission(enabled ? demo::ScenePermissionLevel::Admin
                                    : demo::ScenePermissionLevel::User);
    return makeCommandOk(enabled ? "admin enabled" : "admin disabled",
                         enabled ? "{\"permission\":\"admin\"}"
                                 : "{\"permission\":\"user\"}");
  }

  [[nodiscard]] LX_core::CommandResult adminStatus() const {
    const bool enabled = m_session.permission() == demo::ScenePermissionLevel::Admin;
    return makeCommandOk(enabled ? "admin" : "user",
                         enabled ? "{\"permission\":\"admin\"}"
                                 : "{\"permission\":\"user\"}");
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
            .list = [this]() { return listScenes(); },
            .setAdmin = [this](const bool enabled) { return setAdmin(enabled); },
            .adminStatus = [this]() { return adminStatus(); },
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
    m_lastObservedHistoryIndex = m_commandBus->history().size();
  }

  demo::CameraRig& m_rig;
  demo::UiOverlay& m_ui;
  LX_core::EditorState& m_editorState;
  demo::SceneCatalog m_catalog;
  demo::SceneSession m_session;
  demo::SceneRuntime m_runtime;
  std::optional<demo::SceneRuntime> m_pendingRuntime;
  std::optional<demo::SceneSourceKind> m_pendingSourceKind;
  std::unique_ptr<LX_core::CommandBus> m_commandBus;
  std::unique_ptr<LX_core::ConsolePanel> m_consolePanel;
  std::unique_ptr<LX_core::SceneTreePanel> m_sceneTreePanel;
  std::unique_ptr<LX_core::InspectorPanel> m_inspectorPanel;
  std::unique_ptr<LX_core::ViewportOverlay> m_viewportOverlay;
  size_t m_lastObservedHistoryIndex = 0;
};

struct ClosePromptState final {
  bool open = false;
  bool popupOpened = false;
  bool confirmedClose = false;
  std::optional<std::string> saveError;
};

void drawClosePrompt(ClosePromptState& state, SceneViewerSession& session) {
  if (state.open && !state.popupOpened) {
    ImGui::OpenPopup("Save Scene Before Exit");
    state.popupOpened = true;
  }

  if (!state.open) {
    return;
  }

  if (ImGui::BeginPopupModal("Save Scene Before Exit", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Current scene has unsaved changes.");
    ImGui::TextUnformatted("Save to the scene workspace before closing?");
    if (state.saveError.has_value()) {
      ImGui::Spacing();
      ImGui::TextWrapped("Save failed: %s", state.saveError->c_str());
    }

    if (ImGui::Button("Save")) {
      const auto result = session.saveScene(std::nullopt);
      if (result.ok) {
        state.confirmedClose = true;
        state.open = false;
        state.popupOpened = false;
        state.saveError.reset();
        ImGui::CloseCurrentPopup();
      } else {
        state.saveError = result.message;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
      state.confirmedClose = true;
      state.open = false;
      state.popupOpened = false;
      state.saveError.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      state.open = false;
      state.popupOpened = false;
      state.saveError.reset();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}

} // namespace

int main() {
  expSetEnvVK();
  if (!initializeRuntimeAssetRoot()) {
    std::cerr << "[scene_viewer] failed to initialize runtime asset root\n";
    return 1;
  }

  try {
    LX_infra::Window::Initialize();
    demo::WindowLayoutState layoutState(resolveRuntimePath("data/scene_viewer"));
    const auto initialPlacement = layoutState.loadNativeWindowPlacement();
    auto window = std::make_shared<LX_infra::Window>(
        "demo_scene_viewer", kWindowWidth, kWindowHeight, initialPlacement);

    auto vulkanRenderer =
        std::make_shared<VulkanRenderer>(VulkanRenderer::Token{});
    LX_core::gpu::RendererSharedPtr renderer = vulkanRenderer;
    renderer->initialize(window, "demo_scene_viewer");

    demo::CameraRig rig;
    LX_core::EditorState editorState;
    demo::UiOverlay ui;
    SceneViewerSession session(rig, ui, editorState);
    session.initialize();
    const bool restoredImGuiLayout = layoutState.restoreImGuiLayout();
    ui.setDefaultLayoutEnabled(
        !(restoredImGuiLayout && layoutState.hasAuthoritativeSceneViewerLayout()));
    ClosePromptState closePrompt;

    vulkanRenderer->setDrawUiCallback([&] {
      ui.drawFrame();
      drawClosePrompt(closePrompt, session);
      layoutState.maybeSaveImGuiLayout();
    });

    EngineLoop loop;
    loop.initialize(window, renderer);
    loop.startScene(session.scene());

    ui.attachClock(loop.getClock());

    window->onClose([&]() {
      if (!session.isDirty()) {
        return true;
      }
      closePrompt.open = true;
      closePrompt.confirmedClose = false;
      closePrompt.saveError.reset();
      return false;
    });

    auto input = window->getInputState();

    loop.setUpdateHook([&](LX_core::Scene&, const LX_core::Clock& clock) {
      if (closePrompt.confirmedClose) {
        loop.stop();
        return;
      }
      session.flushPendingSceneLoad(loop);
      session.pollCommandHistory(loop);

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
    layoutState.saveImGuiLayout();
    layoutState.captureNativeWindowPlacement(*window);
    renderer->shutdown();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "[scene_viewer] fatal: " << e.what() << "\n";
    return 2;
  }
}
