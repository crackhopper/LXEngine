#include "demos/lxe_editor/editor_automation_service.hpp"

#include "core/scene/components/camera_component.hpp"

#include <algorithm>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] AutomationCameraPose captureCameraPose(
    const LX_core::SceneNodeSharedPtr& node) {
  AutomationCameraPose pose;
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

[[nodiscard]] AutomationCommandEventPayload commandPayloadFromHistory(
    const LX_core::CommandBus::HistoryEntry& entry) {
  return AutomationCommandEventPayload{
      .line = entry.line,
      .ok = entry.result.ok,
      .message = entry.result.message,
      .structuredJson = entry.result.structured,
      .metadata = entry.result.metadata,
      .timestampMs = entry.timestampMs,
  };
}

} // namespace

LxeEditorApiService::LxeEditorApiService(
    LX_core::CommandBus& commandBus, LX_core::EditorState& editorState,
    LX_core::Scene& scene, Hooks hooks)
    : m_commandBus(commandBus),
      m_editorState(editorState),
      m_scene(scene),
      m_hooks(std::move(hooks)),
      m_lastObservedHistoryIndex(m_commandBus.history().size()),
      m_lastState(captureState()) {}

AutomationCommandResponse LxeEditorApiService::executeCommand(
    const AutomationCommandRequest& request) {
  if (m_hooks.recordCommandHistoryLine) {
    m_hooks.recordCommandHistoryLine(request.line);
  }
  const LX_core::CommandResult result = m_commandBus.dispatch(request.line);
  refresh();

  const auto& history = m_commandBus.history();
  const u64 timestampMs =
      history.empty() ? 0 : history.back().timestampMs;
  AutomationCommandResponse response{
      .ok = result.ok,
      .line = request.line,
      .message = result.message,
      .structuredJson = result.structured,
      .metadata = result.metadata,
      .timestampMs = timestampMs,
  };
  if (!result.ok) {
    response.error = AutomationError{.code = "command_failed",
                                     .message = result.message};
  }
  return response;
}

AutomationStateSnapshot LxeEditorApiService::captureState() const {
  return AutomationStateSnapshot{
      .scene = captureSceneSummary(),
      .selection = captureSelection(),
      .cameras = captureCameras(),
      .toolbar = captureToolbar(),
  };
}

void LxeEditorApiService::refresh() {
  observeCommandHistory();
  observeStateChanges();
}

AutomationEventCursor LxeEditorApiService::currentCursor() const {
  return AutomationEventCursor{m_nextSequence};
}

AutomationEventBatch LxeEditorApiService::collectEventsSince(
    const AutomationEventCursor cursor) const {
  AutomationEventBatch batch;
  batch.nextCursor = currentCursor();
  for (const auto& event : m_events) {
    if (event.sequence >= cursor.nextSequence) {
      batch.events.push_back(event);
    }
  }
  return batch;
}

AutomationSceneSummary LxeEditorApiService::captureSceneSummary() const {
  if (m_hooks.sceneSummary) {
    return m_hooks.sceneSummary();
  }

  return AutomationSceneSummary{
      .sceneName = m_scene.getSceneName(),
      .currentDocumentPath = {},
      .sourceKind = AutomationSceneSourceKind::Unknown,
      .permission = AutomationPermissionLevel::Unknown,
      .dirty = false,
  };
}

AutomationSelectionSnapshot LxeEditorApiService::captureSelection() const {
  AutomationSelectionSnapshot snapshot;
  const auto selected = m_editorState.getSelected();
  snapshot.selectedPaths.reserve(selected.size());
  for (const auto& node : selected) {
    if (!node) {
      continue;
    }
    snapshot.selectedPaths.push_back(node->getPath());
  }

  const auto primary = m_editorState.getPrimarySelected();
  if (primary.has_value()) {
    snapshot.primaryPath = primary->get().getPath();
    snapshot.primaryWorldBounds =
        automationAabbFromBounds(primary->get().getWorldBounds());
  }

  if (m_hooks.lastHitPoint) {
    snapshot.lastHitPoint = m_hooks.lastHitPoint();
  }
  return snapshot;
}

