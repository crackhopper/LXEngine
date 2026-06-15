#include "editor/api/lxe_editor_api_service.hpp"

#include "core/scene/components/camera_component.hpp"
#include "infra/build_info/build_info.hpp"

#include <algorithm>
#include <exception>
#include <sstream>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::optional<std::reference_wrapper<RecordingController>>
recordingFromHooks(const LxeEditorApiService::Hooks &hooks) {
  return hooks.recording ? hooks.recording() : std::nullopt;
}

[[nodiscard]] ApiCameraPose
captureCameraPose(const LX_core::SceneNodeSharedPtr &node) {
  ApiCameraPose pose;
  if (!node) {
    return pose;
  }

  pose.path = node->getPath();
  const auto camera = node->getComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    return pose;
  }

  pose.eye = camera->get().getEyePosition();
  pose.target = camera->get().getLookTarget();
  pose.up = camera->get().getUpVector();
  pose.active = camera->get().isActive();
  return pose;
}

[[nodiscard]] ApiCommandEventPayload
commandPayloadFromHistory(const LX_core::CommandBus::HistoryEntry &entry) {
  return ApiCommandEventPayload{
      .line = entry.line,
      .ok = entry.result.ok,
      .message = entry.result.message,
      .structuredJson = entry.result.structured,
      .metadata = entry.result.metadata,
      .timestampMs = entry.timestampMs,
  };
}

[[nodiscard]] std::optional<std::string>
jsonStringField(const std::string &body, const std::string &key,
                const size_t startPos = 0) {
  const std::string needle = "\"" + key + "\"";
  const size_t keyPos = body.find(needle, startPos);
  if (keyPos == std::string::npos) {
    return std::nullopt;
  }
  const size_t colonPos = body.find(':', keyPos + needle.size());
  if (colonPos == std::string::npos) {
    return std::nullopt;
  }
  const size_t firstQuote = body.find('"', colonPos + 1);
  if (firstQuote == std::string::npos) {
    return std::nullopt;
  }
  std::string value;
  bool escaping = false;
  for (size_t i = firstQuote + 1; i < body.size(); ++i) {
    const char c = body[i];
    if (escaping) {
      value.push_back(c);
      escaping = false;
      continue;
    }
    if (c == '\\') {
      escaping = true;
      continue;
    }
    if (c == '"') {
      return value;
    }
    value.push_back(c);
  }
  return std::nullopt;
}

[[nodiscard]] std::string statusToJson(const RecordingStatus &status) {
  std::string out = "{";
  out += "\"enabled\":";
  out += status.enabled ? "true" : "false";
  out += ",\"active\":";
  out += status.active ? "true" : "false";
  out += ",\"sessionId\":\"";
  out += apiJsonEscape(status.sessionId);
  out += "\",\"detailLevel\":\"";
  out += recordingDetailLevelName(status.detailLevel);
  out += "\",\"stepCount\":";
  out += std::to_string(status.stepCount);
  out += ",\"lastSavedPath\":";
  if (status.lastSavedPath.has_value()) {
    out += "\"";
    out += apiJsonEscape(status.lastSavedPath->string());
    out += "\"";
  } else {
    out += "null";
  }
  out += "}";
  return out;
}

[[nodiscard]] std::string stepPayloadJson(const std::string &line) {
  return "{\"line\":\"" + apiJsonEscape(line) + "\"}";
}

[[nodiscard]] std::string recordingErrorJson(const std::exception &error) {
  return "{\"ok\":false,\"error\":\"" + apiJsonEscape(error.what()) + "\"}";
}

[[nodiscard]] std::optional<std::string>
activeSceneFromState(const ApiStateSnapshot &state) {
  if (!state.project.has_value()) {
    return std::nullopt;
  }
  return state.project->activeScene;
}

[[nodiscard]] bool projectDirtyFromState(const ApiStateSnapshot &state) {
  return state.project.has_value() ? state.project->dirty : false;
}

