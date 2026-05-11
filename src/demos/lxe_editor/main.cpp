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

#include "automation_token_state.hpp"
#include "camera_rig.hpp"
#include "editor_mcp_server.hpp"
#include "editor_automation_server.hpp"
#include "editor_automation_service.hpp"
#include "editor_config_state.hpp"
#include "editor_data_state.hpp"
#include "runtime_state.hpp"
#include "scene_catalog.hpp"
#include "scene_interaction_controller.hpp"
#include "scene_input_routing.hpp"
#include "scene_runtime.hpp"
#include "scene_session.hpp"
#include "lxe_editor_commands.hpp"
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

[[nodiscard]] demo::AutomationEditMode
toAutomationEditMode(const demo::UiOverlay::EditMode mode) {
  switch (mode) {
  case demo::UiOverlay::EditMode::Selection:
    return demo::AutomationEditMode::Selection;
  case demo::UiOverlay::EditMode::Orbit:
    return demo::AutomationEditMode::Orbit;
  case demo::UiOverlay::EditMode::FreeFly:
    return demo::AutomationEditMode::FreeFly;
  }
  return demo::AutomationEditMode::Unknown;
}

[[nodiscard]] demo::AutomationPermissionLevel toAutomationPermissionLevel(
    const demo::ScenePermissionLevel level) {
  switch (level) {
  case demo::ScenePermissionLevel::User:
    return demo::AutomationPermissionLevel::User;
  case demo::ScenePermissionLevel::Admin:
    return demo::AutomationPermissionLevel::Admin;
  }
  return demo::AutomationPermissionLevel::Unknown;
}

struct AutomationLaunchOptions final {
  bool enabled = true;
  std::string host = "0.0.0.0";
  std::uint16_t port = 3768;
};

[[nodiscard]] std::optional<AutomationLaunchOptions>
parseAutomationLaunchOptions(const int argc, char** argv,
                             std::string& errorMessage) {
  AutomationLaunchOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--automation-disable") {
      options.enabled = false;
      continue;
    }
    if (arg == "--automation-enable") {
      options.enabled = true;
      continue;
    }
    if (arg == "--automation-host") {
      if (i + 1 >= argc) {
        errorMessage = "missing value for --automation-host";
        return std::nullopt;
      }
      options.host = argv[++i];
      continue;
    }
    if (arg == "--automation-port") {
      if (i + 1 >= argc) {
        errorMessage = "missing value for --automation-port";
        return std::nullopt;
      }
      try {
        const int parsed = std::stoi(argv[++i]);
        if (parsed < 0 || parsed > 65535) {
          errorMessage = "automation port out of range";
          return std::nullopt;
        }
        options.port = static_cast<std::uint16_t>(parsed);
      } catch (...) {
        errorMessage = "invalid integer for --automation-port";
        return std::nullopt;
      }
      continue;
    }
    errorMessage = "unknown argument: " + arg;
    return std::nullopt;
  }
  return options;
}

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr std::string_view kMcpHost = "127.0.0.1";
constexpr std::uint16_t kMcpPort = 3769;

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

[[nodiscard]] int currentProcessId() {
#if defined(_WIN32)
  return _getpid();
#else
  return static_cast<int>(getpid());
#endif
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

[[nodiscard]] bool commandRequestsCameraRigResync(
    const LX_core::CommandResult& result) {
  const auto it = result.metadata.find("editor_camera.resync");
  return it != result.metadata.end() && it->second == "true";
}

[[nodiscard]] bool commandRequestsQuit(const LX_core::CommandResult& result) {
  const auto it = result.metadata.find("editor.quit");
  return it != result.metadata.end() && it->second == "true";
}

[[nodiscard]] std::reference_wrapper<LX_core::CameraComponent>
requireCameraComponent(const LX_core::SceneNodeSharedPtr& node,
                       const char* nodeLabel) {
  if (!node) {
    throw std::runtime_error(std::string("[lxe_editor] missing scene node: ") +
                             nodeLabel);
  }

  const auto camera = node->getComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    throw std::runtime_error(
        std::string("[lxe_editor] missing camera component: ") + nodeLabel);
  }
  return camera->get();
}

class LxeEditorSession final {
public:
  LxeEditorSession(demo::CameraRig& rig, demo::UiOverlay& ui,
                     LX_core::EditorState& editorState)
      : m_rig(rig),
        m_ui(ui),
        m_editorState(editorState),
        m_catalog(demo::SceneCatalogRoots{
            .assetRoots = {resolveRuntimePath("assets/scenes")},
            .localRoots = {resolveRuntimePath("data/scenes")},
        }),
        m_session(resolveRuntimePath("data/scenes"),
                  [] { return currentTimestampString(); }),
        m_editorDataState(resolveRuntimePath("data/lxe_editor")) {}

  void initialize() {
    m_editorData = m_editorDataState.load();
    refreshCatalog();
    m_runtime.createEmptyScene();
    m_session.setCurrentDocument(std::nullopt, std::nullopt);
    m_session.setDirty(false);
    rebuildBindings();
  }