AutomationCameraSnapshot LxeEditorApiService::captureCameras() const {
  if (m_hooks.cameraSnapshot) {
    return m_hooks.cameraSnapshot();
  }

  AutomationCameraSnapshot snapshot;
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

AutomationToolbarSnapshot LxeEditorApiService::captureToolbar() const {
  if (m_hooks.toolbarSnapshot) {
    return m_hooks.toolbarSnapshot();
  }

  return AutomationToolbarSnapshot{
      .editMode = AutomationEditMode::Unknown,
      .previewEnabled = m_editorState.isPreviewEnabled(),
  };
}

void LxeEditorApiService::observeCommandHistory() {
  const auto& history = m_commandBus.history();
  while (m_lastObservedHistoryIndex < history.size()) {
    const auto& entry = history[m_lastObservedHistoryIndex++];

    AutomationEvent commandEvent{
        .sequence = m_nextSequence++,
        .type = AutomationEventType::CommandExecuted,
        .command = commandPayloadFromHistory(entry),
    };
    commandEvent.payloadJson = toJson(*commandEvent.command);
    appendEvent(std::move(commandEvent));

    if (entry.result.ok && isSceneLoadCommand(entry.line)) {
      appendEvent(AutomationEvent{
          .sequence = m_nextSequence++,
          .type = AutomationEventType::SceneLoaded,
          .state = captureState(),
          .payloadJson = toJson(captureSceneSummary()),
      });
    }
    if (entry.result.ok && isSceneSaveCommand(entry.line)) {
      appendEvent(AutomationEvent{
          .sequence = m_nextSequence++,
          .type = AutomationEventType::SceneSaved,
          .state = captureState(),
          .payloadJson = toJson(captureSceneSummary()),
      });
    }
  }
}

void LxeEditorApiService::observeStateChanges() {
  const AutomationStateSnapshot current = captureState();

  if (current.selection != m_lastState.selection) {
    appendEvent(AutomationEvent{
        .sequence = m_nextSequence++,
        .type = AutomationEventType::SelectionChanged,
        .state = current,
        .payloadJson = toJson(current.selection),
    });
  }
  if (current.toolbar.editMode != m_lastState.toolbar.editMode) {
    appendEvent(AutomationEvent{
        .sequence = m_nextSequence++,
        .type = AutomationEventType::ModeChanged,
        .state = current,
        .payloadJson = toJson(current.toolbar),
    });
  }
  if (current.toolbar.previewEnabled != m_lastState.toolbar.previewEnabled) {
    appendEvent(AutomationEvent{
        .sequence = m_nextSequence++,
        .type = AutomationEventType::PreviewChanged,
        .state = current,
        .payloadJson = toJson(current.toolbar),
    });
  }
  if (current.scene.dirty != m_lastState.scene.dirty) {
    appendEvent(AutomationEvent{
        .sequence = m_nextSequence++,
        .type = AutomationEventType::DirtyChanged,
        .state = current,
        .payloadJson = toJson(current.scene),
    });
  }

  m_lastState = current;
}

void LxeEditorApiService::appendEvent(AutomationEvent event) {
  m_events.push_back(std::move(event));
  if (m_events.size() > kMaxBufferedEvents) {
    m_events.erase(m_events.begin(),
                   m_events.begin() + static_cast<std::ptrdiff_t>(
                                         m_events.size() - kMaxBufferedEvents));
  }
}

bool LxeEditorApiService::isSceneLoadCommand(const std::string_view line) {
  return line == "scene load" || line.starts_with("scene load ");
}

bool LxeEditorApiService::isSceneSaveCommand(const std::string_view line) {
  return line == "scene save" || line.starts_with("scene save ");
}

} // namespace LX_demo::lxe_editor
