#include "demos/lxe_editor/recording_controller.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::string jsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
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

[[nodiscard]] std::string isoTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t timeNow = std::chrono::system_clock::to_time_t(now);
  std::tm tmNow{};
#if defined(_WIN32)
  gmtime_s(&tmNow, &timeNow);
#else
  gmtime_r(&timeNow, &tmNow);
#endif
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tmNow);
  return buffer;
}

[[nodiscard]] std::string fileTimestamp() {
  std::string value = isoTimestamp();
  value.erase(std::remove(value.begin(), value.end(), ':'), value.end());
  value.erase(std::remove(value.begin(), value.end(), '-'), value.end());
  value.erase(std::remove(value.begin(), value.end(), 'Z'), value.end());
  return value;
}

[[nodiscard]] bool looksLikePath(const std::string& value) {
  return value.find('/') != std::string::npos ||
         value.find('\\') != std::string::npos;
}

} // namespace

RecordingController::RecordingController(std::filesystem::path runtimeRoot)
    : m_runtimeRoot(std::move(runtimeRoot)) {}

void RecordingController::enable() { m_enabled = true; }

bool RecordingController::disable(const bool force) {
  if (m_active && !force) {
    return false;
  }
  if (m_active) {
    m_active = false;
    m_steps.clear();
  }
  m_enabled = false;
  return true;
}

RecordingStatus RecordingController::status() const {
  return RecordingStatus{
      .enabled = m_enabled,
      .active = m_active,
      .sessionId = m_sessionId,
      .detailLevel = m_detailLevel,
      .stepCount = static_cast<int>(m_steps.size()),
      .lastSavedPath = m_lastSavedPath,
  };
}

RecordingStartResult RecordingController::start(RecordingStartOptions options) {
  if (!m_enabled) {
    throw std::runtime_error("recording is disabled");
  }
  if (m_active) {
    return RecordingStartResult{.active = true, .sessionId = m_sessionId};
  }

  m_active = true;
  m_detailLevel = options.detailLevel;
  m_scenePath = std::move(options.scenePath);
  m_buildInfoJson =
      options.buildInfoJson.empty() ? std::string{"{}"} : options.buildInfoJson;
  m_windowWidth = options.windowWidth;
  m_windowHeight = options.windowHeight;
  m_steps.clear();
  m_sessionId = fileTimestamp();
  m_startedAt = std::chrono::steady_clock::now();
  return RecordingStartResult{.active = true, .sessionId = m_sessionId};
}

RecordingAppendResult RecordingController::appendStep(RecordingStepInput input) {
  if (!m_enabled || !m_active) {
    return RecordingAppendResult{};
  }
  if (input.kind.empty()) {
    throw std::runtime_error("recording step kind must not be empty");
  }
  if (input.payloadJson.empty()) {
    input.payloadJson = "{}";
  }

  const int stepId = static_cast<int>(m_steps.size()) + 1;
  m_steps.push_back(Step{
      .id = stepId,
      .kind = std::move(input.kind),
      .source = input.source,
      .timeOffsetMs = currentOffsetMs(),
      .payloadJson = std::move(input.payloadJson),
  });
  return RecordingAppendResult{.recorded = true, .stepId = stepId};
}

RecordingStopResult RecordingController::stop(RecordingStopOptions options) {
  if (!m_active) {
    return RecordingStopResult{
        .saved = false,
        .path = m_lastSavedPath.value_or(std::filesystem::path{}),
        .stepCount = static_cast<int>(m_steps.size()),
        .sessionId = m_sessionId,
    };
  }

  std::filesystem::path savedPath;
  bool saved = false;
  if (options.save) {
    savedPath = saveActiveSession();
    m_lastSavedPath = savedPath;
    saved = true;
  }
  m_active = false;
  return RecordingStopResult{
      .saved = saved,
      .path = savedPath,
      .stepCount = static_cast<int>(m_steps.size()),
      .sessionId = m_sessionId,
  };
}

void RecordingController::discardStoppedSession() {
  if (!m_active) {
    m_steps.clear();
  }
}

