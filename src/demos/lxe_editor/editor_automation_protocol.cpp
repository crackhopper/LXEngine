#include "demos/lxe_editor/editor_automation_protocol.hpp"

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
  out += automationJsonEscape(value);
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
    out += automationJsonEscape(keys[i]);
    out += "\":\"";
    out += automationJsonEscape(metadata.at(keys[i]));
    out += '"';
  }
  out += '}';
  return out;
}

} // namespace

const char* automationSceneSourceKindName(const AutomationSceneSourceKind kind) {
  switch (kind) {
  case AutomationSceneSourceKind::Asset:
    return "asset";
  case AutomationSceneSourceKind::Local:
    return "local";
  case AutomationSceneSourceKind::External:
    return "external";
  case AutomationSceneSourceKind::Unknown:
    break;
  }
  return "unknown";
}

const char* automationPermissionLevelName(
    const AutomationPermissionLevel level) {
  switch (level) {
  case AutomationPermissionLevel::User:
    return "user";
  case AutomationPermissionLevel::Admin:
    return "admin";
  case AutomationPermissionLevel::Unknown:
    break;
  }
  return "unknown";
}

const char* automationEditModeName(const AutomationEditMode mode) {
  switch (mode) {
  case AutomationEditMode::Selection:
    return "selection";
  case AutomationEditMode::Orbit:
    return "orbit";
  case AutomationEditMode::FreeFly:
    return "freefly";
  case AutomationEditMode::Unknown:
    break;
  }
  return "unknown";
}

const char* automationEventTypeName(const AutomationEventType type) {
  switch (type) {
  case AutomationEventType::CommandExecuted:
    return "command.executed";
  case AutomationEventType::SceneLoaded:
    return "scene.loaded";
  case AutomationEventType::SceneSaved:
    return "scene.saved";
  case AutomationEventType::SelectionChanged:
    return "selection.changed";
  case AutomationEventType::ModeChanged:
    return "mode.changed";
  case AutomationEventType::PreviewChanged:
    return "preview.changed";
  case AutomationEventType::DirtyChanged:
    return "dirty.changed";
  }
  return "unknown";
}

std::string automationJsonEscape(const std::string_view text) {
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

std::string toJson(const AutomationError& error) {
  std::string out = "{";
  appendJsonStringField(out, "code", error.code, true);
  appendJsonStringField(out, "message", error.message);
  out += '}';
  return out;
}

std::string toJson(const AutomationCommandRequest& request) {
  std::string out = "{";
  appendJsonStringField(out, "line", request.line, true);
  out += '}';
  return out;
}

std::string toJson(const AutomationCommandResponse& response) {
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

std::string toJson(const AutomationEventCursor& cursor) {
  std::string out = "{";
  appendJsonUIntField(out, "nextSequence", cursor.nextSequence, true);
  out += '}';
  return out;
}

std::string toJson(const AutomationAabb& bounds) {
  std::string out = "{";
  out += "\"min\":";
  out += toJson(bounds.min);
  out += ",\"max\":";
  out += toJson(bounds.max);
  out += '}';
  return out;
}

std::string toJson(const AutomationSceneSummary& summary) {
  std::string out = "{";
  appendJsonStringField(out, "sceneName", summary.sceneName, true);
  appendJsonStringField(out, "currentDocumentPath",
                        summary.currentDocumentPath);
  appendJsonStringField(out, "sourceKind",
                        automationSceneSourceKindName(summary.sourceKind));
  appendJsonStringField(out, "permission",
                        automationPermissionLevelName(summary.permission));
  appendJsonBoolField(out, "dirty", summary.dirty);
  out += '}';
  return out;
}

std::string toJson(const AutomationSelectionSnapshot& selection) {
  std::string out = "{";
  out += "\"selectedPaths\":[";
  for (size_t i = 0; i < selection.selectedPaths.size(); ++i) {
    if (i != 0) {
      out += ',';
    }
    out += '"';
    out += automationJsonEscape(selection.selectedPaths[i]);
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

std::string toJson(const AutomationCameraPose& pose) {
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

std::string toJson(const AutomationCameraSnapshot& cameras) {
  std::string out = "{";
  appendJsonStringField(out, "activeCameraPath", cameras.activeCameraPath, true);
  out += ",\"editor\":";
  out += toJson(cameras.editor);
  out += ",\"game\":";
  out += toJson(cameras.game);
  out += '}';
  return out;
}

std::string toJson(const AutomationToolbarSnapshot& toolbar) {
  std::string out = "{";
  appendJsonStringField(out, "editMode",
                        automationEditModeName(toolbar.editMode), true);
  appendJsonBoolField(out, "previewEnabled", toolbar.previewEnabled);
  out += '}';
  return out;
}

std::string toJson(const AutomationStateSnapshot& state) {
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

std::string toJson(const AutomationCommandEventPayload& payload) {
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

std::string toJson(const AutomationEvent& event) {
  std::string payload = event.payloadJson;
  if (payload.empty()) {
    if (event.command.has_value()) {
      payload = toJson(*event.command);
    } else if (event.state.has_value()) {
      payload = toJson(*event.state);
    } else {
      payload = "{}";
    }
  }

  std::string out = "{";
  appendJsonStringField(out, "type", automationEventTypeName(event.type), true);
  appendJsonUIntField(out, "seq", event.sequence);
  out += ",\"payload\":";
  out += payload;
  out += '}';
  return out;
}

std::string toJson(const AutomationEventBatch& batch) {
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

std::optional<AutomationAabb> automationAabbFromBounds(
    const LX_core::BoundingBox& bounds) {
  if (!bounds.isValid()) {
    return std::nullopt;
  }
  return AutomationAabb{bounds.min, bounds.max};
}

} // namespace LX_demo::lxe_editor
