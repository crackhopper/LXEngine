#include "core/editor/console_panel.hpp"

#include <algorithm>
#include <cstring>

#include <imgui.h>

namespace LX_core {

ConsolePanel::ConsolePanel(CommandBus &commandBus) : m_commandBus(commandBus) {}

void ConsolePanel::draw() {
  if (!m_open) {
    return;
  }

  if (!ImGui::Begin("Command Console", &m_open)) {
    ImGui::End();
    return;
  }

  if (ImGui::Button("clear")) {
    clearDisplay();
  }

  const float inputHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f;
  if (ImGui::BeginChild("console_output", ImVec2(0.0f, -inputHeight), true)) {
    for (const auto &entry : displayedEntries()) {
      ImGui::TextWrapped("< %s", entry.line.c_str());
      ImGui::TextWrapped("> %s", entry.result.message.c_str());
      ImGui::Separator();
    }

    if (m_scrollToBottom) {
      ImGui::SetScrollHereY(1.0f);
      m_scrollToBottom = false;
    }
  }
  ImGui::EndChild();

  ImGui::PushItemWidth(-1.0f);
  const bool submitted = ImGui::InputText(
      "##command_input", m_inputBuffer.data(), m_inputBuffer.size(),
      ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::PopItemWidth();

  if (ImGui::IsItemActive()) {
    const ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
      browseHistoryOlder();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
      browseHistoryNewer();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
      autocompleteInput();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
      dispatchUndo();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
      dispatchRedo();
    }
  }

  if (submitted) {
    submitCurrentInput();
    ImGui::SetKeyboardFocusHere(-1);
  }

  ImGui::End();
}

void ConsolePanel::submitLine(std::string_view line) {
  const std::string trimmed = trim(line);
  if (trimmed.empty()) {
    setInputText({});
    m_historyBrowseIndex.reset();
    return;
  }

  const CommandResult result = m_commandBus.dispatch(trimmed);
  (void)result;
  markCommandDispatched();
}

void ConsolePanel::submitCurrentInput() { submitLine(getInputText()); }

void ConsolePanel::clearDisplay() {
  m_displayStartIndex = m_commandBus.history().size();
  m_scrollToBottom = false;
}

void ConsolePanel::browseHistoryOlder() {
  const auto &history = m_commandBus.history();
  if (history.empty()) {
    return;
  }

  if (!m_historyBrowseIndex.has_value()) {
    m_historyBrowseIndex = history.size() - 1;
  } else if (*m_historyBrowseIndex > 0) {
    --(*m_historyBrowseIndex);
  }
  setInputFromHistoryIndex(*m_historyBrowseIndex);
}

void ConsolePanel::browseHistoryNewer() {
  const auto &history = m_commandBus.history();
  if (!m_historyBrowseIndex.has_value() || history.empty()) {
    return;
  }

  if (*m_historyBrowseIndex + 1 < history.size()) {
    ++(*m_historyBrowseIndex);
    setInputFromHistoryIndex(*m_historyBrowseIndex);
    return;
  }

  m_historyBrowseIndex.reset();
  setInputText({});
}

void ConsolePanel::autocompleteInput() {
  const std::string input = getInputText();
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
  }
  setInputText(completedText);
}

void ConsolePanel::dispatchUndo() {
  (void)m_commandBus.dispatch("undo");
  markCommandDispatched();
}

void ConsolePanel::dispatchRedo() {
  (void)m_commandBus.dispatch("redo");
  markCommandDispatched();
}

void ConsolePanel::setInputText(std::string_view text) {
  const usize copyLength = std::min(text.size(), m_inputBuffer.size() - 1);
  std::fill(m_inputBuffer.begin(), m_inputBuffer.end(), '\0');
  if (copyLength > 0) {
    std::memcpy(m_inputBuffer.data(), text.data(), copyLength);
  }
}

std::string ConsolePanel::getInputText() const {
  return std::string(m_inputBuffer.data());
}

std::vector<CommandBus::HistoryEntry> ConsolePanel::displayedEntries() const {
  const auto &history = m_commandBus.history();
  if (m_displayStartIndex >= history.size()) {
    return {};
  }
  return std::vector<CommandBus::HistoryEntry>(history.begin() + m_displayStartIndex,
                                               history.end());
}

bool ConsolePanel::isOpen() const { return m_open; }

void ConsolePanel::setOpen(const bool open) { m_open = open; }

void ConsolePanel::setInputFromHistoryIndex(const usize historyIndex) {
  const auto &history = m_commandBus.history();
  if (historyIndex >= history.size()) {
    return;
  }
  setInputText(history[historyIndex].line);
}

void ConsolePanel::markCommandDispatched() {
  setInputText({});
  m_historyBrowseIndex.reset();
  m_scrollToBottom = true;
}

std::string ConsolePanel::trim(std::string_view text) {
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

std::string ConsolePanel::commonPrefix(const std::string &a,
                                       const std::string &b) {
  const usize length = std::min(a.size(), b.size());
  usize i = 0;
  while (i < length && a[i] == b[i]) {
    ++i;
  }
  return a.substr(0, i);
}

} // namespace LX_core