  [[nodiscard]] LX_core::SceneSharedPtr scene() const { return m_runtime.scene(); }

  [[nodiscard]] LX_core::CameraComponent& editorCamera() const {
    return requireCameraComponent(m_runtime.editorCameraNode(), "editor_camera")
        .get();
  }

  [[nodiscard]] demo::SceneInteractionController& sceneInteraction() const {
    return *m_sceneInteraction;
  }

  [[nodiscard]] LX_core::CameraComponent& gameCamera() const {
    return requireCameraComponent(m_runtime.gameCameraNode(), "game_camera")
        .get();
  }

  [[nodiscard]] bool isDirty() const { return m_session.isDirty(); }
  void setWindowSize(const LX_core::Vec2f& size) { m_windowSize = size; }
  demo::EditorConfigDocument& editorConfig() { return m_editorConfig; }
  [[nodiscard]] LX_core::CommandBus& commandBus() const { return *m_commandBus; }
  [[nodiscard]] usize bindingsGeneration() const { return m_bindingsGeneration; }
  [[nodiscard]] demo::ScenePermissionLevel permission() const {
    return m_session.permission();
  }
  [[nodiscard]] const std::optional<std::filesystem::path>&
  currentDocumentPath() const {
    return m_session.currentDocumentPath();
  }
  [[nodiscard]] const std::optional<demo::SceneSourceKind>& currentSourceKind()
      const {
    return m_session.currentSourceKind();
  }
  void persistEditorData() {
    if (m_consolePanel) {
      m_editorData.consoleHistory = m_consolePanel->persistedHistory();
    }
    (void)m_editorDataState.save(m_editorData);
  }

  void recordCommandHistoryLine(std::string_view line) {
    if (!m_consolePanel) {
      return;
    }
    const auto history = m_consolePanel->persistedHistory();
    if (!history.empty() && history.back() == line) {
      return;
    }
    m_consolePanel->recordPersistedHistoryLine(line);
    m_editorData.consoleHistory = m_consolePanel->persistedHistory();
    (void)m_editorDataState.save(m_editorData);
  }

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
      if (entry.result.ok && commandRequestsCameraRigResync(entry.result)) {
        m_rig.resyncFromAttachedCamera();
      }
      if (entry.result.ok && commandRequestsQuit(entry.result)) {
        loop.stop();
      }
    }
    if (m_consolePanel && m_consolePanel->consumePersistedHistoryDirty()) {
      m_editorData.consoleHistory = m_consolePanel->persistedHistory();
      (void)m_editorDataState.save(m_editorData);
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
    m_consolePanel->setPersistedHistory(m_editorData.consoleHistory);
    m_sceneTreePanel = std::make_unique<LX_core::SceneTreePanel>(
        *m_commandBus, m_editorState, *m_runtime.scene());
    m_inspectorPanel =
        std::make_unique<LX_core::InspectorPanel>(*m_commandBus, m_editorState);
    m_sceneInteraction = std::make_unique<demo::SceneInteractionController>(
        *m_commandBus, m_editorState, *m_runtime.scene());
    demo::registerLxeEditorCommands(
        *m_commandBus,
        demo::LxeEditorCommandContext{
            .editorState = m_editorState,
            .scene = *m_runtime.scene(),
            .interaction = *m_sceneInteraction,
            .getEditMode = [this]() {
              return static_cast<int>(m_ui.currentEditMode());
            },
            .setEditMode = [this](const int modeCode) {
              m_ui.setEditMode(
                  static_cast<demo::UiOverlay::EditMode>(modeCode));
            },
            .sceneViewRect = [this]() {
              return m_ui.sceneViewRect(m_windowSize);
            },
            .dirty = [this]() { return m_session.isDirty(); },
            .permission = [this]() {
              return m_session.permission() == demo::ScenePermissionLevel::Admin
                         ? std::string("admin")
                         : std::string("user");
            },
            .currentDocumentPath = [this]() -> std::optional<std::string> {
              const auto path = m_session.currentDocumentPath();
              return path ? std::optional<std::string>(path->string())
                          : std::nullopt;
            },
            .currentSourceKind = [this]() -> std::optional<std::string> {
              const auto kind = m_session.currentSourceKind();
              if (!kind.has_value()) {
                return std::nullopt;
              }
              return sceneSourceKindName(*kind);
            },
            .persistedHistory = [this]() {
              return m_consolePanel ? m_consolePanel->persistedHistory()
                                    : std::vector<std::string>{};
            },
        });

    m_ui.attach(m_rig, *m_commandBus, m_editorState, m_editorConfig,
                *m_sceneTreePanel, *m_inspectorPanel, *m_consolePanel);
    m_lastObservedHistoryIndex = m_commandBus->history().size();
    ++m_bindingsGeneration;
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
  std::unique_ptr<demo::SceneInteractionController> m_sceneInteraction;
  size_t m_lastObservedHistoryIndex = 0;
  demo::EditorConfigDocument m_editorConfig;
  demo::EditorDataState m_editorDataState;
  demo::EditorDataDocument m_editorData;
  LX_core::Vec2f m_windowSize{static_cast<float>(kWindowWidth),
                              static_cast<float>(kWindowHeight)};
  usize m_bindingsGeneration = 0;
};

