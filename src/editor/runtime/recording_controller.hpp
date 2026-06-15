#pragma once

#include <filesystem>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {

enum class RecordingDetailLevel { Basic, Diagnostic, Trace };

enum class RecordingSource { UserUi, Mcp, System };

struct RecordingStartOptions {
  RecordingDetailLevel detailLevel = RecordingDetailLevel::Basic;
  std::string scenePath;
  std::string buildInfoJson = "{}";
  int windowWidth = 0;
  int windowHeight = 0;
};

struct RecordingStopOptions {
  bool save = true;
};

struct RecordingStepInput {
  std::string kind;
  RecordingSource source = RecordingSource::System;
  std::string payloadJson = "{}";
};

struct RecordingAppendResult {
  bool recorded = false;
  int stepId = 0;
};

struct RecordingStatus {
  bool enabled = false;
  bool active = false;
  std::string sessionId;
  RecordingDetailLevel detailLevel = RecordingDetailLevel::Basic;
  int stepCount = 0;
  std::optional<std::filesystem::path> lastSavedPath;
};

struct RecordingStartResult {
  bool active = false;
  std::string sessionId;
};

struct RecordingStopResult {
  bool saved = false;
  std::filesystem::path path;
  int stepCount = 0;
  std::string sessionId;
};

struct RecordingListEntry {
  std::string id;
  std::filesystem::path path;
};

class RecordingController final {
public:
  explicit RecordingController(std::filesystem::path runtimeRoot);

  void enable();
  [[nodiscard]] bool disable(bool force = false);
  [[nodiscard]] RecordingStatus status() const;

  [[nodiscard]] RecordingStartResult start(RecordingStartOptions options = {});
  [[nodiscard]] RecordingAppendResult appendStep(RecordingStepInput input);
  [[nodiscard]] RecordingStopResult stop(RecordingStopOptions options = {});
  void discardStoppedSession();

  [[nodiscard]] std::vector<RecordingListEntry> list() const;
  [[nodiscard]] std::string read(const std::string& idOrPath) const;
  [[nodiscard]] std::filesystem::path recordingsRoot() const;

private:
  struct Step {
    int id = 0;
    std::string kind;
    RecordingSource source = RecordingSource::System;
    long long timeOffsetMs = 0;
    std::string payloadJson = "{}";
  };

  [[nodiscard]] std::filesystem::path saveActiveSession();
  [[nodiscard]] std::string buildRecordingJson() const;
  [[nodiscard]] long long currentOffsetMs() const;

  std::filesystem::path m_runtimeRoot;
  bool m_enabled = false;
  bool m_active = false;
  std::string m_sessionId;
  RecordingDetailLevel m_detailLevel = RecordingDetailLevel::Basic;
  std::string m_scenePath;
  std::string m_buildInfoJson = "{}";
  int m_windowWidth = 0;
  int m_windowHeight = 0;
  std::vector<Step> m_steps;
  std::optional<std::filesystem::path> m_lastSavedPath;
  std::chrono::steady_clock::time_point m_startedAt{};
};

[[nodiscard]] std::string recordingDetailLevelName(RecordingDetailLevel level);
[[nodiscard]] std::optional<RecordingDetailLevel>
recordingDetailLevelFromName(const std::string& name);
[[nodiscard]] std::string recordingSourceName(RecordingSource source);

} // namespace LX_demo::lxe_editor
