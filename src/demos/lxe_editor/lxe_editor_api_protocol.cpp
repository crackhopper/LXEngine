#include "demos/lxe_editor/lxe_editor_api_protocol.hpp"

#include <algorithm>

namespace LX_demo::lxe_editor {
namespace {

void appendJsonStringField(std::string& out, std::string_view key,
                           std::string_view value, const bool first = false) {
  if (!first) {
    out += ',';
  }
  out += '"';
  out += key;
  out += "\":\"";
  out += apiJsonEscape(value);
  out += '"';
}

void appendJsonBoolField(std::string& out, std::string_view key, const bool value,
                         const bool first = false) {
  if (!first) {
    out += ',';
  }
  out += '"';
  out += key;
  out += "\":";
  out += value ? "true" : "false";
}

void appendJsonUIntField(std::string& out, std::string_view key,
                         const u64 value, const bool first = false) {
  if (!first) {
    out += ',';
  }
  out += '"';
  out += key;
  out += "\":";
  out += std::to_string(value);
}

void appendJsonFloatField(std::string& out, std::string_view key, const float value,
                          const bool first = false) {
  if (!first) {
    out += ',';
  }
  out += '"';
  out += key;
  out += "\":";
  out += std::to_string(value);
}

std::string toJson(const LX_core::Vec3f& value) {
  std::string out = "{";
  appendJsonFloatField(out, "x", value.x, true);
  appendJsonFloatField(out, "y", value.y);
  appendJsonFloatField(out, "z", value.z);
  out += '}';
  return out;
}

std::string metadataToJson(
    const std::unordered_map<std::string, std::string>& metadata) {
  std::vector<std::string> keys;
  keys.reserve(metadata.size());
  for (const auto& entry : metadata) {
    keys.push_back(entry.first);
  }
  std::sort(keys.begin(), keys.end());

  std::string out = "{";
  for (size_t i = 0; i < keys.size(); ++i) {
    if (i != 0) {
      out += ',';
    }
    out += '"';
    out += apiJsonEscape(keys[i]);
    out += "\":\"";
    out += apiJsonEscape(metadata.at(keys[i]));
    out += '"';
  }
  out += '}';
  return out;
}

} // namespace

const char* apiSceneSourceKindName(const ApiSceneSourceKind kind) {
  switch (kind) {
  case ApiSceneSourceKind::Asset:
    return "asset";
  case ApiSceneSourceKind::Local:
    return "local";
  case ApiSceneSourceKind::External:
    return "external";
  case ApiSceneSourceKind::Unknown:
    break;
  }
  return "unknown";
}

const char* apiPermissionLevelName(
    const ApiPermissionLevel level) {
  switch (level) {
  case ApiPermissionLevel::User:
    return "user";
  case ApiPermissionLevel::Admin:
    return "admin";
  case ApiPermissionLevel::Unknown:
    break;
  }
  return "unknown";
}

const char* apiEditorModeName(const ApiEditorMode mode) {
  switch (mode) {
  case ApiEditorMode::Selection:
    return "selection";
  case ApiEditorMode::Unknown:
    break;
  }
  return "unknown";
}

const char* apiCameraControlModeName(const ApiCameraControlMode mode) {
  switch (mode) {
  case ApiCameraControlMode::Orbit:
    return "orbit";
  case ApiCameraControlMode::FreeFly:
    return "freefly";
  case ApiCameraControlMode::Unknown:
    break;
  }
  return "unknown";
}

const char* apiEventTypeName(const ApiEventType type) {
  switch (type) {
  case ApiEventType::CommandExecuted:
    return "command.executed";
  case ApiEventType::SceneLoaded:
    return "scene.loaded";
  case ApiEventType::SceneSaved:
    return "scene.saved";
  case ApiEventType::SelectionChanged:
    return "selection.changed";
  case ApiEventType::ModeChanged:
    return "mode.changed";
  case ApiEventType::PreviewChanged:
    return "preview.changed";
  case ApiEventType::DirtyChanged:
    return "dirty.changed";
  case ApiEventType::SceneNodeChanged:
    return "scene_node.changed";
  }
  return "unknown";
}

std::string apiJsonEscape(const std::string_view text) {
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

std::string toJson(const ApiError& error) {
  std::string out = "{";
  appendJsonStringField(out, "code", error.code, true);
  appendJsonStringField(out, "message", error.message);
  out += '}';
  return out;
}

std::string toJson(const ApiCommandRequest& request) {
  std::string out = "{";
  appendJsonStringField(out, "line", request.line, true);
  out += '}';
  return out;
}

std::string toJson(const ApiCommandResponse& response) {
  std::string out = "{";
  appendJsonBoolField(out, "ok", response.ok, true);
  appendJsonStringField(out, "line", response.line);
  appendJsonStringField(out, "message", response.message);
  appendJsonStringField(out, "structuredJson", response.structuredJson);
  out += ",\"metadata\":";
  out += metadataToJson(response.metadata);
  appendJsonUIntField(out, "timestampMs", response.timestampMs);
  out += ",\"error\":";
  out += response.error.has_value() ? toJson(*response.error) : "null";
  out += '}';
  return out;
}

std::string toJson(const ApiEventCursor& cursor) {
  std::string out = "{";
  appendJsonUIntField(out, "nextSequence", cursor.nextSequence, true);
  out += '}';
  return out;
}

std::string toJson(const ApiAabb& bounds) {
  std::string out = "{";
  out += "\"min\":";
  out += toJson(bounds.min);
  out += ",\"max\":";
  out += toJson(bounds.max);
  out += '}';
  return out;
}

std::string toJson(const ApiSceneSummary& summary) {
  std::string out = "{";
  appendJsonStringField(out, "sceneName", summary.sceneName, true);
  appendJsonStringField(out, "currentDocumentPath",
                        summary.currentDocumentPath);
  appendJsonStringField(out, "sourceKind",
                        apiSceneSourceKindName(summary.sourceKind));
  appendJsonStringField(out, "permission",
                        apiPermissionLevelName(summary.permission));
  appendJsonBoolField(out, "dirty", summary.dirty);
  out += '}';
  return out;
}

std::string toJson(const ApiSelectionSnapshot& selection) {
  std::string out = "{";
  out += "\"selectedPaths\":[";
  for (size_t i = 0; i < selection.selectedPaths.size(); ++i) {
    if (i != 0) {
      out += ',';
    }
    out += '"';
    out += apiJsonEscape(selection.selectedPaths[i]);
    out += '"';
  }
  out += "]";
  appendJsonStringField(out, "primaryPath", selection.primaryPath);
  out += ",\"primaryWorldBounds\":";
  out += selection.primaryWorldBounds.has_value()
             ? toJson(*selection.primaryWorldBounds)
             : "null";
  out += ",\"lastHitPoint\":";
  out += selection.lastHitPoint.has_value() ? toJson(*selection.lastHitPoint)
                                            : "null";
  out += '}';
  return out;
}

std::string toJson(const ApiCameraPose& pose) {
  std::string out = "{";
  appendJsonStringField(out, "path", pose.path, true);
  out += ",\"eye\":";
  out += toJson(pose.eye);
  out += ",\"target\":";
  out += toJson(pose.target);
  out += ",\"up\":";
  out += toJson(pose.up);
  appendJsonBoolField(out, "active", pose.active);
  out += '}';
  return out;
}

std::string toJson(const ApiCameraSnapshot& cameras) {
  std::string out = "{";
  appendJsonStringField(out, "activeCameraPath", cameras.activeCameraPath, true);
  out += ",\"editor\":";
  out += toJson(cameras.editor);
  out += ",\"game\":";
  out += toJson(cameras.game);
  out += '}';
  return out;
}

std::string toJson(const ApiToolbarSnapshot& toolbar) {
  std::string out = "{";
  appendJsonStringField(out, "mode", apiEditorModeName(toolbar.mode), true);
  appendJsonStringField(out, "camera",
                        apiCameraControlModeName(toolbar.camera));
  appendJsonBoolField(out, "previewEnabled", toolbar.previewEnabled);
  appendJsonBoolField(out, "debugEnabled", toolbar.debugEnabled);
  out += '}';
  return out;
}

std::string toJson(const ApiStateSnapshot& state) {
  std::string out = "{";
  out += "\"scene\":";
  out += toJson(state.scene);
  out += ",\"selection\":";
  out += toJson(state.selection);
  out += ",\"cameras\":";
  out += toJson(state.cameras);
  out += ",\"toolbar\":";
  out += toJson(state.toolbar);
  out += '}';
  return out;
}

std::string toJson(const ApiCommandEventPayload& payload) {
  std::string out = "{";
  appendJsonStringField(out, "line", payload.line, true);
  appendJsonBoolField(out, "ok", payload.ok);
  appendJsonStringField(out, "message", payload.message);
  appendJsonStringField(out, "structuredJson", payload.structuredJson);
  out += ",\"metadata\":";
  out += metadataToJson(payload.metadata);
  appendJsonUIntField(out, "timestampMs", payload.timestampMs);
  out += '}';
  return out;
}

std::string toJson(const ApiSceneNodeEventPayload& payload) {
  std::string out = "{";
  appendJsonStringField(out, "path", payload.path, true);
  appendJsonStringField(out, "stableNodeName", payload.stableNodeName);
  out += ",\"aspects\":[";
  for (usize i = 0; i < payload.aspects.size(); ++i) {
    if (i != 0) {
      out += ',';
    }
    out += '"';
    out += apiJsonEscape(payload.aspects[i]);
    out += '"';
  }
  out += "]}";
  return out;
}

std::string toJson(const ApiEvent& event) {
  std::string payload = event.payloadJson;
  if (payload.empty()) {
    if (event.command.has_value()) {
      payload = toJson(*event.command);
    } else if (event.sceneNode.has_value()) {
      payload = toJson(*event.sceneNode);
    } else if (event.state.has_value()) {
      payload = toJson(*event.state);
    } else {
      payload = "{}";
    }
  }

  std::string out = "{";
  appendJsonStringField(out, "type", apiEventTypeName(event.type), true);
  appendJsonUIntField(out, "seq", event.sequence);
  out += ",\"payload\":";
  out += payload;
  out += '}';
  return out;
}

std::string toJson(const ApiEventBatch& batch) {
  std::string out = "{";
  out += "\"nextCursor\":";
  out += toJson(batch.nextCursor);
  out += ",\"events\":[";
  for (size_t i = 0; i < batch.events.size(); ++i) {
    if (i != 0) {
      out += ',';
    }
    out += toJson(batch.events[i]);
  }
  out += "]}";
  return out;
}

std::optional<ApiAabb> apiAabbFromBounds(
    const LX_core::BoundingBox& bounds) {
  if (!bounds.isValid()) {
    return std::nullopt;
  }
  return ApiAabb{bounds.min, bounds.max};
}

} // namespace LX_demo::lxe_editor