struct ClosePromptState final {
  bool open = false;
  bool popupOpened = false;
  bool confirmedClose = false;
  std::optional<std::string> saveError;
};

void drawClosePrompt(ClosePromptState& state, LxeEditorSession& session) {
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

  std::string automationArgError;
  const auto automationOptions =
      parseAutomationLaunchOptions(argc, argv, automationArgError);
  if (!automationOptions.has_value()) {
    std::cerr << "[lxe_editor] " << automationArgError << "\n";
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
    LxeEditorSession session(rig, ui, editorState);
    session.editorConfig() = editorConfig;
    session.initialize();
    ClosePromptState closePrompt;
    demo::AutomationTokenState automationTokenState(
        resolveRuntimePath("data/lxe_editor"));
    const std::string automationToken =
        automationOptions->enabled ? automationTokenState.loadOrCreateToken()
                                   : std::string{};
    demo::EditorAutomationServer automationServer(
        demo::EditorAutomationServerConfig{
            .enabled = automationOptions->enabled,
            .host = automationOptions->host,
            .port = automationOptions->port,
            .token = automationToken,
        });
    demo::EditorMcpServer mcpServer(
        demo::EditorMcpServerConfig{
            .enabled = true,
            .host = std::string(kMcpHost),
            .port = kMcpPort,
        });
    if (automationOptions->enabled) {
      std::string serverError;
      if (!automationServer.start(&serverError)) {
        throw std::runtime_error(serverError);
      }
      std::cout << "[lxe_editor] automation listening on "
                << automationServer.config().host << ":"
                << automationServer.boundPort() << " token_file="
                << automationTokenState.tokenPath() << "\n";
    }
    {
      std::string serverError;
      if (!mcpServer.start(&serverError)) {
        throw std::runtime_error(serverError);
      }
      std::cout << "[lxe_editor] mcp listening on " << mcpServer.config().host
                << ":" << mcpServer.boundPort() << "\n";
    }
    const std::uint16_t automationBoundPort =
        automationOptions->enabled
            ? static_cast<std::uint16_t>(automationServer.boundPort())
            : 0;
    saveLxeEditorRuntimeState(
        resolveRuntimePath("data/lxe_editor"),
        demo::LxeEditorRuntimeState{
            .pid = currentProcessId(),
            .httpHost = automationOptions->enabled ? automationServer.config().host
                                                   : std::string{},
            .httpPort = automationBoundPort,
            .wsHost = automationOptions->enabled ? automationServer.config().host
                                                 : std::string{},
            .wsPort = automationBoundPort,
            .mcpHost = mcpServer.config().host,
            .mcpPort = mcpServer.boundPort(),
            .tokenFile = automationTokenState.tokenPath().string(),
            .startedAt = currentTimestampString(),
        });
    auto makeAutomationService =
        [&]() -> std::unique_ptr<demo::EditorAutomationService> {
      return std::make_unique<demo::EditorAutomationService>(
          session.commandBus(), editorState, *session.scene(),
          demo::EditorAutomationService::Hooks{
              .sceneSummary =
                  [&]() {
                    return demo::AutomationSceneSummary{
                        .sceneName = session.scene()->getSceneName(),
                        .currentDocumentPath =
                            session.currentDocumentPath().has_value()
                                ? session.currentDocumentPath()->string()
                                : std::string{},
                        .sourceKind =
                            session.currentSourceKind().has_value()
                                ? (*session.currentSourceKind() ==
                                           demo::SceneSourceKind::Asset
                                       ? demo::AutomationSceneSourceKind::Asset
                                       : demo::AutomationSceneSourceKind::Local)
                                : demo::AutomationSceneSourceKind::Unknown,
                        .permission =
                            toAutomationPermissionLevel(session.permission()),
                        .dirty = session.isDirty(),
                    };
                  },
              .toolbarSnapshot =
                  [&]() {
                    return demo::AutomationToolbarSnapshot{
                        .editMode = toAutomationEditMode(ui.currentEditMode()),
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
    usize automationBindingsGeneration = session.bindingsGeneration();
    auto automationService = makeAutomationService();

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
      if (automationBindingsGeneration != session.bindingsGeneration()) {
        automationBindingsGeneration = session.bindingsGeneration();
        automationService = makeAutomationService();
      }
      session.pollCommandHistory(loop);
      automationService->refresh();
      automationServer.pump(*automationService);
      mcpServer.pump(*automationService);

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
    mcpServer.stop();
    automationServer.stop();
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
