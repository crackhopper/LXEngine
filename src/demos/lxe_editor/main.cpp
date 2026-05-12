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
#include "core/gpu/engine_loop.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"

#include "api_token_state.hpp"
#include "camera_rig.hpp"
#include "editor_session.hpp"
#include "editor_config_state.hpp"
#include "lxe_editor_api_server.hpp"
#include "lxe_editor_api_service.hpp"
#include "runtime_state.hpp"
#include "scene_interaction_controller.hpp"
#include "scene_input_routing.hpp"
#include "ui_overlay.hpp"

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
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using LX_core::backend::VulkanRenderer;
using LX_core::gpu::EngineLoop;

namespace demo = LX_demo::lxe_editor;

namespace {

[[nodiscard]] demo::SceneInputEditMode
toSceneInputEditMode(const demo::UiOverlay::EditMode mode) {
  switch (mode) {
  case demo::UiOverlay::EditMode::Selection:
    return demo::SceneInputEditMode::Selection;
  case demo::UiOverlay::EditMode::Orbit:
    return demo::SceneInputEditMode::Orbit;
  case demo::UiOverlay::EditMode::FreeFly:
    return demo::SceneInputEditMode::FreeFly;
  }
  return demo::SceneInputEditMode::Selection;
}

[[nodiscard]] demo::ApiEditMode toApiEditMode(
    const demo::UiOverlay::EditMode mode) {
  switch (mode) {
  case demo::UiOverlay::EditMode::Selection:
    return demo::ApiEditMode::Selection;
  case demo::UiOverlay::EditMode::Orbit:
    return demo::ApiEditMode::Orbit;
  case demo::UiOverlay::EditMode::FreeFly:
    return demo::ApiEditMode::FreeFly;
  }
  return demo::ApiEditMode::Unknown;
}

[[nodiscard]] demo::ApiPermissionLevel toApiPermissionLevel(
    const demo::ScenePermissionLevel level) {
  switch (level) {
  case demo::ScenePermissionLevel::User:
    return demo::ApiPermissionLevel::User;
  case demo::ScenePermissionLevel::Admin:
    return demo::ApiPermissionLevel::Admin;
  }
  return demo::ApiPermissionLevel::Unknown;
}

struct ApiLaunchOptions final {
  bool enabled = true;
  std::string host = "0.0.0.0";
  std::uint16_t port = 3768;
};

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

[[nodiscard]] std::optional<ApiLaunchOptions>
parseApiLaunchOptions(const int argc, char** argv,
                      std::string& errorMessage) {
  ApiLaunchOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--api-disable") {
      options.enabled = false;
      continue;
    }
    if (arg == "--api-enable") {
      options.enabled = true;
      continue;
    }
    if (arg == "--api-host") {
      if (i + 1 >= argc) {
        errorMessage = "missing value for --api-host";
        return std::nullopt;
      }
      options.host = argv[++i];
      continue;
    }
    if (arg == "--api-port") {
      if (i + 1 >= argc) {
        errorMessage = "missing value for --api-port";
        return std::nullopt;
      }
      try {
        const int parsed = std::stoi(argv[++i]);
        if (parsed < 0 || parsed > 65535) {
          errorMessage = "api port out of range";
          return std::nullopt;
        }
        options.port = static_cast<std::uint16_t>(parsed);
      } catch (...) {
        errorMessage = "invalid integer for --api-port";
        return std::nullopt;
      }
      continue;
    }
    errorMessage = "unknown argument: " + arg;
    return std::nullopt;
  }
  return options;
}