[[nodiscard]] bool commandStartsWith(std::string_view line,
                                     std::string_view verb,
                                     std::string_view subcommand) {
  std::vector<std::string_view> tokens;
  bool inToken = false;
  size_t tokenBegin = 0;
  for (size_t i = 0; i <= line.size(); ++i) {
    const bool atEnd = i == line.size();
    const char c = atEnd ? '\0' : line[i];
    const bool whitespace = c == ' ' || c == '\t' || c == '\r';
    if (!atEnd && !whitespace) {
      if (!inToken) {
        tokenBegin = i;
        inToken = true;
      }
      continue;
    }
    if (inToken) {
      tokens.push_back(line.substr(tokenBegin, i - tokenBegin));
      if (tokens.size() >= 2) {
        break;
      }
      inToken = false;
    }
  }
  return tokens.size() >= 2 && tokens[0] == verb && tokens[1] == subcommand;
}

} // namespace

LxeEditorApiService::LxeEditorApiService(LX_core::CommandBus &commandBus,
                                         LX_core::EditorState &editorState,
                                         LX_core::Scene &scene, Hooks hooks)
    : m_commandBus(commandBus), m_editorState(editorState), m_scene(scene),
      m_hooks(std::move(hooks)), m_sceneSubscription(m_scene.events().subscribe(
                                     [this](const LX_core::SceneEvent &event) {
                                       observeRuntimeSceneEvent(event);
                                     })),
      m_lastObservedHistoryIndex(m_commandBus.history().size()),
      m_lastState(captureState()),
      m_lastActiveSceneEventKey(captureActiveSceneEventKey()) {}

LxeEditorApiService::LxeEditorApiService(
    LX_core::CommandBus &commandBus, LX_core::EditorState &editorState,
    LX_core::Scene &scene, Hooks hooks, const LxeEditorApiService &previous)
    : LxeEditorApiService(commandBus, editorState, scene, std::move(hooks)) {
  m_lastObservedHistoryIndex = previous.m_lastObservedHistoryIndex;
  m_nextSequence = previous.m_nextSequence;
  m_lastState = previous.m_lastState;
  m_lastActiveSceneEventKey = previous.m_lastActiveSceneEventKey;
  m_events = previous.m_events;
  m_pendingRuntimeSceneEvents = previous.m_pendingRuntimeSceneEvents;
}

ApiCommandResponse
LxeEditorApiService::executeCommand(const ApiCommandRequest &request) {
  if (m_hooks.recordCommandHistoryLine) {
    m_hooks.recordCommandHistoryLine(request.line);
  }
  flushPendingRuntimeSceneEvents();
  const LX_core::CommandResult result = m_commandBus.dispatch(request.line);
  if (result.ok) {
    if (auto recording = recordingFromHooks(m_hooks); recording.has_value()) {
      (void)recording->get().appendStep(RecordingStepInput{
          .kind = "command",
          .source = RecordingSource::Mcp,
          .payloadJson = stepPayloadJson(request.line),
      });
    }
  }
  refresh();

  const auto &history = m_commandBus.history();
  const u64 timestampMs = history.empty() ? 0 : history.back().timestampMs;
  ApiCommandResponse response{
      .ok = result.ok,
      .line = request.line,
      .message = result.message,
      .structuredJson = result.structured,
      .metadata = result.metadata,
      .timestampMs = timestampMs,
  };
  if (!result.ok) {
    response.error =
        ApiError{.code = "command_failed", .message = result.message};
  }
  return response;
}

std::string LxeEditorApiService::buildInfo() const {
  return LX_infra::currentBuildInfoJson("lxe_editor");
}

std::string LxeEditorApiService::recordingStatus() const {
  const auto recording = recordingFromHooks(m_hooks);
  if (!recording.has_value()) {
    return "{\"available\":false}";
  }
  return statusToJson(recording->get().status());
}

std::string LxeEditorApiService::recordingEnable() {
  const auto recording = recordingFromHooks(m_hooks);
  if (!recording.has_value()) {
    return "{\"ok\":false,\"error\":\"recording unavailable\"}";
  }
  recording->get().enable();
  return statusToJson(recording->get().status());
}

std::string LxeEditorApiService::recordingDisable(const bool force) {
  const auto recording = recordingFromHooks(m_hooks);
  if (!recording.has_value()) {
    return "{\"ok\":false,\"error\":\"recording unavailable\"}";
  }
  const bool disabled = recording->get().disable(force);
  if (!disabled) {
    return "{\"ok\":false,\"error\":\"recording active; pass force=true\"}";
  }
  return statusToJson(recording->get().status());
}

