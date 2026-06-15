#include "editor/app/editor_session.hpp"

#include "editor/commands/command_bus.hpp"
#include "editor/commands/builtin_commands.hpp"
#include "editor/panels/console_panel.hpp"
#include "editor/panels/inspector_panel.hpp"
#include "editor/panels/scene_tree_panel.hpp"
#include "editor/panels/viewport_overlay.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/scene_render_settings.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "editor/project/builtin_asset_catalog.hpp"
#include "editor/project/debug_render_export.hpp"
#include "editor/commands/lxe_editor_commands.hpp"
#include "editor/project/project_catalog.hpp"
#include "editor/project/scene_builder.hpp"
#include "editor/runtime/scene_interaction_controller.hpp"
#include "editor/render/editor_render_view.hpp"
#include "infra/build_info/build_info.hpp"

#include <chrono>
#include <exception>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace LX_demo::lxe_editor {
namespace {

constexpr const char *kDefaultProjectId = "lxe_default";

[[nodiscard]] std::string sanitizeDumpName(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  for (const char c : name) {
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
    out.push_back(safe ? c : '_');
  }
  return out.empty() ? "target" : out;
}

[[nodiscard]] std::filesystem::path pairedScreenDumpPath(
    const std::filesystem::path &targetPath) {
  const auto parent = targetPath.parent_path();
  const std::string stem = targetPath.stem().generic_string();
  return parent / (stem + "-screen.bmp");
}

[[nodiscard]] std::filesystem::path defaultDumpPathForTarget(
    std::string_view targetName) {
  const auto timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  return std::filesystem::path("data/debug/dump") /
         (std::to_string(timestamp) + "-" + sanitizeDumpName(targetName) +
          ".bmp");
}

[[nodiscard]] std::string jsonEscape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  std::ostringstream escapedControl;
  for (const unsigned char c : text) {
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
      if (c < 0x20u) {
        escapedControl.str({});
        escapedControl.clear();
        escapedControl << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<int>(c);
        out += escapedControl.str();
      } else {
        out.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  return out;
}

[[nodiscard]] LX_core::CommandResult makeCommandError(std::string message) {
  return LX_core::CommandResult{false, std::move(message), {}};
}

[[nodiscard]] LX_core::CommandResult
makeCommandOk(std::string message, std::string structured = {}) {
  return LX_core::CommandResult{true, std::move(message),
                                std::move(structured)};
}

[[nodiscard]] const char *
realtimeRenderModeName(const LX_core::SceneRealtimeRenderMode mode) {
  switch (mode) {
  case LX_core::SceneRealtimeRenderMode::Forward:
    return "forward";
  case LX_core::SceneRealtimeRenderMode::Deferred:
    return "deferred";
  }
  return "forward";
}

[[nodiscard]] std::string
cameraControlModeName(const UiOverlay::CameraControlMode mode) {
  switch (mode) {
  case UiOverlay::CameraControlMode::Orbit:
    return "orbit";
  case UiOverlay::CameraControlMode::FreeFly:
    return "freefly";
  }
  return "orbit";
}

[[nodiscard]] bool commandMarksSceneDirty(const std::string &line) {
  const auto firstSpace = line.find(' ');
  const std::string verb =
      firstSpace == std::string::npos ? line : line.substr(0, firstSpace);
  static const std::unordered_set<std::string> kMutatingVerbs = {
      "move",
      "rotate",
      "scale",
      "set",
      "add",
      "remove",
      "paste_as_sibling",
      "undo",
      "redo",
      "__remove_to_stash",
      "__restore_from_stash"};
  return kMutatingVerbs.find(verb) != kMutatingVerbs.end();
}

[[nodiscard]] bool
commandRequestsSceneRebuild(const LX_core::CommandResult &result) {
  const auto it = result.metadata.find("scene.rebuild");
  return it != result.metadata.end() && it->second == "true";
}

[[nodiscard]] bool
commandRequestsCameraRigResync(const LX_core::CommandResult &result) {
  const auto it = result.metadata.find("editor_camera.resync");
  return it != result.metadata.end() && it->second == "true";
}

[[nodiscard]] bool commandRequestsQuit(const LX_core::CommandResult &result) {
  const auto it = result.metadata.find("editor.quit");
  return it != result.metadata.end() && it->second == "true";
}

[[nodiscard]] std::filesystem::path
normalizedAbsolutePath(const std::filesystem::path &path) {
  return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] std::reference_wrapper<LX_core::CameraComponent>
requireCameraComponent(const LX_core::SceneNodeSharedPtr &node,
                       const char *nodeLabel) {
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

void removeDefaultProjectDirectory() {
  std::error_code ec;
  std::filesystem::remove_all(resolveRuntimePath("data/projects") /
                                  kDefaultProjectId,
                              ec);
}

} // namespace

LxeEditorSession::LxeEditorSession(CameraRig &rig, UiOverlay &ui,
                                   LX_core::EditorState &editorState)
    : m_rig(rig), m_ui(ui), m_editorState(editorState),
      m_projectSession(resolveRuntimePath("data/project_templates_disabled"),
                       resolveRuntimePath("data/projects")),
      m_editorDataState(resolveRuntimePath("data/lxe_editor")),
      m_recording(resolveRuntimePath("data/lxe_editor")) {}

LxeEditorSession::~LxeEditorSession() = default;

void LxeEditorSession::initialize(
    DisplayCommandHooks displayCommandHooks,
    RenderDebugCommandHooks renderDebugCommandHooks,
    RealtimeRenderProfileHooks realtimeRenderProfileHooks) {
  m_displayCommandHooks = std::move(displayCommandHooks);
  m_renderDebugCommandHooks = std::move(renderDebugCommandHooks);
  m_realtimeRenderProfileHooks = std::move(realtimeRenderProfileHooks);
  m_editorData = m_editorDataState.load();
  std::optional<EditorSceneStateDocument> editorSceneState;
  bool loadedRuntime = false;
  if (m_editorData.lastProject.has_value()) {
    const auto opened =
        m_projectSession.openProject(m_editorData.lastProject->string());
    if (opened.ok) {
      if (const auto activePath = m_projectSession.activeScenePath();
          activePath.has_value()) {
        try {
          m_runtime.loadFromDocumentPath(*activePath);
          editorSceneState = loadEditorSceneStateIfPresent(*activePath);
          loadedRuntime = true;
        } catch (const std::exception &) {
          loadedRuntime = false;
        }
      }
    }
  }
  if (!loadedRuntime) {
    if (m_projectSession.hasProject()) {
      (void)m_projectSession.closeProject();
      m_editorData.lastProject.reset();
      (void)m_editorDataState.save(m_editorData);
    }
    auto openedDefault = m_projectSession.openProject(kDefaultProjectId);
    if (!openedDefault.ok) {
      openedDefault = m_projectSession.initProject(kDefaultProjectId,
                                                   std::nullopt);
    }
    if (openedDefault.ok) {
      if (const auto activePath = m_projectSession.activeScenePath();
          activePath.has_value()) {
        try {
          m_runtime.loadFromDocumentPath(*activePath);
          editorSceneState = loadEditorSceneStateIfPresent(*activePath);
          loadedRuntime = true;
        } catch (const std::exception &) {
          (void)m_projectSession.closeProject();
          removeDefaultProjectDirectory();
          const auto rebuiltDefault =
              m_projectSession.initProject(kDefaultProjectId, std::nullopt);
          if (rebuiltDefault.ok) {
            if (const auto rebuiltActivePath =
                    m_projectSession.activeScenePath();
                rebuiltActivePath.has_value()) {
              try {
                m_runtime.loadFromDocumentPath(*rebuiltActivePath);
                editorSceneState =
                    loadEditorSceneStateIfPresent(*rebuiltActivePath);
                loadedRuntime = true;
              } catch (const std::exception &) {
                loadedRuntime = false;
              }
            }
          }
        }
      }
    }
    if (loadedRuntime) {
      m_editorData.lastProject = m_projectSession.projectRoot();
      (void)m_editorDataState.save(m_editorData);
    } else {
      m_runtime.createEmptyScene();
    }
  }
  rebuildBindings(std::move(editorSceneState));
}

LX_core::SceneSharedPtr LxeEditorSession::scene() const {
  return m_runtime.scene();
}

LX_core::CameraComponent &LxeEditorSession::editorCamera() const {
  return requireCameraComponent(m_runtime.editorCameraNode(), "editor_camera")
      .get();
}

SceneInteractionController &LxeEditorSession::sceneInteraction() const {
  return *m_sceneInteraction;
}

LX_core::CameraComponent &LxeEditorSession::gameCamera() const {
  return requireCameraComponent(m_runtime.gameCameraNode(), "game_camera")
      .get();
}

bool LxeEditorSession::isDirty() const {
  return isSceneDirty() || isProjectDirty();
}

bool LxeEditorSession::isSceneDirty() const { return m_sceneDirty; }

bool LxeEditorSession::isProjectDirty() const {
  return m_projectSession.dirty();
}

void LxeEditorSession::setWindowSize(const LX_core::Vec2f &size) {
  m_windowSize = size;
}

EditorConfigDocument &LxeEditorSession::editorConfig() {
  return m_editorConfig;
}

LX_core::CommandBus &LxeEditorSession::commandBus() const {
  return *m_commandBus;
}

const LX_core::ConsolePanel &LxeEditorSession::consolePanel() const {
  return *m_consolePanel;
}

usize LxeEditorSession::bindingsGeneration() const {
  return m_bindingsGeneration;
}

bool LxeEditorSession::debugEnabled() const { return m_debugEnabled; }

RecordingController &LxeEditorSession::recording() { return m_recording; }

const RecordingController &LxeEditorSession::recording() const {
  return m_recording;
}

std::optional<std::filesystem::path>
LxeEditorSession::runtimeScenePath() const {
  return m_runtime.documentPath();
}

SceneRuntime &LxeEditorSession::runtime() { return m_runtime; }

const SceneRuntime &LxeEditorSession::runtime() const { return m_runtime; }

std::optional<LX_core::gpu::LiveRenderView>
LxeEditorSession::buildLiveRenderView() const {
  if (!m_runtime.scene()) {
    return std::nullopt;
  }

  const auto editorView =
      LX_editor::buildEditorRenderView(m_editorState, *m_runtime.scene(),
                                      m_windowSize);
  if (!editorView.has_value()) {
    return std::nullopt;
  }
  return LX_core::gpu::LiveRenderView{
      .cameraPath = editorView->cameraPath,
      .cameraResource = editorView->cameraResource,
      .visibleMask = editorView->visibleMask,
      .viewportExtent = editorView->viewportExtent,
      .previewEnabled = editorView->previewEnabled,
      .editorOverlayVisible = editorView->editorOverlayVisible};
}

std::optional<std::string> LxeEditorSession::currentProjectId() const {
  const auto &project = m_projectSession.currentProject();
  if (!project.has_value()) {
    return std::nullopt;
  }
  return project->id;
}

std::optional<std::string>
LxeEditorSession::currentProjectDisplayName() const {
  const auto &project = m_projectSession.currentProject();
  if (!project.has_value()) {
    return std::nullopt;
  }
  return project->displayName;
}

std::optional<std::string>
LxeEditorSession::currentProjectActiveScene() const {
  const auto &project = m_projectSession.currentProject();
  if (!project.has_value()) {
    return std::nullopt;
  }
  return project->activeScene.generic_string();
}

std::optional<std::filesystem::path>
LxeEditorSession::currentProjectRoot() const {
  return m_projectSession.projectRoot();
}

std::optional<std::filesystem::path> LxeEditorSession::activeScenePath() const {
  return m_projectSession.activeScenePath();
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
LxeEditorSession::saveScene(const std::optional<std::string> &path) {
  if (path.has_value()) {
    return makeCommandError(
        "scene save is project-scoped; use scene save without a path");
  }
  return saveActiveProjectScene();
}

LX_core::CommandResult
LxeEditorSession::setRealtimeRenderMode(const std::string_view modeName) {
  const auto currentSettings = m_runtime.scene()->realtimeRenderSettings();
  if (modeName == "status") {
    const char *mode = realtimeRenderModeName(currentSettings.mode);
    return makeCommandOk("realtime render mode: " + std::string(mode),
                         "{\"mode\":\"" + std::string(mode) + "\"}");
  }

  LX_core::SceneRealtimeRenderMode nextMode;
  if (modeName == "forward") {
    nextMode = LX_core::SceneRealtimeRenderMode::Forward;
  } else if (modeName == "deferred") {
    nextMode = LX_core::SceneRealtimeRenderMode::Deferred;
  } else {
    return makeCommandError(
        "usage: realtime-render mode status|forward|deferred");
  }

  if (!m_projectSession.hasProject()) {
    return makeCommandError("no project is open; use project init first");
  }
  const auto activePath = m_projectSession.activeScenePath();
  if (!activePath.has_value()) {
    return makeCommandError("project has no active scene");
  }
  const auto runtimePath = m_runtime.documentPath();
  if (!runtimePath.has_value() ||
      normalizedAbsolutePath(*runtimePath) !=
          normalizedAbsolutePath(*activePath)) {
    return makeCommandError(
        "active project scene is not loaded; wait for scene open to finish");
  }
  if (hasPendingSceneOpen()) {
    return makeCommandError(
        "active scene open is pending; wait for the next update tick");
  }
  if (m_sceneDirty) {
    return makeCommandError(
        "current scene has unsaved edits; run scene save before switching "
        "realtime render mode");
  }

  try {
    SceneDocument document = m_runtime.document();
    LX_core::SceneRealtimeRenderSettings settings =
        document.realtimeRenderSettings();
    settings.mode = nextMode;
    document.setRealtimeRenderSettings(settings);
    saveSceneDocument(*activePath, document);
    m_runtime.scene()->setRealtimeRenderSettings(settings);
    saveEditorSceneStateForScenePath(*activePath, captureEditorSceneState());
    const auto saved = m_projectSession.saveProject();
    if (!saved.ok) {
      return makeCommandError(saved.message);
    }
    const auto queued = queueActiveSceneOpen();
    if (!queued.ok) {
      return makeCommandError(queued.message);
    }
    const char *mode = realtimeRenderModeName(nextMode);
    return makeCommandOk("realtime render mode set to " + std::string(mode) +
                             "; scene reload queued",
                         "{\"mode\":\"" + std::string(mode) +
                             "\",\"status\":\"queued\"}");
  } catch (const std::exception &e) {
    return makeCommandError(e.what());
  }
}

void LxeEditorSession::flushPendingSceneOpen(LX_core::gpu::EngineLoop &loop) {
  if (!hasPendingSceneOpen()) {
    return;
  }

  std::optional<SceneRuntime> pendingRuntime = std::move(m_pendingRuntime);
  const std::optional<std::filesystem::path> nextScenePath = m_pendingScenePath;
  std::optional<EditorSceneStateDocument> nextEditorSceneState =
      std::move(m_pendingEditorSceneState);
  m_pendingRuntime.reset();
  m_pendingScenePath.reset();
  m_pendingEditorSceneState.reset();

  SceneRuntime nextRuntime;
  try {
    if (pendingRuntime.has_value()) {
      nextRuntime = std::move(*pendingRuntime);
    } else {
      if (!nextScenePath.has_value()) {
        throw std::runtime_error("pending scene open has no scene path");
      }
      nextRuntime.loadFromDocumentPath(*nextScenePath);
    }
    loop.startScene(nextRuntime.scene());
  } catch (const std::exception &error) {
    try {
      loop.startScene(m_runtime.scene());
      loop.setLiveRenderView(buildLiveRenderView());
    } catch (const std::exception &restoreError) {
      if (m_consolePanel) {
        m_consolePanel->appendSystemLine(
            std::string("scene restore failed after open error: ") +
            restoreError.what());
      }
      throw;
    }
    if (m_consolePanel) {
      m_consolePanel->appendSystemLine(std::string("scene open failed: ") +
                                       error.what());
    }
    std::cerr << "[lxe_editor] scene open failed: " << error.what() << '\n';
    return;
  }

  m_runtime = std::move(nextRuntime);
  m_sceneDirty = false;
  (void)nextScenePath;
  rebuildBindings(std::move(nextEditorSceneState));
  loop.setLiveRenderView(buildLiveRenderView());
}

void LxeEditorSession::pollCommandHistory(LX_core::gpu::EngineLoop &loop) {
  if (!m_commandBus) {
    return;
  }
  const auto &history = m_commandBus->history();
  while (m_lastObservedHistoryIndex < history.size()) {
    const auto &entry = history[m_lastObservedHistoryIndex++];
    if (entry.result.ok && commandMarksSceneDirty(entry.line)) {
      m_sceneDirty = true;
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

bool LxeEditorSession::hasPendingSceneOpen() const {
  return m_pendingRuntime.has_value() || m_pendingScenePath.has_value();
}

LX_core::CommandResult LxeEditorSession::queueActiveSceneOpen() {
  const auto activePath = m_projectSession.activeScenePath();
  if (!activePath.has_value()) {
    return makeCommandError("no active project scene; use project init first");
  }
  try {
    m_pendingRuntime.reset();
    m_pendingScenePath = *activePath;
    m_pendingEditorSceneState = loadEditorSceneStateIfPresent(*activePath);
    return makeCommandOk(
        "queued scene open for next update tick: " + activePath->string(),
        "{\"path\":\"" + jsonEscape(activePath->string()) +
            "\",\"status\":\"queued\",\"deferredUntil\":\"next_update_tick\"}");
  } catch (const std::exception &e) {
    return makeCommandError(e.what());
  }
}

LX_core::CommandResult LxeEditorSession::saveActiveProjectScene() {
  if (!m_projectSession.hasProject()) {
    return makeCommandError("no project is open; use project init first");
  }
  const auto activePath = m_projectSession.activeScenePath();
  if (!activePath.has_value()) {
    return makeCommandError("project has no active scene");
  }
  if (hasPendingSceneOpen()) {
    return makeCommandError(
        "active scene open is pending; wait for the next update tick");
  }
  const auto runtimePath = m_runtime.documentPath();
  if (!runtimePath.has_value() ||
      normalizedAbsolutePath(*runtimePath) !=
          normalizedAbsolutePath(*activePath)) {
    return makeCommandError(
        "active project scene is not loaded; wait for scene open to finish");
  }
  try {
    m_runtime.saveToDocumentPath(*activePath);
    saveEditorSceneStateForScenePath(*activePath, captureEditorSceneState());
    m_sceneDirty = false;
    const auto saved = m_projectSession.saveProject();
    if (!saved.ok) {
      return makeCommandError(saved.message);
    }
    m_editorData.lastProject = m_projectSession.projectRoot();
    persistEditorData();
    const auto projectFile =
        (*m_projectSession.projectRoot() / "project.yaml").lexically_normal();
    return makeCommandOk("saved project " + projectFile.string() +
                             " and scene " + activePath->string(),
                         "{\"path\":\"" + jsonEscape(activePath->string()) +
                             "\",\"project\":" + projectSummaryJson() + "}");
  } catch (const std::exception &e) {
    return makeCommandError(e.what());
  }
}

std::string LxeEditorSession::realtimeRenderProfilesJson() const {
  const SceneDocument &document = m_runtime.document();
  const LX_core::offline::RenderProfileDocument profiles =
      document.hasRenderProfileDocument()
          ? document.renderProfileDocument()
          : LX_core::offline::makeDefaultRenderProfileDocument();

  std::map<std::string, LX_core::offline::OutputProfile> orderedProfiles(
      profiles.outputProfiles.begin(), profiles.outputProfiles.end());

  std::ostringstream oss;
  oss << "{\"sceneName\":\"" << jsonEscape(document.sceneName())
      << "\",\"defaultOutputProfile\":\""
      << jsonEscape(profiles.defaultOutputProfile) << "\",\"profiles\":[";
  usize index = 0;
  for (const auto &[name, profile] : orderedProfiles) {
    if (index++ != 0) {
      oss << ',';
    }
    oss << "{\"name\":\"" << jsonEscape(name) << "\",\"camera\":\""
        << jsonEscape(profile.cameraPath) << "\",\"width\":" << profile.width
        << ",\"height\":" << profile.height << ",\"outputFormat\":\""
        << jsonEscape(profile.outputFormat) << "\",\"outDir\":\""
        << jsonEscape(profile.outDir.generic_string()) << "\"}";
  }
  oss << "]}";
  return oss.str();
}

LX_core::CommandResult
LxeEditorSession::runRealtimeRenderProfile(std::string_view profileName) {
  if (!m_realtimeRenderProfileHooks.generate) {
    return makeCommandError("realtime render output hook unavailable");
  }
  if (!m_projectSession.hasProject()) {
    return makeCommandError("no project is open; use project open first");
  }
  const auto activePath = m_projectSession.activeScenePath();
  if (!activePath.has_value()) {
    return makeCommandError("project has no active scene");
  }
  if (hasPendingSceneOpen()) {
    return makeCommandError(
        "active scene open is pending; wait for the next update tick");
  }
  const auto runtimePath = m_runtime.documentPath();
  if (!runtimePath.has_value() ||
      normalizedAbsolutePath(*runtimePath) != normalizedAbsolutePath(*activePath)) {
    return makeCommandError(
        "active project scene is not loaded; wait for scene open to finish");
  }

  try {
    const SceneDocument &document = m_runtime.document();
    const LX_core::offline::RenderProfileDocument profiles =
        document.hasRenderProfileDocument()
            ? document.renderProfileDocument()
            : LX_core::offline::makeDefaultRenderProfileDocument();
    const LX_core::offline::ResolvedRenderProfile resolved =
        LX_core::offline::resolveRenderProfileDocument(
            profiles,
            LX_core::offline::RenderProfileCliOverrides{
                .profileName = std::string(profileName),
            });
    RealtimeProfileOutputRequest request{
        .scenePath = m_runtime.documentPath().value_or(std::filesystem::path{}),
        .sceneName = document.sceneName(),
        .profileName = resolved.profileName,
        .output = resolved.output,
        .outputBasePath = makeRealtimeProfileOutputBasePath(
            document.sceneName(), resolved.profileName, resolved.output),
    };
    const RealtimeProfileOutputResult result =
        m_realtimeRenderProfileHooks.generate(m_runtime.scene(), request);
    return makeCommandOk(
        "realtime render profile generated: " +
            result.metadataPath.generic_string(),
        realtimeProfileOutputResultJson(resolved.profileName, result));
  } catch (const std::exception &e) {
    return makeCommandError(e.what());
  }
}

LX_core::CommandResult
LxeEditorSession::handleProjectCommand(const std::vector<std::string> &args) {
  if (args.empty() || args[0] == "status") {
    return makeCommandOk(projectSummaryJson(), projectSummaryJson());
  }
  if (args[0] == "templates") {
    if (args.size() > 2 || (args.size() == 2 && args[1] != "list")) {
      return makeCommandError("usage: project templates [list]");
    }
    std::ostringstream oss;
    oss << "{\"templates\":[";
    const ProjectTemplateCatalog catalog(
        resolveRuntimePath("data/project_templates_disabled"));
    const auto &entries = catalog.entries();
    for (usize i = 0; i < entries.size(); ++i) {
      if (i != 0) {
        oss << ',';
      }
      oss << "{\"id\":\"" << jsonEscape(entries[i].id)
          << "\",\"displayName\":\"" << jsonEscape(entries[i].displayName)
          << "\",\"path\":\"" << jsonEscape(entries[i].path.string()) << "\"}";
    }
    oss << "]}";
    return makeCommandOk("listed project templates", oss.str());
  }
  if (args[0] == "list") {
    ProjectCatalog catalog(resolveRuntimePath("data/projects"));
    catalog.refresh();
    std::ostringstream oss;
    std::ostringstream message;
    message << "projects:";
    oss << "{\"projects\":[";
    const auto &entries = catalog.entries();
    if (entries.empty()) {
      message << " <none>";
    }
    for (usize i = 0; i < entries.size(); ++i) {
      if (i != 0) {
        oss << ',';
      }
      message << "\n- " << entries[i].id << " (" << entries[i].displayName
              << "): "
              << (entries[i].path / "project.yaml").lexically_normal().string();
      oss << "{\"id\":\"" << jsonEscape(entries[i].id)
          << "\",\"displayName\":\"" << jsonEscape(entries[i].displayName)
          << "\",\"path\":\"" << jsonEscape(entries[i].path.string()) << "\"}";
    }
    oss << "]}";
    return makeCommandOk(message.str(), oss.str());
  }
  if (args[0] == "init") {
    if (args.size() < 2) {
      return makeCommandError(
          "usage: project init <template-id> [project-name]");
    }
    std::optional<std::string> projectName;
    if (args.size() > 2) {
      projectName = args[2];
      for (usize i = 3; i < args.size(); ++i) {
        *projectName += " " + args[i];
      }
    }
    const auto initialized = m_projectSession.initProject(args[1], projectName);
    if (!initialized.ok) {
      return makeCommandError(initialized.message);
    }
    m_editorData.lastProject = m_projectSession.projectRoot();
    persistEditorData();
    LX_core::CommandResult queued = queueActiveSceneOpen();
    if (!queued.ok) {
      return makeCommandOk("project initialized; active scene open failed: " +
                               queued.message,
                           initialized.structuredJson);
    }
    return makeCommandOk(initialized.message, initialized.structuredJson);
  }
  if (args[0] == "open") {
    if (args.size() != 2) {
      return makeCommandError("usage: project open <project-id-or-path>");
    }
    const auto opened = m_projectSession.openProject(args[1]);
    if (!opened.ok) {
      return makeCommandError(opened.message);
    }
    m_editorData.lastProject = m_projectSession.projectRoot();
    persistEditorData();
    const auto queued = queueActiveSceneOpen();
    if (!queued.ok) {
      return makeCommandError("project opened but active scene open failed: " +
                              queued.message);
    }
    return makeCommandOk(opened.message, opened.structuredJson);
  }
  if (args[0] == "save") {
    if (args.size() != 1) {
      return makeCommandError("usage: project save");
    }
    return saveActiveProjectScene();
  }
  if (args[0] == "close") {
    if (args.size() != 1) {
      return makeCommandError("usage: project close");
    }
    const auto closed = m_projectSession.closeProject();
    m_editorData.lastProject.reset();
    persistEditorData();
    m_pendingRuntime.reset();
    m_pendingScenePath.reset();
    m_pendingEditorSceneState.reset();
    SceneRuntime emptyRuntime;
    emptyRuntime.createEmptyScene();
    m_pendingRuntime = std::move(emptyRuntime);
    return makeCommandOk(closed.message, closed.structuredJson);
  }
  return makeCommandError(
      "usage: project templates [list] | project list | project init "
      "<template-id> [project-name] | project open <project-id-or-path> | "
      "project save | project status | project close");
}

LX_core::CommandResult
LxeEditorSession::handleSceneCommand(const std::vector<std::string> &args) {
  if (args.empty() || args[0] == "status") {
    return makeCommandOk(projectSummaryJson(), projectSummaryJson());
  }
  if (args[0] == "list") {
    const auto &project = m_projectSession.currentProject();
    if (!project.has_value()) {
      return makeCommandError("no project is open; use project init first");
    }
    std::ostringstream oss;
    oss << "{\"scenes\":[";
    for (usize i = 0; i < project->scenes.size(); ++i) {
      if (i != 0) {
        oss << ',';
      }
      const auto &scene = project->scenes[i];
      oss << "{\"id\":\"" << jsonEscape(scene.id) << "\",\"path\":\""
          << jsonEscape(scene.path.generic_string()) << "\"}";
    }
    oss << "],\"activeScene\":\""
        << jsonEscape(project->activeScene.generic_string()) << "\"}";
    return makeCommandOk("listed project scenes", oss.str());
  }
  if (args[0] == "save") {
    if (args.size() != 1) {
      return makeCommandError("usage: scene save");
    }
    return saveActiveProjectScene();
  }
  if (args[0] == "open") {
    if (args.size() != 2) {
      return makeCommandError("usage: scene open <scene-id-or-path>");
    }
    const auto opened = m_projectSession.openScene(args[1]);
    if (!opened.ok) {
      return makeCommandError(opened.message);
    }
    const auto queued = queueActiveSceneOpen();
    if (!queued.ok) {
      return makeCommandError(queued.message);
    }
    return makeCommandOk(opened.message, opened.structuredJson);
  }
  if (args[0] == "new") {
    if (args.size() != 2) {
      return makeCommandError("usage: scene new <scene-id>");
    }
    const auto created = m_projectSession.newScene(args[1]);
    if (!created.ok) {
      return makeCommandError(created.message);
    }
    const auto queued = queueActiveSceneOpen();
    if (!queued.ok) {
      return makeCommandError(queued.message);
    }
    return makeCommandOk(created.message, created.structuredJson);
  }
  if (args[0] == "duplicate") {
    if (args.size() != 3) {
      return makeCommandError("usage: scene duplicate <source-id> <new-id>");
    }
    const auto duplicated = m_projectSession.duplicateScene(args[1], args[2]);
    if (!duplicated.ok) {
      return makeCommandError(duplicated.message);
    }
    const auto queued = queueActiveSceneOpen();
    if (!queued.ok) {
      return makeCommandError(queued.message);
    }
    return makeCommandOk(duplicated.message, duplicated.structuredJson);
  }
  if (args[0] == "import") {
    if (args.size() != 3) {
      return makeCommandError("usage: scene import <source-path> <scene-id>");
    }
    const auto imported = m_projectSession.importScene(args[1], args[2]);
    if (!imported.ok) {
      return makeCommandError(imported.message);
    }
    const auto queued = queueActiveSceneOpen();
    if (!queued.ok) {
      return makeCommandError(queued.message);
    }
    return makeCommandOk(imported.message, imported.structuredJson);
  }
  if (args[0] == "remove") {
    if (args.size() != 2) {
      return makeCommandError("usage: scene remove <scene-id>");
    }
    const auto removed = m_projectSession.removeScene(args[1]);
    if (!removed.ok) {
      return makeCommandError(removed.message);
    }
    return makeCommandOk(removed.message, removed.structuredJson);
  }
  return makeCommandError(
      "usage: scene list | scene open <scene-id-or-path> | scene save | "
      "scene new <scene-id> | scene duplicate <source-id> <new-id> | "
      "scene import <source-path> <scene-id> | scene remove <scene-id> | "
      "scene status");
}

std::string LxeEditorSession::projectSummaryJson() const {
  const auto &project = m_projectSession.currentProject();
  const auto &projectRoot = m_projectSession.projectRoot();
  if (!project.has_value() || !projectRoot.has_value()) {
    return "null";
  }
  const auto activePath = m_projectSession.activeScenePath();
  const auto runtimePath = m_runtime.documentPath();
  const bool sceneOpenPending = hasPendingSceneOpen();
  const bool activeSceneLoaded =
      activePath.has_value() && runtimePath.has_value() &&
      normalizedAbsolutePath(*runtimePath) ==
          normalizedAbsolutePath(*activePath) &&
      !sceneOpenPending;
  std::ostringstream oss;
  oss << "{\"id\":\"" << jsonEscape(project->id) << "\",\"displayName\":\""
      << jsonEscape(project->displayName) << "\",\"path\":\""
      << jsonEscape(projectRoot->string())
      << "\",\"dirty\":" << (m_projectSession.dirty() ? "true" : "false")
      << ",\"activeScene\":\""
      << jsonEscape(project->activeScene.generic_string()) << "\""
      << ",\"loadedScene\":\""
      << jsonEscape(runtimePath.has_value() ? runtimePath->string()
                                            : std::string{})
      << "\",\"activeSceneLoaded\":"
      << (activeSceneLoaded ? "true" : "false")
      << ",\"sceneOpenPending\":"
      << (sceneOpenPending ? "true" : "false") << "}";
  return oss.str();
}

EditorSceneStateDocument LxeEditorSession::captureEditorSceneState() const {
  EditorSceneStateDocument state;
  state.editorCamera = EditorCameraState::captureFrom(
      *m_runtime.editorCameraNode(), editorCamera());
  state.orbitTarget = m_rig.orbitTarget();
  const auto selected = m_editorState.getSelected();
  state.selectedPaths.reserve(selected.size());
  for (const auto &node : selected) {
    if (node) {
      state.selectedPaths.push_back(node->getPath());
    }
  }
  return state;
}

void LxeEditorSession::applyEditorSceneState(
    const EditorSceneStateDocument &state) {
  if (state.editorCamera.has_value()) {
    auto &camera = editorCamera();
    state.editorCamera->applyTo(*m_runtime.editorCameraNode(), camera);
    camera.updateMatrices();
  }
  if (state.orbitTarget.has_value()) {
    m_rig.setOrbitTarget(*state.orbitTarget);
  }

  std::vector<LX_core::SceneNodeSharedPtr> selectedNodes;
  selectedNodes.reserve(state.selectedPaths.size());
  for (const auto &path : state.selectedPaths) {
    if (LX_core::SceneNode *node = m_runtime.scene()->findByPath(path)) {
      selectedNodes.push_back(node->shared_from_this());
    }
  }
  m_editorState.select(std::move(selectedNodes));
}

void LxeEditorSession::rebuildBindings(
    std::optional<EditorSceneStateDocument> editorSceneState) {
  const bool previewEnabled = m_editorState.isPreviewEnabled();
  m_editorState.deselect();
  m_editorState.setEditorCamera(m_runtime.editorCameraNode());
  m_editorState.setPreviewCamera(m_runtime.gameCameraNode());
  m_editorState.setPreviewEnabled(previewEnabled);
  (void)m_editorState.syncActiveCamera(*m_runtime.scene());

  m_rig.attach(editorCamera());
  if (editorSceneState.has_value()) {
    applyEditorSceneState(*editorSceneState);
  }

  if (!m_commandBus) {
    m_commandBus = std::make_unique<LX_core::CommandBus>();
  }
  LX_core::registerBuiltinCommands(
      *m_commandBus, m_editorState, *m_runtime.scene(),
      LX_core::SceneIoContext{
          .open =
              [](const std::string &) {
                return makeCommandError(
                    "scene command removed; use scene open");
              },
          .save =
              [this](const std::optional<std::string> &path) {
                return saveScene(path);
              },
          .list =
              []() {
                return makeCommandError(
                    "scene list is project-scoped; use scene list");
              },
          .cameraControl =
              [this](const std::vector<std::string> &args) {
                if (args.size() != 2) {
                  return makeCommandError(
                      "usage: cam control (orbit|freefly|status)");
                }
                if (args[1] == "status") {
                  const std::string camera =
                      cameraControlModeName(m_ui.currentCameraControlMode());
                  LX_core::CommandResult result = makeCommandOk(
                      "camera " + camera, "{\"camera\":\"" + camera + "\"}");
                  result.metadata[std::string(
                      LX_core::kCommandResultClearRedoOnSuccessMetadataKey)] =
                      "false";
                  result.metadata[std::string(
                      LX_core::kCommandResultClearUndoOnSuccessMetadataKey)] =
                      "false";
                  return result;
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
                LX_core::CommandResult result = makeCommandOk(
                    "camera " + camera, "{\"camera\":\"" + camera + "\"}");
                result.metadata["inverse.line"] =
                    "cam control " + cameraControlModeName(previous);
                return result;
              },
          .defaultAddPlacement = [this]() { return m_rig.orbitTarget(); },
          .createNode =
              [](const std::string &kind, const std::string &nodeName,
                 const std::string &displayName,
                 LX_core::SceneNodeSharedPtr &outNode) {
                if (kind.rfind("model:", 0) == 0) {
                  const std::string assetId =
                      kind.substr(std::string("model:").size());
                  return makeCommandError("builtin model assets removed: " +
                                          assetId);
                }
                if (kind.rfind("patch:", 0) == 0) {
                  const std::string shape =
                      kind.substr(std::string("patch:").size());
                  const std::string meshUri =
                      "builtin://lxe_editor/patches/" + shape;
                  try {
                    outNode = buildBuiltinPatchNode(meshUri, nodeName);
                    outNode->setName(displayName);
                    return makeCommandOk("created " + kind);
                  } catch (const std::exception &e) {
                    return makeCommandError(e.what());
                  }
                }
                if (kind.rfind("primitive:", 0) != 0) {
                  return makeCommandError("unsupported create kind: " + kind);
                }
                const std::string shape =
                    kind.substr(std::string("primitive:").size());
                const std::string meshUri =
                    "builtin://lxe_editor/primitives/" + shape;
                try {
                  outNode = buildBuiltinPrimitiveNode(meshUri, nodeName);
                  outNode->setName(displayName);
                  return makeCommandOk("created " + kind);
                } catch (const std::exception &e) {
                  return makeCommandError(e.what());
                }
              },
          .getMaterialUri =
              [this](const std::string &path) {
                return m_runtime.materialUriForNode(path);
              },
          .setMaterialUri =
              [this](const std::string &path, const std::string &uri) {
                return m_runtime.setNodeMaterialUri(path, uri);
              },
          .getNodeMaterialBaseColor =
              [this](const std::string &path) {
                return m_runtime.nodeMaterialBaseColorForNode(path);
              },
          .setNodeMaterialBaseColor =
              [this](const std::string &path, const LX_core::Vec3f &color) {
                return m_runtime.setNodeMaterialBaseColor(path, color);
              },
          .getProceduralMaterialEnabled =
              [this](const std::string &path) {
                return m_runtime.proceduralMaterialEnabledForNode(path);
              },
          .setProceduralMaterialEnabled =
              [this](const std::string &path, const bool enabled) {
                return m_runtime.setNodeProceduralMaterialEnabled(path,
                                                                  enabled);
              },
          .getNodeMaterialParameter =
              [this](const std::string &path, const std::string &binding,
                     const std::string &member) {
                return m_runtime.nodeMaterialParameterForNode(path, binding,
                                                              member);
              },
          .setNodeMaterialParameter =
              [this](const std::string &path, const std::string &binding,
                     const std::string &member,
                     const LX_core::MaterialParameterValue &value) {
                return m_runtime.setNodeMaterialParameter(path, binding, member,
                                                          value);
              },
          .clearNodeMaterialParameter =
              [this](const std::string &path, const std::string &binding,
                     const std::string &member) {
                return m_runtime.clearNodeMaterialParameter(path, binding,
                                                            member);
              },
          .applyMaterialOverride =
              [this](const std::string &path, const std::string &field) {
                return m_runtime.applyMaterialOverride(path, field);
              },
      });
  if (!m_consolePanel) {
    m_consolePanel = std::make_unique<LX_core::ConsolePanel>(*m_commandBus);
    m_consolePanel->setPersistedHistory(m_editorData.consoleHistory);
  }
  m_sceneTreePanel = std::make_unique<LX_core::SceneTreePanel>(
      *m_commandBus, m_editorState, *m_runtime.scene());
  m_inspectorPanel = std::make_unique<LX_core::InspectorPanel>(
      *m_commandBus, m_editorState,
      LX_core::InspectorMaterialCallbacks{
          .materialUri =
              [this](const std::string &path) {
                return m_runtime.materialUriForNode(path);
              },
          .nodeBaseColor =
              [this](const std::string &path) {
                return m_runtime.nodeMaterialBaseColorForNode(path);
              },
          .canEditBaseColor =
              [this](const std::string &path) {
                return m_runtime.nodeMaterialBaseColorEditable(path);
              },
          .proceduralMaterialEnabled =
              [this](const std::string &path) {
                return m_runtime.proceduralMaterialEnabledForNode(path);
              },
          .presets = [this]() { return m_runtime.materialPresets(); },
          .materialParameters =
              [this](const std::string &path) {
                std::vector<LX_core::MaterialParameterEditorValue> out;
                const auto runtimeValues =
                    m_runtime.nodeMaterialParametersForNode(path);
                out.reserve(runtimeValues.size());
                for (const auto &value : runtimeValues) {
                  out.push_back(LX_core::MaterialParameterEditorValue{
                      .binding = value.binding,
                      .member = value.member,
                      .value = value.value,
                      .runtimeOwned = value.runtimeOwned});
                }
                return out;
              },
          .realtimeRenderMode =
              [this]() -> std::optional<LX_core::SceneRealtimeRenderMode> {
            return m_runtime.scene()->realtimeRenderSettings().mode;
          },
          .setRealtimeRenderMode =
              [this](const LX_core::SceneRealtimeRenderMode mode) {
                switch (mode) {
                case LX_core::SceneRealtimeRenderMode::Forward:
                  return setRealtimeRenderMode("forward");
                case LX_core::SceneRealtimeRenderMode::Deferred:
                  return setRealtimeRenderMode("deferred");
                }
                return makeCommandError("unsupported realtime render mode");
              },
      });
  m_viewportOverlay = std::make_unique<LX_core::ViewportOverlay>(
      *m_commandBus, m_editorState, *m_runtime.scene());
  m_sceneInteraction = std::make_unique<SceneInteractionController>(
      *m_commandBus, m_editorState, *m_runtime.scene());
  m_sceneInteraction->setBoxSelectionDispatch(
      [this](const LX_core::Vec2f &dragStart, const LX_core::Vec2f &dragEnd,
             const SceneViewRect &sceneViewRect, const bool ctrlHeld,
             const bool shiftHeld) {
        if (!m_viewportOverlay) {
          return LX_core::CommandResult{
              false, "viewport overlay unavailable", {}, {}};
        }
        return m_viewportOverlay->dispatchBoxSelection(
            sceneViewRect.localPixel(dragStart),
            sceneViewRect.localPixel(dragEnd), sceneViewRect.size(), ctrlHeld,
            shiftHeld);
      });
  registerLxeEditorCommands(
      *m_commandBus,
      LxeEditorCommandContext{
          .editorState = m_editorState,
          .scene = *m_runtime.scene(),
          .interaction = *m_sceneInteraction,
          .getEditMode =
              [this]() { return static_cast<int>(m_ui.currentEditorMode()); },
          .setEditMode =
              [this](const int modeCode) {
                m_ui.setEditorMode(
                    static_cast<UiOverlay::EditorMode>(modeCode));
              },
          .getCameraControlMode =
              [this]() {
                return static_cast<int>(m_ui.currentCameraControlMode());
              },
          .setCameraControlMode =
              [this](const int modeCode) {
                m_ui.setCameraControlMode(
                    static_cast<UiOverlay::CameraControlMode>(modeCode));
              },
          .sceneViewRect =
              [this]() { return m_ui.sceneViewRect(m_windowSize); },
          .dirty = [this]() { return isDirty(); },
          .debugEnabled = [this]() { return m_debugEnabled; },
          .setDebugEnabled =
              [this](const bool enabled) { m_debugEnabled = enabled; },
          .runtimeScenePath = [this]() -> std::optional<std::string> {
            const auto path = runtimeScenePath();
            return path ? std::optional<std::string>(path->string())
                        : std::nullopt;
          },
          .projectCommand =
              [this](const std::vector<std::string> &args) {
                return handleProjectCommand(args);
              },
          .sceneCommand =
              [this](const std::vector<std::string> &args) {
                return handleSceneCommand(args);
              },
          .projectSummaryJson = [this]() { return projectSummaryJson(); },
          .persistedHistory =
              [this]() {
                return m_consolePanel ? m_consolePanel->persistedHistory()
                                      : std::vector<std::string>{};
              },
          .appendConsoleDebugLine =
              [this](std::string_view line) {
                if (m_consolePanel) {
                  m_consolePanel->appendSystemLine(line);
                }
              },
          .recording = [this]()
              -> std::optional<std::reference_wrapper<RecordingController>> {
            return m_recording;
          },
          .buildInfoJson =
              []() { return LX_infra::currentBuildInfoJson("lxe_editor"); },
          .displayListJson = m_displayCommandHooks.displayListJson,
          .displayActiveJson = m_displayCommandHooks.displayActiveJson,
          .displayConfigGetJson = m_displayCommandHooks.displayConfigGetJson,
          .displayConfigSet = m_displayCommandHooks.displayConfigSet,
          .displaySelect = m_displayCommandHooks.displaySelect,
          .displayNext = m_displayCommandHooks.displayNext,
          .realtimeRenderListJson =
              [this]() { return realtimeRenderProfilesJson(); },
          .realtimeRenderRun =
              [this](std::string_view profileName) {
                return runRealtimeRenderProfile(profileName);
              },
          .realtimeRenderMode =
              [this](std::string_view modeName) {
                return setRealtimeRenderMode(modeName);
              },
      });
  m_commandBus->registerHandler(
      "render",
      "render debug dump <target> [camera-path] [path] | render debug "
      "stats <target> | render debug live-stats | render debug export-path "
      "color-transfer [camera-path] [out-dir]",
      [this](std::vector<std::string> args) {
        if (args.size() == 2 && args[0] == "debug" &&
            args[1] == "live-stats") {
          if (!m_renderDebugCommandHooks.liveRenderSubmissionStats) {
            return makeCommandError("render debug live-stats unavailable");
          }
          const auto stats =
              m_renderDebugCommandHooks.liveRenderSubmissionStats();
          std::ostringstream structured;
          structured << "{"
                     << "\"compilerInputCount\":" << stats.compilerInputCount
                     << ",\"acceptedInputCount\":" << stats.acceptedInputCount
                     << ",\"rejectedInputCount\":" << stats.rejectedInputCount
                     << ",\"submittedDrawCount\":" << stats.submittedDrawCount
                     << ",\"submittedDispatchCount\":"
                     << stats.submittedDispatchCount
                     << ",\"fallbackObservedCount\":"
                     << stats.fallbackObservedCount
                     << ",\"descPipelineLookupCount\":"
                     << stats.descPipelineLookupCount
                     << ",\"descBoundInputCount\":"
                     << stats.descBoundInputCount
                     << ",\"descExecutedInputCount\":"
                     << stats.descExecutedInputCount
                     << ",\"bindlessSceneDescriptorCount\":"
                     << stats.bindlessSceneDescriptorCount
                     << ",\"usedExplicitCamera\":"
                     << (stats.usedExplicitCamera ? "true" : "false")
                     << ",\"usedBindlessSceneDescriptors\":"
                     << (stats.usedBindlessSceneDescriptors ? "true" : "false")
                     << "}";
          return makeCommandOk("render debug live-stats", structured.str());
        }
        if (args.size() == 3 && args[0] == "debug" &&
            args[1] == "stats") {
          if (!m_renderDebugCommandHooks.statsRenderTarget) {
            return makeCommandError("render debug stats unavailable");
          }
          try {
            const RenderDebugDumpResult dump =
                m_renderDebugCommandHooks.statsRenderTarget(args[2]);
            std::ostringstream structured;
            structured << "{\"width\":" << dump.width
                       << ",\"height\":" << dump.height << ",\"format\":\""
                       << jsonEscape(dump.format) << "\""
                       << ",\"stats\":{\"min\":" << dump.minValue
                       << ",\"max\":" << dump.maxValue
                       << ",\"mean\":" << dump.meanValue
                       << ",\"nonZeroRatio\":" << dump.nonZeroRatio << "}}";
            return makeCommandOk("render target stats: " + args[2],
                                 structured.str());
          } catch (const std::exception &e) {
            return makeCommandError(e.what());
          }
        }
        if (args.size() >= 3 && args[0] == "debug" &&
            args[1] == "export-path" && args[2] == "color-transfer") {
          if (!m_renderDebugCommandHooks.exportColorTransferPath) {
            return makeCommandError(
                "render debug export-path color-transfer unavailable");
          }
          if (args.size() > 5) {
            return makeCommandError(
                "usage: render debug export-path color-transfer "
                "[camera-path] [out-dir]");
          }

          LX_core::backend::VulkanDebugColorTransferExportRequest request;
          if (args.size() >= 4) {
            request.cameraPath = args[3];
          }
          if (args.size() == 5) {
            request.outputDirectory = std::filesystem::path(args[4]);
          }

          try {
            const auto result =
                m_renderDebugCommandHooks.exportColorTransferPath(request);
            return makeCommandOk(
                "debug color transfer exported: " +
                    result.manifestPath.generic_string(),
                debugColorTransferExportResultJson(result));
          } catch (const std::exception &e) {
            return makeCommandError(e.what());
          }
        }

        if (args.size() < 3 || args[0] != "debug" || args[1] != "dump" ||
            args.size() > 5) {
          return makeCommandError(
              "usage: render debug dump <target> [camera-path] [path] | "
              "render debug stats <target> | render debug live-stats | "
              "render debug export-path color-transfer [camera-path] "
              "[out-dir]");
        }
        if (!m_renderDebugCommandHooks.dumpRenderTarget) {
          return makeCommandError("render debug dump unavailable");
        }

        const bool targetIsPass =
            args[2] == "Forward" || args[2] == "DebugOverlay";
        std::optional<std::string> cameraPath;
        std::filesystem::path outputPath = defaultDumpPathForTarget(args[2]);
        if (args.size() == 4 && targetIsPass) {
          cameraPath = args[3];
        } else if (args.size() == 4) {
          outputPath = std::filesystem::path(args[3]);
        } else if (args.size() == 5) {
          cameraPath = args[3];
          outputPath = std::filesystem::path(args[4]);
        }
        try {
          const RenderDebugDumpResult dump =
              m_renderDebugCommandHooks.dumpRenderTarget(args[2], cameraPath,
                                                         outputPath);
          std::ostringstream structured;
          structured << "{\"path\":\""
                     << jsonEscape(dump.path.generic_string()) << "\"";
          if (!dump.screenPath.empty()) {
            structured << ",\"screenPath\":\""
                       << jsonEscape(dump.screenPath.generic_string()) << "\"";
          }
          structured << ",\"width\":" << dump.width
                     << ",\"height\":" << dump.height << ",\"format\":\""
                     << jsonEscape(dump.format) << "\""
                     << ",\"stats\":{\"min\":" << dump.minValue
                     << ",\"max\":" << dump.maxValue
                     << ",\"mean\":" << dump.meanValue
                     << ",\"nonZeroRatio\":" << dump.nonZeroRatio << "}}";
          return makeCommandOk(
              "render target dumped: " + dump.path.generic_string(),
              structured.str());
        } catch (const std::exception &e) {
          return makeCommandError(e.what());
        }
      });

  m_ui.attach(
      m_rig, *m_commandBus, m_editorState, m_editorConfig, *m_viewportOverlay,
      *m_sceneTreePanel, *m_inspectorPanel, *m_consolePanel,
      [this]() { return m_debugEnabled; },
      [this]() { return m_recording.status(); });
  ++m_bindingsGeneration;
}

} // namespace LX_demo::lxe_editor
