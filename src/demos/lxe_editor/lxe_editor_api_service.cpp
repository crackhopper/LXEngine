#include "demos/lxe_editor/lxe_editor_api_service.hpp"

#include "core/scene/components/camera_component.hpp"

#include <algorithm>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] ApiCameraPose captureCameraPose(
    const LX_core::SceneNodeSharedPtr& node) {
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

[[nodiscard]] ApiCommandEventPayload commandPayloadFromHistory(
    const LX_core::CommandBus::HistoryEntry& entry) {
  return ApiCommandEventPayload{
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
      m_sceneSubscription(m_scene.events().subscribe(
          [this](const LX_core::SceneEvent& event) {
            observeRuntimeSceneEvent(event);
          })),
      m_lastObservedHistoryIndex(m_commandBus.history().size()),
      m_lastState(captureState()) {}

ApiCommandResponse LxeEditorApiService::executeCommand(
    const ApiCommandRequest& request) {
  if (m_hooks.recordCommandHistoryLine) {
    m_hooks.recordCommandHistoryLine(request.line);
  }
  flushPendingRuntimeSceneEvents();
  const LX_core::CommandResult result = m_commandBus.dispatch(request.line);
  refresh();

  const auto& history = m_commandBus.history();
  const u64 timestampMs =
      history.empty() ? 0 : history.back().timestampMs;
  ApiCommandResponse response{
      .ok = result.ok,
      .line = request.line,
      .message = result.message,
      .structuredJson = result.structured,
      .metadata = result.metadata,
      .timestampMs = timestampMs,
  };
  if (!result.ok) {
    response.error = ApiError{.code = "command_failed",
                                     .message = result.message};
  }
  return response;
}

ApiStateSnapshot LxeEditorApiService::captureState() const {
  return ApiStateSnapshot{
      .scene = captureSceneSummary(),
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

ApiEventBatch LxeEditorApiService::collectEventsSince(
    const ApiEventCursor cursor) const {
  ApiEventBatch batch;
  batch.nextCursor = currentCursor();
  for (const auto& event : m_events) {
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
      .currentDocumentPath = {},
      .sourceKind = ApiSceneSourceKind::Unknown,
      .permission = ApiPermissionLevel::Unknown,
      .dirty = false,
  };
}

ApiSelectionSnapshot LxeEditorApiService::captureSelection() const {
  ApiSelectionSnapshot snapshot;
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
      .editMode = ApiEditMode::Unknown,
      .previewEnabled = m_editorState.isPreviewEnabled(),
      .debugEnabled = false,
  };
}

void LxeEditorApiService::observeRuntimeSceneEvent(
    const LX_core::SceneEvent& event) {
  if (event.domain != LX_core::SceneEventDomain::Runtime ||
      event.type != LX_core::SceneEventType::SceneNodeChanged) {
    return;
  }

  m_pendingRuntimeSceneEvents.push_back(event);
}

void LxeEditorApiService::flushPendingRuntimeSceneEvents() {
  for (const auto& event : m_pendingRuntimeSceneEvents) {
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
  const auto& history = m_commandBus.history();
  while (m_lastObservedHistoryIndex < history.size()) {
    const auto& entry = history[m_lastObservedHistoryIndex++];

    ApiEvent commandEvent{
        .sequence = m_nextSequence++,
        .type = ApiEventType::CommandExecuted,
        .command = commandPayloadFromHistory(entry),
    };
    commandEvent.payloadJson = toJson(*commandEvent.command);
    appendEvent(std::move(commandEvent));

    if (entry.result.ok && isSceneLoadCommand(entry.line)) {
      appendEvent(ApiEvent{
          .sequence = m_nextSequence++,
          .type = ApiEventType::SceneLoaded,
          .state = captureState(),
          .payloadJson = toJson(captureSceneSummary()),
      });
    }
    if (entry.result.ok && isSceneSaveCommand(entry.line)) {
      appendEvent(ApiEvent{
          .sequence = m_nextSequence++,
          .type = ApiEventType::SceneSaved,
          .state = captureState(),
          .payloadJson = toJson(captureSceneSummary()),
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
  if (current.toolbar.editMode != m_lastState.toolbar.editMode) {
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
  if (current.scene.dirty != m_lastState.scene.dirty) {
    appendEvent(ApiEvent{
        .sequence = m_nextSequence++,
        .type = ApiEventType::DirtyChanged,
        .state = current,
        .payloadJson = toJson(current.scene),
    });
  }

  m_lastState = current;
}

void LxeEditorApiService::appendEvent(ApiEvent event) {
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
  return line == "scene save" || line.starts_with("scene save ");
}

} // namespace LX_demo::lxe_editor
