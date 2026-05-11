#include "core/editor/console_input_controller.hpp"

#include <algorithm>
#include <cstring>

#include <imgui.h>

namespace LX_core {
namespace {

constexpr usize kMaxPersistedHistoryEntries = 50;

void clampPersistedHistory(std::vector<std::string> &history) {
  history.erase(
      std::remove_if(history.begin(), history.end(),
                     [](const std::string &line) { return line.empty(); }),
      history.end());
  if (history.size() > kMaxPersistedHistoryEntries) {
    history.erase(history.begin(),
                  history.begin() + static_cast<std::ptrdiff_t>(
                                        history.size() - kMaxPersistedHistoryEntries));
  }
}

} // namespace

ConsoleInputController::ConsoleInputController(CommandBus &commandBus)
    : m_commandBus(commandBus) {}

void ConsoleInputController::submitLine(std::string_view line) {
  const std::string trimmed = trim(line);
  if (trimmed.empty()) {
    setInputText({});
    m_historyBrowseIndex.reset();
    m_historyBrowseDraft.reset();
    return;
  }

  (void)m_commandBus.dispatch(trimmed);
  appendPersistedHistoryLine(trimmed);
  markCommandDispatched();
}

void ConsoleInputController::submitCurrentInput() { submitLine(inputText()); }

void ConsoleInputController::autocomplete() {
  const std::string input = inputText();
  if (trim(input).empty()) {
    return;
  }

  const CompletionResult completion = m_commandBus.complete(input);
  if (completion.candidates.empty() || completion.commonPrefix.empty()) {
    return;
  }

  std::string completedText;
  const usize lastWhitespace = input.find_last_of(" \t");
  if (lastWhitespace == std::string::npos) {
    completedText = completion.commonPrefix;
  } else {
    completedText = input.substr(0, lastWhitespace + 1) + completion.commonPrefix;
  }

  if (completion.candidates.size() == 1) {
    completedText.push_back(' ');
    setInputText(completedText);
    return;
  }

  if (completedText != input) {
    setInputText(completedText);
    return;
  }

  clearHelperOutput();
  appendHelperLine("? completion candidates:");
  for (const auto &candidate : completion.candidates) {
    appendHelperLine("  " + candidate);
  }
}

void ConsoleInputController::browseHistoryOlder() {
  const auto &history = m_persistedHistory;
  if (history.empty()) {
    return;
  }

  beginHistoryBrowseIfNeeded();
  if (!m_historyBrowseIndex.has_value()) {
    m_historyBrowseIndex = history.size() - 1;
  } else if (*m_historyBrowseIndex > 0) {
    --(*m_historyBrowseIndex);
  }
  setInputFromHistoryIndex(*m_historyBrowseIndex);
}

void ConsoleInputController::browseHistoryNewer() {
  const auto &history = m_persistedHistory;
  if (!m_historyBrowseIndex.has_value() || history.empty()) {
    return;
  }

  if (*m_historyBrowseIndex + 1 < history.size()) {
    ++(*m_historyBrowseIndex);
    setInputFromHistoryIndex(*m_historyBrowseIndex);
    return;
  }

  m_historyBrowseIndex.reset();
  if (m_historyBrowseDraft.has_value()) {
    setInputText(*m_historyBrowseDraft);
  } else {
    setInputText({});
  }
}

void ConsoleInputController::cancelHistoryBrowse() {
  if (!m_historyBrowseIndex.has_value()) {
    return;
  }
  m_historyBrowseIndex.reset();
  if (m_historyBrowseDraft.has_value()) {
    setInputText(*m_historyBrowseDraft);
  } else {
    setInputText({});
  }
}

void ConsoleInputController::dispatchUndo() {
  (void)m_commandBus.dispatch("undo");
  appendPersistedHistoryLine("undo");
  markCommandDispatched();
}

void ConsoleInputController::dispatchRedo() {
  (void)m_commandBus.dispatch("redo");
  appendPersistedHistoryLine("redo");
  markCommandDispatched();
}

void ConsoleInputController::setPersistedHistory(
    std::vector<std::string> historyLines) {
  clampPersistedHistory(historyLines);
  m_persistedHistory = std::move(historyLines);
  m_historyBrowseIndex.reset();
  m_historyBrowseDraft.reset();
  m_persistedHistoryDirty = false;
}

void ConsoleInputController::recordPersistedHistoryLine(std::string line) {
  appendPersistedHistoryLine(std::move(line));
}

std::vector<std::string> ConsoleInputController::persistedHistory() const {
  return m_persistedHistory;
}

bool ConsoleInputController::consumePersistedHistoryDirty() {
  const bool dirty = m_persistedHistoryDirty;
  m_persistedHistoryDirty = false;
  return dirty;
}

void ConsoleInputController::setInputText(std::string_view text) {
  const usize copyLength = std::min(text.size(), m_inputBuffer.size() - 1);
  std::fill(m_inputBuffer.begin(), m_inputBuffer.end(), '\0');
  if (copyLength > 0) {
    std::memcpy(m_inputBuffer.data(), text.data(), copyLength);
  }
}

std::string ConsoleInputController::inputText() const {
  return std::string(m_inputBuffer.data());
}

char *ConsoleInputController::inputBufferData() { return m_inputBuffer.data(); }

usize ConsoleInputController::inputBufferSize() const { return m_inputBuffer.size(); }

std::string ConsoleInputController::helperOutputText() const {
  std::string output;
  for (usize i = 0; i < m_helperLines.size(); ++i) {
    output += m_helperLines[i];
    if (i + 1 < m_helperLines.size()) {
      output += '\n';
    }
  }
  return output;
}

void ConsoleInputController::clearHelperOutput() { m_helperLines.clear(); }

int ConsoleInputController::handleCallbackEvent(const int eventFlag,
                                                const int eventKey) {
  switch (eventFlag) {
  case ImGuiInputTextFlags_CallbackCompletion:
    autocomplete();
    return 0;
  case ImGuiInputTextFlags_CallbackHistory:
    if (eventKey == ImGuiKey_UpArrow) {
      browseHistoryOlder();
    } else if (eventKey == ImGuiKey_DownArrow) {
      browseHistoryNewer();
    }
    return 0;
  default:
    return 0;
  }
}

void ConsoleInputController::beginHistoryBrowseIfNeeded() {
  if (!m_historyBrowseIndex.has_value()) {
    m_historyBrowseDraft = inputText();
  }
}

void ConsoleInputController::setInputFromHistoryIndex(const usize historyIndex) {
  const auto &history = m_persistedHistory;
  if (historyIndex >= history.size()) {
    return;
  }
  setInputText(history[historyIndex]);
}

void ConsoleInputController::markCommandDispatched() {
  setInputText({});
  m_historyBrowseIndex.reset();
  m_historyBrowseDraft.reset();
  clearHelperOutput();
}

void ConsoleInputController::appendPersistedHistoryLine(std::string line) {
  if (line.empty()) {
    return;
  }
  m_persistedHistory.push_back(std::move(line));
  clampPersistedHistory(m_persistedHistory);
  m_persistedHistoryDirty = true;
}

void ConsoleInputController::appendHelperLine(std::string line) {
  m_helperLines.push_back(std::move(line));
}

std::string ConsoleInputController::trim(std::string_view text) {
  usize begin = 0;
  while (begin < text.size() &&
         (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
          text[begin] == '\n')) {
    ++begin;
  }

  usize end = text.size();
  while (end > begin &&
         (text[end - 1] == ' ' || text[end - 1] == '\t' ||
          text[end - 1] == '\r' || text[end - 1] == '\n')) {
    --end;
  }

  return std::string(text.substr(begin, end - begin));
}

std::string ConsoleInputController::commonPrefix(const std::string &a,
                                                 const std::string &b) {
  const usize length = std::min(a.size(), b.size());
  usize i = 0;
  while (i < length && a[i] == b[i]) {
    ++i;
  }
  return a.substr(0, i);
}

} // namespace LX_core