std::string
LxeEditorApiService::recordingStart(const RecordingDetailLevel detailLevel) {
  const auto recording = recordingFromHooks(m_hooks);
  if (!recording.has_value()) {
    return "{\"ok\":false,\"error\":\"recording unavailable\"}";
  }
  try {
    const ApiStateSnapshot state = captureState();
    const auto result = recording->get().start(RecordingStartOptions{
        .detailLevel = detailLevel,
        .scenePath =
            state.project.has_value() ? state.project->activeScene : std::string{},
        .buildInfoJson = buildInfo(),
    });
    return "{\"active\":" + std::string(result.active ? "true" : "false") +
           ",\"sessionId\":\"" + apiJsonEscape(result.sessionId) + "\"}";
  } catch (const std::exception &error) {
    return recordingErrorJson(error);
  }
}

std::string LxeEditorApiService::recordingStop(const bool save) {
  const auto recording = recordingFromHooks(m_hooks);
  if (!recording.has_value()) {
    return "{\"ok\":false,\"error\":\"recording unavailable\"}";
  }
  try {
    const auto result =
        recording->get().stop(RecordingStopOptions{.save = save});
    return "{\"saved\":" + std::string(result.saved ? "true" : "false") +
           ",\"path\":\"" + apiJsonEscape(result.path.string()) +
           "\",\"stepCount\":" + std::to_string(result.stepCount) +
           ",\"sessionId\":\"" + apiJsonEscape(result.sessionId) + "\"}";
  } catch (const std::exception &error) {
    return recordingErrorJson(error);
  }
}

std::string LxeEditorApiService::recordingList() const {
  const auto recording = recordingFromHooks(m_hooks);
  if (!recording.has_value()) {
    return "{\"recordings\":[]}";
  }
  try {
    const auto entries = recording->get().list();
    std::string out = "{\"recordings\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
      if (i != 0) {
        out += ",";
      }
      out += "{\"id\":\"" + apiJsonEscape(entries[i].id) + "\",\"path\":\"" +
             apiJsonEscape(entries[i].path.string()) + "\"}";
    }
    out += "]}";
    return out;
  } catch (const std::exception &error) {
    return recordingErrorJson(error);
  }
}

std::string
LxeEditorApiService::recordingRead(const std::string &idOrPath) const {
  const auto recording = recordingFromHooks(m_hooks);
  if (!recording.has_value()) {
    return "{\"ok\":false,\"error\":\"recording unavailable\"}";
  }
  try {
    return recording->get().read(idOrPath);
  } catch (const std::exception &error) {
    return recordingErrorJson(error);
  }
}

std::string LxeEditorApiService::recordingReplay(const std::string &idOrPath) {
  const auto recording = recordingFromHooks(m_hooks);
  if (!recording.has_value()) {
    return "{\"ok\":false,\"error\":\"recording unavailable\"}";
  }
  std::string text;
  try {
    text = recording->get().read(idOrPath);
  } catch (const std::exception &error) {
    return recordingErrorJson(error);
  }
  int completed = 0;
  size_t search = 0;
  while (true) {
    const size_t kindPos = text.find("\"kind\":\"command\"", search);
    if (kindPos == std::string::npos) {
      break;
    }
    const auto line = jsonStringField(text, "line", kindPos);
    if (!line.has_value()) {
      return "{\"ok\":false,\"completedSteps\":" + std::to_string(completed) +
             ",\"error\":\"recording command step missing line\",\"summary\":" +
             toJson(captureSceneSummary()) + "}";
    }
    const LX_core::CommandResult result = m_commandBus.dispatch(*line);
    refresh();
    if (!result.ok) {
      return "{\"ok\":false,\"completedSteps\":" + std::to_string(completed) +
             ",\"failedKind\":\"command\",\"error\":\"" +
             apiJsonEscape(result.message) +
             "\",\"summary\":" + toJson(captureSceneSummary()) + "}";
    }
    ++completed;
    search = kindPos + 1;
  }
  return "{\"ok\":true,\"completedSteps\":" + std::to_string(completed) +
         ",\"summary\":" + toJson(captureSceneSummary()) + "}";
}