[[nodiscard]] int currentProcessId() {
#if defined(_WIN32)
  return _getpid();
#else
  return static_cast<int>(getpid());
#endif
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

[[nodiscard]] std::string runtimeClientHost(std::string_view host) {
  if (host == "0.0.0.0") {
    return "127.0.0.1";
  }
  return std::string(host);
}

struct ClosePromptState final {
  bool open = false;
  bool popupOpened = false;
  bool confirmedClose = false;
  std::optional<std::string> saveError;
};

void drawClosePrompt(ClosePromptState& state, demo::LxeEditorSession& session) {
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

int main(int argc, char** argv) {
  expSetEnvVK();
  if (!initializeRuntimeAssetRoot()) {
    std::cerr << "[lxe_editor] failed to initialize runtime asset root\n";
    return 1;
  }

  std::string apiArgError;
  const auto apiOptions =
      parseApiLaunchOptions(argc, argv, apiArgError);
  if (!apiOptions.has_value()) {
    std::cerr << "[lxe_editor] " << apiArgError << "\n";
    return 1;
  }

  try {
    LX_infra::Window::Initialize();
    demo::EditorConfigState configState(resolveRuntimePath("data/lxe_editor"));
    demo::EditorConfigDocument editorConfig = configState.load();
    auto window = std::make_shared<LX_infra::Window>(
        "lxe_editor", kWindowWidth, kWindowHeight,
        editorConfig.windowPlacement);

    auto vulkanRenderer =
        std::make_shared<VulkanRenderer>(VulkanRenderer::Token{});
    LX_core::gpu::RendererSharedPtr renderer = vulkanRenderer;
    renderer->initialize(window, "lxe_editor");

    demo::CameraRig rig;
    LX_core::EditorState editorState;
    demo::UiOverlay ui;
    demo::LxeEditorSession session(rig, ui, editorState);
    session.editorConfig() = editorConfig;
    session.initialize();
    ClosePromptState closePrompt;
    demo::ApiTokenState apiTokenState(
        resolveRuntimePath("data/lxe_editor"));
    const std::string apiToken =
        apiOptions->enabled ? apiTokenState.loadOrCreateToken()
                            : std::string{};
    demo::LxeEditorApiServer apiServer(
        demo::LxeEditorApiServerConfig{
            .enabled = apiOptions->enabled,
            .host = apiOptions->host,
            .port = apiOptions->port,
            .token = apiToken,
        });
    if (apiOptions->enabled) {
      std::string serverError;
      if (!apiServer.start(&serverError)) {
        throw std::runtime_error(serverError);
      }
      std::cout << "[lxe_editor] api listening on "
                << apiServer.config().host << ":"
                << apiServer.boundPort() << " token_file="
                << apiTokenState.tokenPath() << "\n";
    }
    const std::uint16_t apiBoundPort =
        apiOptions->enabled
            ? static_cast<std::uint16_t>(apiServer.boundPort())
            : 0;
    const std::string runtimeHost =
        runtimeClientHost(apiServer.config().host);
    const std::string mcpUrl =
        apiOptions->enabled
            ? std::string("http://") + runtimeHost + ":" +
                  std::to_string(apiBoundPort) + "/mcp"
            : std::string{};
    demo::saveLxeEditorRuntimeState(
        resolveRuntimePath("data/lxe_editor"),
        demo::LxeEditorRuntimeState{
            .pid = currentProcessId(),
            .httpHost = apiOptions->enabled ? runtimeHost
                                                   : std::string{},
            .httpPort = apiBoundPort,
            .wsHost = apiOptions->enabled ? runtimeHost
                                                 : std::string{},
            .wsPort = apiBoundPort,
            .mcpUrl = mcpUrl,
            .tokenFile = apiTokenState.tokenPath().string(),
            .startedAt = currentTimestampString(),
        });
    auto makeApiService =
        [&]() -> std::unique_ptr<demo::LxeEditorApiService> {
      return std::make_unique<demo::LxeEditorApiService>(
          session.commandBus(), editorState, *session.scene(),
          demo::LxeEditorApiService::Hooks{
              .sceneSummary =
                  [&]() {
                    return demo::ApiSceneSummary{
                        .sceneName = session.scene()->getSceneName(),
                        .currentDocumentPath =
                            session.currentDocumentPath().has_value()
                                ? session.currentDocumentPath()->string()
                                : std::string{},
                        .sourceKind =
                            session.currentSourceKind().has_value()
                                ? (*session.currentSourceKind() ==
                                           demo::SceneSourceKind::Asset
                                       ? demo::ApiSceneSourceKind::Asset
                                       : demo::ApiSceneSourceKind::Local)
                                : demo::ApiSceneSourceKind::Unknown,
                        .permission =
                            toApiPermissionLevel(session.permission()),
                        .dirty = session.isDirty(),
                    };
                  },
              .toolbarSnapshot =
                  [&]() {
                    return demo::ApiToolbarSnapshot{
                        .editMode = toApiEditMode(ui.currentEditMode()),
                        .previewEnabled = editorState.isPreviewEnabled(),
                    };
                  },
              .lastHitPoint = [&]() {
                return session.sceneInteraction().lastHitPoint();
              },
              .recordCommandHistoryLine = [&session](std::string_view line) {
                session.recordCommandHistoryLine(line);
              },
          });
    };
    usize apiBindingsGeneration = session.bindingsGeneration();
    auto apiService = makeApiService();

    vulkanRenderer->setDrawUiCallback([&] {
      ui.drawFrame();
      drawClosePrompt(closePrompt, session);
      session.editorConfig().windowPlacement = window->getPlacement();
      if (ui.consumeConfigDirty()) {
        (void)configState.save(session.editorConfig());
      }
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
      if (apiBindingsGeneration != session.bindingsGeneration()) {
        apiBindingsGeneration = session.bindingsGeneration();
        apiService = makeApiService();
      }
      session.pollCommandHistory(loop);
      apiService->refresh();
      apiServer.pump(*apiService);

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

      const int windowWidth = window->getWidth();
      const int windowHeight = window->getHeight();
      session.setWindowSize(LX_core::Vec2f{static_cast<float>(windowWidth),
                                           static_cast<float>(windowHeight)});
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

      const demo::SceneInputEditMode inputMode =
          toSceneInputEditMode(ui.currentEditMode());
      if (demo::shouldProcessSelectionMode(editorState.isPreviewEnabled(),
                                           wantsMouse, inputMode)) {
          session.sceneInteraction().updateSelectionMode(
              *input, ui.sceneViewRect(LX_core::Vec2f{
                          static_cast<float>(windowWidth),
                          static_cast<float>(windowHeight)}));
          session.editorCamera().updateMatrices();
      } else if (demo::shouldProcessCameraRig(
                     editorState.isPreviewEnabled(), wantsKeyboard, wantsMouse,
                     inputMode)) {
          rig.update(*input, clock.deltaTime());
      } else {
        session.editorCamera().updateMatrices();
      }
      session.sceneInteraction().enqueueDebugDraw();
      input->nextFrame();
    });

    loop.run();
    std::filesystem::remove(resolveRuntimePath("data/lxe_editor") /
                            "runtime_state.yaml");
    apiServer.stop();
    session.editorConfig().windowPlacement = window->getPlacement();
    (void)configState.save(session.editorConfig());
    session.persistEditorData();
    renderer->shutdown();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "[lxe_editor] fatal: " << e.what() << "\n";
    return 2;
  }
}
