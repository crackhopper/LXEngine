#include "demos/lxe_editor/editor_session.hpp"

#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/console_panel.hpp"
#include "core/editor/inspector_panel.hpp"
#include "core/editor/scene_tree_panel.hpp"
#include "core/editor/viewport_overlay.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "demos/lxe_editor/lxe_editor_commands.hpp"
#include "demos/lxe_editor/scene_interaction_controller.hpp"

#include <chrono>
#include <ctime>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace LX_demo::lxe_editor {
namespace {

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

[[nodiscard]] std::string sceneSourceKindName(const SceneSourceKind kind) {
  return kind == SceneSourceKind::Asset ? "asset" : "local";
}

[[nodiscard]] std::string cameraControlModeName(
    const UiOverlay::CameraControlMode mode) {
  switch (mode) {
  case UiOverlay::CameraControlMode::Orbit:
    return "orbit";
  case UiOverlay::CameraControlMode::FreeFly:
    return "freefly";
  }
  return "orbit";
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

} // namespace

LxeEditorSession::LxeEditorSession(CameraRig& rig, UiOverlay& ui,
                                   LX_core::EditorState& editorState)
    : m_rig(rig),
      m_ui(ui),
      m_editorState(editorState),
      m_catalog(SceneCatalogRoots{
          .assetRoots = {resolveRuntimePath("assets/scenes")},
          .localRoots = {resolveRuntimePath("data/scenes")},
      }),
      m_session(resolveRuntimePath("data/scenes"),
                [] { return currentTimestampString(); }),
      m_editorDataState(resolveRuntimePath("data/lxe_editor")) {}

LxeEditorSession::~LxeEditorSession() = default;

void LxeEditorSession::initialize() {
  m_editorData = m_editorDataState.load();
  refreshCatalog();
  m_runtime.createEmptyScene();
  m_session.setCurrentDocument(std::nullopt, std::nullopt);
  m_session.setDirty(false);
  rebuildBindings();
}

LX_core::SceneSharedPtr LxeEditorSession::scene() const { return m_runtime.scene(); }

LX_core::CameraComponent& LxeEditorSession::editorCamera() const {
  return requireCameraComponent(m_runtime.editorCameraNode(), "editor_camera")
      .get();
}

SceneInteractionController& LxeEditorSession::sceneInteraction() const {
  return *m_sceneInteraction;
}

LX_core::CameraComponent& LxeEditorSession::gameCamera() const {
  return requireCameraComponent(m_runtime.gameCameraNode(), "game_camera").get();
}

bool LxeEditorSession::isDirty() const { return m_session.isDirty(); }

void LxeEditorSession::setWindowSize(const LX_core::Vec2f& size) {
  m_windowSize = size;
}

EditorConfigDocument& LxeEditorSession::editorConfig() { return m_editorConfig; }

LX_core::CommandBus& LxeEditorSession::commandBus() const { return *m_commandBus; }

const LX_core::ConsolePanel& LxeEditorSession::consolePanel() const {
  return *m_consolePanel;
}

usize LxeEditorSession::bindingsGeneration() const { return m_bindingsGeneration; }

ScenePermissionLevel LxeEditorSession::permission() const {
  return m_session.permission();
}

bool LxeEditorSession::debugEnabled() const { return m_debugEnabled; }

const std::optional<std::filesystem::path>&
LxeEditorSession::currentDocumentPath() const {
  return m_session.currentDocumentPath();
}

const std::optional<SceneSourceKind>& LxeEditorSession::currentSourceKind() const {
  return m_session.currentSourceKind();
}

void LxeEditorSession::persistEditorData() {
  if (m_consolePanel) {
    m_editorData.consoleHistory = m_consolePanel->persistedHistory();
  }
  (void)m_editorDataState.save(m_editorData);
}

void LxeEditorSession::recordCommandHistoryLine(std::string_view line) {
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

LX_core::CommandResult
LxeEditorSession::saveScene(const std::optional<std::string>& path) {
  try {
    const std::optional<std::filesystem::path> explicitPath =
        path.has_value() ? std::optional<std::filesystem::path>(*path)
                         : std::nullopt;
    const SaveDecision decision = m_session.decideSaveTarget(
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
            sceneSourceKindName(decision.kind) + "\",\"redirectedFromAsset\":" +
            std::string(decision.redirectedFromAsset ? "true" : "false") + "}");
  } catch (const std::exception& e) {
    return makeCommandError(e.what());
  }
}

void LxeEditorSession::flushPendingSceneLoad(LX_core::gpu::EngineLoop& loop) {
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

void LxeEditorSession::pollCommandHistory(LX_core::gpu::EngineLoop& loop) {
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

void LxeEditorSession::refreshCatalog() { m_catalog.refresh(); }

LX_core::CommandResult LxeEditorSession::listScenes() {
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

LX_core::CommandResult LxeEditorSession::queueSceneLoad(const std::string& path) {
  try {
    refreshCatalog();
    const std::filesystem::path resolvedPath = m_catalog.resolveNameOrPath(path);
    const auto classified = m_catalog.classifyPath(resolvedPath);
    SceneRuntime loaded;
    loaded.loadFromDocumentPath(resolvedPath,
                                classified ? std::optional{classified->kind}
                                           : std::nullopt);
    const auto loadedPath = loaded.documentPath();
    if (!loadedPath.has_value()) {
      return makeCommandError("queued scene load produced no document path");
    }
    m_pendingRuntime = std::move(loaded);
    m_pendingSourceKind = classified ? std::optional{classified->kind} : std::nullopt;
    return makeCommandOk(
        "queued scene load for next update tick: " + loadedPath->string(),
        "{\"path\":\"" + jsonEscape(loadedPath->string()) + "\",\"kind\":\"" +
            jsonEscape(classified ? sceneSourceKindName(classified->kind)
                                  : std::string("external")) +
            "\",\"status\":\"queued\",\"deferredUntil\":\"next_update_tick\"}");
  } catch (const std::exception& e) {
    return makeCommandError(e.what());
  }
}

LX_core::CommandResult LxeEditorSession::setAdmin(const bool enabled) {
  m_session.setPermission(enabled ? ScenePermissionLevel::Admin
                                  : ScenePermissionLevel::User);
  return makeCommandOk(enabled ? "admin enabled" : "admin disabled",
                       enabled ? "{\"permission\":\"admin\"}"
                               : "{\"permission\":\"user\"}");
}

LX_core::CommandResult LxeEditorSession::adminStatus() const {
  const bool enabled = m_session.permission() == ScenePermissionLevel::Admin;
  return makeCommandOk(enabled ? "admin" : "user",
                       enabled ? "{\"permission\":\"admin\"}"
                               : "{\"permission\":\"user\"}");
}

void LxeEditorSession::rebuildBindings() {
  const bool previewEnabled = m_editorState.isPreviewEnabled();
  m_editorState.deselect();
  m_editorState.setEditorCamera(m_runtime.editorCameraNode());
  m_editorState.setPreviewCamera(m_runtime.gameCameraNode());
  m_editorState.setPreviewEnabled(previewEnabled);
  (void)m_editorState.syncActiveCamera(*m_runtime.scene());

  m_rig.attach(editorCamera());

  if (!m_commandBus) {
    m_commandBus = std::make_unique<LX_core::CommandBus>();
  }
  LX_core::registerBuiltinCommands(
      *m_commandBus, m_editorState, *m_runtime.scene(),
      LX_core::SceneIoContext{
          .load = [this](const std::string& path) { return queueSceneLoad(path); },
          .save = [this](const std::optional<std::string>& path) {
            return saveScene(path);
          },
          .list = [this]() { return listScenes(); },
          .setAdmin = [this](const bool enabled) { return setAdmin(enabled); },
          .adminStatus = [this]() { return adminStatus(); },
          .cameraControl =
              [this](const std::vector<std::string>& args) {
                if (args.size() != 2) {
                  return makeCommandError(
                      "usage: cam control (orbit|freefly|status)");
                }
                if (args[1] == "status") {
                  const std::string camera = cameraControlModeName(
                      m_ui.currentCameraControlMode());
                  return makeCommandOk("camera " + camera,
                                       "{\"camera\":\"" + camera + "\"}");
                }
                if (args[1] != "orbit" && args[1] != "freefly") {
                  return makeCommandError("unknown camera control: " + args[1]);
                }
                const UiOverlay::CameraControlMode previous =
                    m_ui.currentCameraControlMode();
                const UiOverlay::CameraControlMode next =
                    args[1] == "orbit" ? UiOverlay::CameraControlMode::Orbit
                                        : UiOverlay::CameraControlMode::FreeFly;
                m_ui.setCameraControlMode(next);
                const std::string camera = cameraControlModeName(next);
                LX_core::CommandResult result =
                    makeCommandOk("camera " + camera,
                                  "{\"camera\":\"" + camera + "\"}");
                result.metadata["inverse.line"] =
                    "cam control " + cameraControlModeName(previous);
                return result;
              },
      });
  if (!m_consolePanel) {
    m_consolePanel = std::make_unique<LX_core::ConsolePanel>(*m_commandBus);
    m_consolePanel->setPersistedHistory(m_editorData.consoleHistory);
  }
  m_sceneTreePanel = std::make_unique<LX_core::SceneTreePanel>(
      *m_commandBus, m_editorState, *m_runtime.scene());
  m_inspectorPanel =
      std::make_unique<LX_core::InspectorPanel>(*m_commandBus, m_editorState);
  m_viewportOverlay = std::make_unique<LX_core::ViewportOverlay>(
      *m_commandBus, m_editorState, *m_runtime.scene());
  m_sceneInteraction = std::make_unique<SceneInteractionController>(
      *m_commandBus, m_editorState, *m_runtime.scene());
  m_sceneInteraction->setResolveHelperOwner(
      [this](const std::string& path) {
        return m_runtime.resolveEditorHelperOwner(path);
      });
  registerLxeEditorCommands(
      *m_commandBus,
      LxeEditorCommandContext{
          .editorState = m_editorState,
          .scene = *m_runtime.scene(),
          .interaction = *m_sceneInteraction,
          .getEditMode =
              [this]() { return static_cast<int>(m_ui.currentEditorMode()); },
          .setEditMode = [this](const int modeCode) {
            m_ui.setEditorMode(static_cast<UiOverlay::EditorMode>(modeCode));
          },
          .getCameraControlMode = [this]() {
            return static_cast<int>(m_ui.currentCameraControlMode());
          },
          .setCameraControlMode = [this](const int modeCode) {
            m_ui.setCameraControlMode(
                static_cast<UiOverlay::CameraControlMode>(modeCode));
          },
          .sceneViewRect = [this]() { return m_ui.sceneViewRect(m_windowSize); },
          .dirty = [this]() { return m_session.isDirty(); },
          .permission = [this]() {
            return m_session.permission() == ScenePermissionLevel::Admin
                       ? std::string("admin")
                       : std::string("user");
          },
          .debugEnabled = [this]() { return m_debugEnabled; },
          .setDebugEnabled = [this](const bool enabled) {
            m_debugEnabled = enabled;
          },
          .currentDocumentPath = [this]() -> std::optional<std::string> {
            const auto path = m_session.currentDocumentPath();
            return path ? std::optional<std::string>(path->string()) : std::nullopt;
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
          .appendConsoleDebugLine = [this](std::string_view line) {
            if (m_consolePanel) {
              m_consolePanel->appendSystemLine(line);
            }
          },
      });

  m_ui.attach(m_rig, *m_commandBus, m_editorState, m_editorConfig,
              *m_viewportOverlay, *m_sceneTreePanel, *m_inspectorPanel,
              *m_consolePanel,
              [this]() { return m_debugEnabled; });
  ++m_bindingsGeneration;
}

} // namespace LX_demo::lxe_editor