std::string
LxeEditorApiService::recordingProbe(const std::string &target) const {
  const ApiStateSnapshot state = captureState();
  if (target == "summary") {
    return toJson(state.scene);
  }
  if (target == "project") {
    return state.project.has_value() ? toJson(*state.project) : "null";
  }
  if (target == "selection") {
    return toJson(state.selection);
  }
  if (target == "cameras") {
    return toJson(state.cameras);
  }
  if (target == "toolbar") {
    return toJson(state.toolbar);
  }
  if (target == "scene") {
    return toJson(state.scene);
  }
  return toJson(state);
}

std::string LxeEditorApiService::displayList() const {
  if (!m_hooks.displayListJson) {
    return "{\"ok\":false,\"error\":\"display config unavailable\"}";
  }
  return m_hooks.displayListJson();
}

std::string LxeEditorApiService::displayActive() const {
  if (!m_hooks.displayActiveJson) {
    return "{\"ok\":false,\"error\":\"display config unavailable\"}";
  }
  return m_hooks.displayActiveJson();
}

std::string
LxeEditorApiService::displayConfigGet(const std::string &key) const {
  if (!m_hooks.displayConfigGetJson) {
    return "{\"ok\":false,\"error\":\"display config unavailable\"}";
  }
  return m_hooks.displayConfigGetJson(key);
}

std::string LxeEditorApiService::displayConfigSet(const std::string &key,
                                                  const std::string &patch) {
  if (!m_hooks.displayConfigSet) {
    return "{\"ok\":false,\"error\":\"display config unavailable\"}";
  }
  return m_hooks.displayConfigSet(key, patch);
}

std::string LxeEditorApiService::displaySelect(const std::string &key) {
  if (!m_hooks.displaySelect) {
    return "{\"ok\":false,\"error\":\"display config unavailable\"}";
  }
  return m_hooks.displaySelect(key);
}

ApiStateSnapshot LxeEditorApiService::captureState() const {
  return ApiStateSnapshot{
      .scene = captureSceneSummary(),
      .project = captureProjectSummary(),
      .selection = captureSelection(),
      .cameras = captureCameras(),
      .toolbar = captureToolbar(),
  };
}

void LxeEditorApiService::refresh() {
  observeCommandHistory();
  flushPendingRuntimeSceneEvents();
  observeStateChanges();
}

ApiEventCursor LxeEditorApiService::currentCursor() const {
  return ApiEventCursor{m_nextSequence};
}

ApiEventBatch
LxeEditorApiService::collectEventsSince(const ApiEventCursor cursor) const {
  ApiEventBatch batch;
  batch.nextCursor = currentCursor();
  for (const auto &event : m_events) {
    if (event.sequence >= cursor.nextSequence) {
      batch.events.push_back(event);
    }
  }
  return batch;
}

ApiSceneSummary LxeEditorApiService::captureSceneSummary() const {
  if (m_hooks.sceneSummary) {
    return m_hooks.sceneSummary();
  }

  return ApiSceneSummary{
      .sceneName = m_scene.getSceneName(),
      .dirty = false,
  };
}

std::optional<ApiProjectSummary>
LxeEditorApiService::captureProjectSummary() const {
  if (m_hooks.projectSummary) {
    return m_hooks.projectSummary();
  }

  return std::nullopt;
}

std::optional<std::string>
LxeEditorApiService::captureActiveSceneEventKey() const {
  if (m_hooks.activeSceneEventKey) {
    return m_hooks.activeSceneEventKey();
  }
  return std::nullopt;
}

ApiSelectionSnapshot LxeEditorApiService::captureSelection() const {
  ApiSelectionSnapshot snapshot;
  const auto selected = m_editorState.getSelected();
  snapshot.selectedPaths.reserve(selected.size());
  for (const auto &node : selected) {
    if (!node) {
      continue;
    }
    snapshot.selectedPaths.push_back(node->getPath());
  }

  const auto primary = m_editorState.getPrimarySelected();
  if (primary.has_value()) {
    snapshot.primaryPath = primary->get().getPath();
    snapshot.primaryWorldBounds =
        apiAabbFromBounds(primary->get().getWorldBounds());
  }

  if (m_hooks.lastHitPoint) {
    snapshot.lastHitPoint = m_hooks.lastHitPoint();
  }
  return snapshot;
}

