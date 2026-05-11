#pragma once

#include "core/math/bounds.hpp"
#include "core/math/vec.hpp"
#include "core/platform/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace LX_demo::lxe_editor {

enum class AutomationSceneSourceKind {
  Unknown,
  Asset,
  Local,
  External,
};

enum class AutomationPermissionLevel {
  Unknown,
  User,
  Admin,
};

enum class AutomationEditMode {
  Unknown,
  Selection,
  Orbit,
  FreeFly,
};

enum class AutomationEventType {
  CommandExecuted,
  SceneLoaded,
  SceneSaved,
  SelectionChanged,
  ModeChanged,
  PreviewChanged,
  DirtyChanged,
};

struct AutomationError final {
  std::string code;
  std::string message;

  bool operator==(const AutomationError&) const = default;
};

struct AutomationCommandRequest final {
  std::string line;

  bool operator==(const AutomationCommandRequest&) const = default;
};

struct AutomationCommandResponse final {
  bool ok = false;
  std::string line;
  std::string message;
  std::string structuredJson;
  std::unordered_map<std::string, std::string> metadata;
  std::optional<AutomationError> error;
  u64 timestampMs = 0;

  bool operator==(const AutomationCommandResponse&) const = default;
};

struct AutomationEventCursor final {
  u64 nextSequence = 1;

  bool operator==(const AutomationEventCursor&) const = default;
};

struct AutomationAabb final {
  LX_core::Vec3f min{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f max{0.0f, 0.0f, 0.0f};

  bool operator==(const AutomationAabb&) const = default;
};

struct AutomationSceneSummary final {
  std::string sceneName;
  std::string currentDocumentPath;
  AutomationSceneSourceKind sourceKind = AutomationSceneSourceKind::Unknown;
  AutomationPermissionLevel permission = AutomationPermissionLevel::Unknown;
  bool dirty = false;

  bool operator==(const AutomationSceneSummary&) const = default;
};

struct AutomationSelectionSnapshot final {
  std::vector<std::string> selectedPaths;
  std::string primaryPath;
  std::optional<AutomationAabb> primaryWorldBounds;
  std::optional<LX_core::Vec3f> lastHitPoint;

  bool operator==(const AutomationSelectionSnapshot&) const = default;
};

struct AutomationCameraPose final {
  std::string path;
  LX_core::Vec3f eye{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f target{0.0f, 0.0f, -1.0f};
  LX_core::Vec3f up{0.0f, 1.0f, 0.0f};
  bool active = false;

  bool operator==(const AutomationCameraPose&) const = default;
};

struct AutomationCameraSnapshot final {
  std::string activeCameraPath;
  AutomationCameraPose editor;
  AutomationCameraPose game;

  bool operator==(const AutomationCameraSnapshot&) const = default;
};

struct AutomationToolbarSnapshot final {
  AutomationEditMode editMode = AutomationEditMode::Unknown;
  bool previewEnabled = false;

  bool operator==(const AutomationToolbarSnapshot&) const = default;
};

struct AutomationStateSnapshot final {
  AutomationSceneSummary scene;
  AutomationSelectionSnapshot selection;
  AutomationCameraSnapshot cameras;
  AutomationToolbarSnapshot toolbar;

  bool operator==(const AutomationStateSnapshot&) const = default;
};

struct AutomationCommandEventPayload final {
  std::string line;
  bool ok = false;
  std::string message;
  std::string structuredJson;
  std::unordered_map<std::string, std::string> metadata;
  u64 timestampMs = 0;

  bool operator==(const AutomationCommandEventPayload&) const = default;
};

struct AutomationEvent final {
  u64 sequence = 0;
  AutomationEventType type = AutomationEventType::CommandExecuted;
  std::optional<AutomationCommandEventPayload> command;
  std::optional<AutomationStateSnapshot> state;
  std::string payloadJson;

  bool operator==(const AutomationEvent&) const = default;
};

struct AutomationEventBatch final {
  AutomationEventCursor nextCursor;
  std::vector<AutomationEvent> events;

  bool operator==(const AutomationEventBatch&) const = default;
};

[[nodiscard]] const char* automationSceneSourceKindName(
    AutomationSceneSourceKind kind);
[[nodiscard]] const char* automationPermissionLevelName(
    AutomationPermissionLevel level);
[[nodiscard]] const char* automationEditModeName(AutomationEditMode mode);
[[nodiscard]] const char* automationEventTypeName(AutomationEventType type);

[[nodiscard]] std::string automationJsonEscape(std::string_view text);
[[nodiscard]] std::string toJson(const AutomationError& error);
[[nodiscard]] std::string toJson(const AutomationCommandRequest& request);
[[nodiscard]] std::string toJson(const AutomationCommandResponse& response);
[[nodiscard]] std::string toJson(const AutomationEventCursor& cursor);
[[nodiscard]] std::string toJson(const AutomationAabb& bounds);
[[nodiscard]] std::string toJson(const AutomationSceneSummary& summary);
[[nodiscard]] std::string toJson(const AutomationSelectionSnapshot& selection);
[[nodiscard]] std::string toJson(const AutomationCameraPose& pose);
[[nodiscard]] std::string toJson(const AutomationCameraSnapshot& cameras);
[[nodiscard]] std::string toJson(const AutomationToolbarSnapshot& toolbar);
[[nodiscard]] std::string toJson(const AutomationStateSnapshot& state);
[[nodiscard]] std::string toJson(
    const AutomationCommandEventPayload& payload);
[[nodiscard]] std::string toJson(const AutomationEvent& event);
[[nodiscard]] std::string toJson(const AutomationEventBatch& batch);
[[nodiscard]] std::optional<AutomationAabb> automationAabbFromBounds(
    const LX_core::BoundingBox& bounds);

} // namespace LX_demo::lxe_editor
