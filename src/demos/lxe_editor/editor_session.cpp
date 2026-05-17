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
#include "demos/lxe_editor/builtin_asset_catalog.hpp"
#include "demos/lxe_editor/lxe_editor_build_info.hpp"
#include "demos/lxe_editor/lxe_editor_commands.hpp"
#include "demos/lxe_editor/project_catalog.hpp"
#include "demos/lxe_editor/scene_builder.hpp"
#include "demos/lxe_editor/scene_interaction_controller.hpp"

#include <exception>
#include <functional>
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

[[nodiscard]] std::string jsonEscape(const std::string &text) {
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

[[nodiscard]] LX_core::CommandResult
makeCommandOk(std::string message, std::string structured = {}) {
  return LX_core::CommandResult{true, std::move(message),
                                std::move(structured)};
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

} // namespace

LxeEditorSession::LxeEditorSession(CameraRig &rig, UiOverlay &ui,
                                   LX_core::EditorState &editorState)
    : m_rig(rig), m_ui(ui), m_editorState(editorState),
      m_projectSession(resolveRuntimePath("assets/project_templates"),
                       resolveRuntimePath("data/projects")),
      m_editorDataState(resolveRuntimePath("data/lxe_editor")),
      m_recording(resolveRuntimePath("data/lxe_editor")) {}

LxeEditorSession::~LxeEditorSession() = default;

void LxeEditorSession::initialize(DisplayCommandHooks displayCommandHooks) {
  m_displayCommandHooks = std::move(displayCommandHooks);
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
    m_runtime.createEmptyScene();
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

bool LxeEditorSession::isDirty() const { return m_projectSession.dirty(); }

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

void LxeEditorSession::flushPendingSceneOpen(LX_core::gpu::EngineLoop &loop) {
  if (!m_pendingRuntime.has_value()) {
    return;
  }

  SceneRuntime nextRuntime = std::move(*m_pendingRuntime);
  const std::optional<std::filesystem::path> nextScenePath = m_pendingScenePath;
  std::optional<EditorSceneStateDocument> nextEditorSceneState =
      std::move(m_pendingEditorSceneState);
  m_pendingRuntime.reset();
  m_pendingScenePath.reset();
  m_pendingEditorSceneState.reset();

  try {
    loop.startScene(nextRuntime.scene());
  } catch (const std::exception &error) {
    try {
      loop.startScene(m_runtime.scene());
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
    return;
  }

  m_runtime = std::move(nextRuntime);
  (void)nextScenePath;
  rebuildBindings(std::move(nextEditorSceneState));
}

void LxeEditorSession::pollCommandHistory(LX_core::gpu::EngineLoop &loop) {
  if (!m_commandBus) {
    return;
  }
  const auto &history = m_commandBus->history();
  while (m_lastObservedHistoryIndex < history.size()) {
    const auto &entry = history[m_lastObservedHistoryIndex++];
    if (entry.result.ok && commandMarksSceneDirty(entry.line)) {
      m_projectSession.setDirty(true);
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

LX_core::CommandResult LxeEditorSession::queueActiveSceneOpen() {
  const auto activePath = m_projectSession.activeScenePath();
  if (!activePath.has_value()) {
    return makeCommandError("no active project scene; use project init first");
  }
  try {
    SceneRuntime loaded;
    loaded.loadFromDocumentPath(*activePath);
    const auto loadedPath = loaded.documentPath();
    if (!loadedPath.has_value()) {
      return makeCommandError("queued scene open produced no document path");
    }
    m_pendingRuntime = std::move(loaded);
    m_pendingScenePath = *loadedPath;
    m_pendingEditorSceneState = loadEditorSceneStateIfPresent(*loadedPath);
    return makeCommandOk(
        "queued scene open for next update tick: " + loadedPath->string(),
        "{\"path\":\"" + jsonEscape(loadedPath->string()) +
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
  if (m_pendingRuntime.has_value()) {
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

LX_core::CommandResult
LxeEditorSession::handleProjectCommand(const std::vector<std::string> &args) {
  if (args.empty() || args[0] == "status") {
    return makeCommandOk(projectSummaryJson(), projectSummaryJson());
  }
  if (args[0] == "templates") {
    if (args.size() > 2 || (args.size() == 2 && args[1] != "list")) {
      return makeCommandError("usage: project templates [list]");
    }
    ProjectTemplateCatalog catalog(
        resolveRuntimePath("assets/project_templates"));
    catalog.refresh();
    std::ostringstream oss;
    oss << "{\"templates\":[";
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
  std::ostringstream oss;
  oss << "{\"id\":\"" << jsonEscape(project->id) << "\",\"displayName\":\""
      << jsonEscape(project->displayName) << "\",\"path\":\""
      << jsonEscape(projectRoot->string())
      << "\",\"dirty\":" << (m_projectSession.dirty() ? "true" : "false")
      << ",\"activeScene\":\""
      << jsonEscape(project->activeScene.generic_string()) << "\"}";
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
                  BuiltinAssetCatalog catalog;
                  catalog.refresh(resolveRuntimePath("assets/models/builtin"));
                  const std::string assetId =
                      kind.substr(std::string("model:").size());
                  const auto asset = catalog.findByAssetId(assetId);
                  if (!asset.has_value()) {
                    return makeCommandError("unknown model asset: " + assetId);
                  }
                  try {
                    outNode = buildModelAssetNode(
                        asset->meshUri, asset->defaultMaterialUri,
                        asset->albedoTextureUri, nodeName);
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
                      .value = value.value});
                }
                return out;
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
          .dirty = [this]() { return m_projectSession.dirty(); },
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
          .buildInfoJson = []() { return toJson(currentLxeEditorBuildInfo()); },
          .displayListJson = m_displayCommandHooks.displayListJson,
          .displayActiveJson = m_displayCommandHooks.displayActiveJson,
          .displayConfigGetJson = m_displayCommandHooks.displayConfigGetJson,
          .displayConfigSet = m_displayCommandHooks.displayConfigSet,
          .displaySelect = m_displayCommandHooks.displaySelect,
      });

  m_ui.attach(
      m_rig, *m_commandBus, m_editorState, m_editorConfig, *m_viewportOverlay,
      *m_sceneTreePanel, *m_inspectorPanel, *m_consolePanel,
      [this]() { return m_debugEnabled; },
      [this]() { return m_recording.status(); });
  ++m_bindingsGeneration;
}

} // namespace LX_demo::lxe_editor