ApiCameraSnapshot LxeEditorApiService::captureCameras() const {
  if (m_hooks.cameraSnapshot) {
    return m_hooks.cameraSnapshot();
  }

  ApiCameraSnapshot snapshot;
  snapshot.editor = captureCameraPose(m_editorState.getEditorCamera());
  snapshot.game = captureCameraPose(m_editorState.getPreviewCamera());
  if (const auto active = m_editorState.resolveActiveCamera(m_scene); active) {
    snapshot.activeCameraPath = active->getPath();
    if (snapshot.editor.path == snapshot.activeCameraPath) {
      snapshot.editor.active = true;
    }
    if (snapshot.game.path == snapshot.activeCameraPath) {
      snapshot.game.active = true;
    }
  }
  return snapshot;
}

ApiToolbarSnapshot LxeEditorApiService::captureToolbar() const {
  if (m_hooks.toolbarSnapshot) {
    return m_hooks.toolbarSnapshot();
  }

  return ApiToolbarSnapshot{
      .mode = ApiEditorMode::Unknown,
      .camera = ApiCameraControlMode::Unknown,
      .previewEnabled = m_editorState.isPreviewEnabled(),
      .debugEnabled = false,
  };
}

void LxeEditorApiService::observeRuntimeSceneEvent(
    const LX_core::SceneEvent &event) {
  if (event.domain != LX_core::SceneEventDomain::Runtime ||
      event.type != LX_core::SceneEventType::SceneNodeChanged) {
    return;
  }

  m_pendingRuntimeSceneEvents.push_back(event);
}

void LxeEditorApiService::flushPendingRuntimeSceneEvents() {
  for (const auto &event : m_pendingRuntimeSceneEvents) {
    ApiSceneNodeEventPayload payload{
        .path = event.path,
        .stableNodeName = event.stableNodeName,
    };
    payload.aspects.reserve(event.aspects.size());
    for (const auto aspect : event.aspects) {
      payload.aspects.push_back(sceneNodeAspectName(aspect));
    }

    ApiEvent apiEvent{
        .sequence = m_nextSequence++,
        .type = ApiEventType::SceneNodeChanged,
        .sceneNode = payload,
        .payloadJson = toJson(payload),
    };
    appendEvent(std::move(apiEvent));
  }
  m_pendingRuntimeSceneEvents.clear();
}

void LxeEditorApiService::observeCommandHistory() {
  const auto &history = m_commandBus.history();
  while (m_lastObservedHistoryIndex < history.size()) {
    const auto &entry = history[m_lastObservedHistoryIndex++];

    ApiEvent commandEvent{
        .sequence = m_nextSequence++,
        .type = ApiEventType::CommandExecuted,
        .command = commandPayloadFromHistory(entry),
    };
    commandEvent.payloadJson = toJson(*commandEvent.command);
    appendEvent(std::move(commandEvent));

    if (entry.result.ok && isSceneSaveCommand(entry.line)) {
      appendEvent(ApiEvent{
          .sequence = m_nextSequence++,
          .type = ApiEventType::SceneSaved,
          .state = captureState(),
          .payloadJson = toJson(captureSceneSummary()),
      });
    }
    if (entry.result.ok && isProjectInitCommand(entry.line)) {
      const ApiStateSnapshot state = captureState();
      appendEvent(ApiEvent{
          .sequence = m_nextSequence++,
          .type = ApiEventType::ProjectInitialized,
          .state = state,
          .payloadJson =
              state.project.has_value() ? toJson(*state.project) : "null",
      });
    }
    if (entry.result.ok && isProjectOpenCommand(entry.line)) {
      const ApiStateSnapshot state = captureState();
      appendEvent(ApiEvent{
          .sequence = m_nextSequence++,
          .type = ApiEventType::ProjectOpened,
          .state = state,
          .payloadJson =
              state.project.has_value() ? toJson(*state.project) : "null",
      });
    }
    if (entry.result.ok && isProjectSaveCommand(entry.line)) {
      const ApiStateSnapshot state = captureState();
      appendEvent(ApiEvent{
          .sequence = m_nextSequence++,
          .type = ApiEventType::ProjectSaved,
          .state = state,
          .payloadJson =
              state.project.has_value() ? toJson(*state.project) : "null",
      });
    }
    if (entry.result.ok && isProjectCloseCommand(entry.line)) {
      const ApiStateSnapshot state = captureState();
      appendEvent(ApiEvent{
          .sequence = m_nextSequence++,
          .type = ApiEventType::ProjectClosed,
          .state = state,
          .payloadJson =
              state.project.has_value() ? toJson(*state.project) : "null",
      });
    }
  }
}