std::vector<RecordingListEntry> RecordingController::list() const {
  std::vector<RecordingListEntry> entries;
  const auto root = recordingsRoot();
  if (!std::filesystem::exists(root)) {
    return entries;
  }
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    entries.push_back(RecordingListEntry{
        .id = entry.path().filename().string(),
        .path = entry.path(),
    });
  }
  std::sort(entries.begin(), entries.end(),
            [](const RecordingListEntry& a, const RecordingListEntry& b) {
              return a.id < b.id;
            });
  return entries;
}

std::string RecordingController::read(const std::string& idOrPath) const {
  std::filesystem::path path;
  if (idOrPath == "active") {
    return buildRecordingJson();
  }
  if (looksLikePath(idOrPath)) {
    path = idOrPath;
  } else {
    path = recordingsRoot() / idOrPath;
  }
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("recording not found: " + idOrPath);
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

std::filesystem::path RecordingController::recordingsRoot() const {
  return m_runtimeRoot / "recordings";
}

std::filesystem::path RecordingController::saveActiveSession() {
  std::filesystem::create_directories(recordingsRoot());
  const std::filesystem::path path =
      recordingsRoot() / (m_sessionId + "-recording.json");
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open recording for write: " +
                             path.string());
  }
  out << buildRecordingJson();
  return path;
}

std::string RecordingController::buildRecordingJson() const {
  std::ostringstream out;
  out << "{";
  out << "\"schemaVersion\":1,";
  out << "\"metadata\":{";
  out << "\"startedAt\":\"" << jsonEscape(m_sessionId) << "\",";
  out << "\"editorVersion\":\"unknown\",";
#if defined(_WIN32)
  out << "\"platform\":\"windows\",";
#elif defined(__APPLE__)
  out << "\"platform\":\"macos\",";
#else
  out << "\"platform\":\"linux\",";
#endif
  out << "\"scenePath\":\"" << jsonEscape(m_scenePath) << "\",";
  out << "\"build\":" << m_buildInfoJson << ",";
  out << "\"window\":{\"width\":" << m_windowWidth
      << ",\"height\":" << m_windowHeight << "}";
  out << "},";
  out << "\"detailLevel\":\"" << recordingDetailLevelName(m_detailLevel) << "\",";
  out << "\"steps\":[";
  for (size_t i = 0; i < m_steps.size(); ++i) {
    const Step& step = m_steps[i];
    if (i > 0) {
      out << ",";
    }
    out << "{";
    out << "\"id\":" << step.id << ",";
    out << "\"kind\":\"" << jsonEscape(step.kind) << "\",";
    out << "\"source\":\"" << recordingSourceName(step.source) << "\",";
    out << "\"timeOffsetMs\":" << step.timeOffsetMs << ",";
    out << "\"payload\":" << step.payloadJson;
    out << "}";
  }
  out << "],";
  out << "\"snapshots\":{},";
  out << "\"errors\":[]";
  out << "}";
  return out.str();
}

long long RecordingController::currentOffsetMs() const {
  if (!m_active) {
    return 0;
  }
  const auto elapsed = std::chrono::steady_clock::now() - m_startedAt;
  return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

std::string recordingDetailLevelName(const RecordingDetailLevel level) {
  switch (level) {
  case RecordingDetailLevel::Basic:
    return "basic";
  case RecordingDetailLevel::Diagnostic:
    return "diagnostic";
  case RecordingDetailLevel::Trace:
    return "trace";
  }
  return "basic";
}

std::optional<RecordingDetailLevel>
recordingDetailLevelFromName(const std::string& name) {
  if (name == "basic") {
    return RecordingDetailLevel::Basic;
  }
  if (name == "diagnostic") {
    return RecordingDetailLevel::Diagnostic;
  }
  if (name == "trace") {
    return RecordingDetailLevel::Trace;
  }
  return std::nullopt;
}

std::string recordingSourceName(const RecordingSource source) {
  switch (source) {
  case RecordingSource::UserUi:
    return "user_ui";
  case RecordingSource::Mcp:
    return "mcp";
  case RecordingSource::System:
    return "system";
  }
  return "system";
}

} // namespace LX_demo::lxe_editor
