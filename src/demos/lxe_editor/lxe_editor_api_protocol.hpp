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

enum class ApiSceneSourceKind {
  Unknown,
  Asset,
  Local,
  External,
};

enum class ApiPermissionLevel {
  Unknown,
  User,
  Admin,
};

enum class ApiEditMode {
  Unknown,
  Selection,
  Orbit,
  FreeFly,
};

enum class ApiEventType {
  CommandExecuted,
  SceneLoaded,
  SceneSaved,
  SelectionChanged,
  ModeChanged,
  PreviewChanged,
  DirtyChanged,
  SceneNodeChanged,
};

struct ApiError final {
  std::string code;
  std::string message;

  bool operator==(const ApiError&) const = default;
};

struct ApiCommandRequest final {
  std::string line;

  bool operator==(const ApiCommandRequest&) const = default;
};

struct ApiCommandResponse final {
  bool ok = false;
  std::string line;
  std::string message;
  std::string structuredJson;
  std::unordered_map<std::string, std::string> metadata;
  std::optional<ApiError> error;
  u64 timestampMs = 0;

  bool operator==(const ApiCommandResponse&) const = default;
};

struct ApiEventCursor final {
  u64 nextSequence = 1;

  bool operator==(const ApiEventCursor&) const = default;
};

struct ApiAabb final {
  LX_core::Vec3f min{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f max{0.0f, 0.0f, 0.0f};

  bool operator==(const ApiAabb&) const = default;
};

struct ApiSceneSummary final {
  std::string sceneName;
  std::string currentDocumentPath;
  ApiSceneSourceKind sourceKind = ApiSceneSourceKind::Unknown;
  ApiPermissionLevel permission = ApiPermissionLevel::Unknown;
  bool dirty = false;

  bool operator==(const ApiSceneSummary&) const = default;
};

struct ApiSelectionSnapshot final {
  std::vector<std::string> selectedPaths;
  std::string primaryPath;
  std::optional<ApiAabb> primaryWorldBounds;
  std::optional<LX_core::Vec3f> lastHitPoint;

  bool operator==(const ApiSelectionSnapshot&) const = default;
};

struct ApiCameraPose final {
  std::string path;
  LX_core::Vec3f eye{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f target{0.0f, 0.0f, -1.0f};
  LX_core::Vec3f up{0.0f, 1.0f, 0.0f};
  bool active = false;

  bool operator==(const ApiCameraPose&) const = default;
};

struct ApiCameraSnapshot final {
  std::string activeCameraPath;
  ApiCameraPose editor;
  ApiCameraPose game;

  bool operator==(const ApiCameraSnapshot&) const = default;
};

struct ApiToolbarSnapshot final {
  ApiEditMode editMode = ApiEditMode::Unknown;
  bool previewEnabled = false;

  bool operator==(const ApiToolbarSnapshot&) const = default;
};

struct ApiStateSnapshot final {
  ApiSceneSummary scene;
  ApiSelectionSnapshot selection;
  ApiCameraSnapshot cameras;
  ApiToolbarSnapshot toolbar;

  bool operator==(const ApiStateSnapshot&) const = default;
};

struct ApiCommandEventPayload final {
  std::string line;
  bool ok = false;
  std::string message;
  std::string structuredJson;
  std::unordered_map<std::string, std::string> metadata;
  u64 timestampMs = 0;

  bool operator==(const ApiCommandEventPayload&) const = default;
};

struct ApiSceneNodeEventPayload final {
  std::string path;
  std::string stableNodeName;
  std::vector<std::string> aspects;

  bool operator==(const ApiSceneNodeEventPayload&) const = default;
};

struct ApiEvent final {
  u64 sequence = 0;
  ApiEventType type = ApiEventType::CommandExecuted;
  std::optional<ApiCommandEventPayload> command;
  std::optional<ApiSceneNodeEventPayload> sceneNode;
  std::optional<ApiStateSnapshot> state;
  std::string payloadJson;

  bool operator==(const ApiEvent&) const = default;
};

struct ApiEventBatch final {
  ApiEventCursor nextCursor;
  std::vector<ApiEvent> events;

  bool operator==(const ApiEventBatch&) const = default;
};

[[nodiscard]] const char* apiSceneSourceKindName(
    ApiSceneSourceKind kind);
[[nodiscard]] const char* apiPermissionLevelName(
    ApiPermissionLevel level);
[[nodiscard]] const char* apiEditModeName(ApiEditMode mode);
[[nodiscard]] const char* apiEventTypeName(ApiEventType type);

[[nodiscard]] std::string apiJsonEscape(std::string_view text);
[[nodiscard]] std::string toJson(const ApiError& error);
[[nodiscard]] std::string toJson(const ApiCommandRequest& request);
[[nodiscard]] std::string toJson(const ApiCommandResponse& response);
[[nodiscard]] std::string toJson(const ApiEventCursor& cursor);
[[nodiscard]] std::string toJson(const ApiAabb& bounds);
[[nodiscard]] std::string toJson(const ApiSceneSummary& summary);
[[nodiscard]] std::string toJson(const ApiSelectionSnapshot& selection);
[[nodiscard]] std::string toJson(const ApiCameraPose& pose);
[[nodiscard]] std::string toJson(const ApiCameraSnapshot& cameras);
[[nodiscard]] std::string toJson(const ApiToolbarSnapshot& toolbar);
[[nodiscard]] std::string toJson(const ApiStateSnapshot& state);
[[nodiscard]] std::string toJson(
    const ApiCommandEventPayload& payload);
[[nodiscard]] std::string toJson(
    const ApiSceneNodeEventPayload& payload);
[[nodiscard]] std::string toJson(const ApiEvent& event);
[[nodiscard]] std::string toJson(const ApiEventBatch& batch);
[[nodiscard]] std::optional<ApiAabb> apiAabbFromBounds(
    const LX_core::BoundingBox& bounds);

} // namespace LX_demo::lxe_editor