void LxeEditorApiService::observeStateChanges() {
  const ApiStateSnapshot current = captureState();

  if (current.selection != m_lastState.selection) {
    appendEvent(ApiEvent{
        .sequence = m_nextSequence++,
        .type = ApiEventType::SelectionChanged,
        .state = current,
        .payloadJson = toJson(current.selection),
    });
  }
  if (current.toolbar.mode != m_lastState.toolbar.mode ||
      current.toolbar.camera != m_lastState.toolbar.camera) {
    appendEvent(ApiEvent{
        .sequence = m_nextSequence++,
        .type = ApiEventType::ModeChanged,
        .state = current,
        .payloadJson = toJson(current.toolbar),
    });
  }
  if (current.toolbar.previewEnabled != m_lastState.toolbar.previewEnabled) {
    appendEvent(ApiEvent{
        .sequence = m_nextSequence++,
        .type = ApiEventType::PreviewChanged,
        .state = current,
        .payloadJson = toJson(current.toolbar),
    });
  }
  if (current.scene.dirty != m_lastState.scene.dirty ||
      projectDirtyFromState(current) != projectDirtyFromState(m_lastState)) {
    appendEvent(ApiEvent{
        .sequence = m_nextSequence++,
        .type = ApiEventType::DirtyChanged,
        .state = current,
        .payloadJson = toJson(current),
    });
  }
  const auto activeSceneEventKey = captureActiveSceneEventKey();
  if (activeSceneEventKey.has_value() &&
      activeSceneEventKey != m_lastActiveSceneEventKey) {
    appendEvent(ApiEvent{
        .sequence = m_nextSequence++,
        .type = ApiEventType::ActiveSceneChanged,
        .state = current,
        .payloadJson =
            current.project.has_value() ? toJson(*current.project) : "null",
    });
  }

  m_lastState = current;
  m_lastActiveSceneEventKey = activeSceneEventKey;
}

void LxeEditorApiService::appendEvent(ApiEvent event) {
  m_events.push_back(std::move(event));
  if (m_events.size() > kMaxBufferedEvents) {
    m_events.erase(
        m_events.begin(),
        m_events.begin() +
            static_cast<std::ptrdiff_t>(m_events.size() - kMaxBufferedEvents));
  }
}

std::string LxeEditorApiService::sceneNodeAspectName(
    const LX_core::SceneNodeAspect aspect) {
  switch (aspect) {
  case LX_core::SceneNodeAspect::Transform:
    return "transform";
  case LX_core::SceneNodeAspect::Identity:
    return "identity";
  case LX_core::SceneNodeAspect::Hierarchy:
    return "hierarchy";
  case LX_core::SceneNodeAspect::Visibility:
    return "visibility";
  case LX_core::SceneNodeAspect::RenderableStructure:
    return "renderable_structure";
  case LX_core::SceneNodeAspect::CameraProperties:
    return "cameraProperties";
  case LX_core::SceneNodeAspect::LightProperties:
    return "lightProperties";
  }
  return "unknown";
}

bool LxeEditorApiService::isSceneSaveCommand(const std::string_view line) {
  return commandStartsWith(line, "scene", "save");
}

bool LxeEditorApiService::isProjectInitCommand(const std::string_view line) {
  return commandStartsWith(line, "project", "init");
}

bool LxeEditorApiService::isProjectOpenCommand(const std::string_view line) {
  return commandStartsWith(line, "project", "open");
}

bool LxeEditorApiService::isProjectSaveCommand(const std::string_view line) {
  return commandStartsWith(line, "project", "save");
}

bool LxeEditorApiService::isProjectCloseCommand(const std::string_view line) {
  return commandStartsWith(line, "project", "close");
}

} // namespace LX_demo::lxe_editor
