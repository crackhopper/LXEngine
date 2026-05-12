#include "core/editor/console_panel.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include <imgui.h>

namespace LX_core {

namespace {

[[nodiscard]] bool resultsMatchForAttachment(const CommandResult &lhs,
                                             const CommandResult &rhs) {
  return lhs.ok == rhs.ok && lhs.message == rhs.message;
}

} // namespace

ConsolePanel::ConsolePanel(CommandBus &commandBus)
    : m_commandBus(commandBus), m_inputController(commandBus) {}

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
  drawOutputRegion(inputHeight);

  ImGui::PushItemWidth(-1.0f);
  const bool submitted = ImGui::InputText(
      "##command_input", m_inputController.inputBufferData(),
      m_inputController.inputBufferSize(),
      ImGuiInputTextFlags_EnterReturnsTrue |
          ImGuiInputTextFlags_CallbackCompletion |
          ImGuiInputTextFlags_CallbackHistory,
      &ConsolePanel::inputTextCallback, &m_inputController);
  ImGui::PopItemWidth();

  if (ImGui::IsItemActive()) {
    const ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      m_inputController.cancelHistoryBrowse();
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
  m_inputController.submitLine(line);
  m_scrollToBottom = true;
}

void ConsolePanel::submitCurrentInput() { submitLine(getInputText()); }

void ConsolePanel::clearDisplay() {
  m_displayStartIndex = m_commandBus.history().size();
  m_orphanSystemLines.clear();
  m_entryAttachments.clear();
  m_pendingSystemAttachments.clear();
  m_inputController.clearHelperOutput();
  m_scrollToBottom = false;
}

void ConsolePanel::browseHistoryOlder() { m_inputController.browseHistoryOlder(); }

void ConsolePanel::browseHistoryNewer() { m_inputController.browseHistoryNewer(); }

void ConsolePanel::autocompleteInput() { m_inputController.autocomplete(); }

void ConsolePanel::dispatchUndo() {
  m_inputController.dispatchUndo();
  m_scrollToBottom = true;
}

void ConsolePanel::dispatchRedo() {
  m_inputController.dispatchRedo();
  m_scrollToBottom = true;
}

void ConsolePanel::setPersistedHistory(std::vector<std::string> historyLines) {
  m_inputController.setPersistedHistory(std::move(historyLines));
}

void ConsolePanel::recordPersistedHistoryLine(std::string_view line) {
  m_inputController.recordPersistedHistoryLine(std::string(line));
}

void ConsolePanel::appendSystemLine(std::string_view line) {
  if (line.empty()) {
    return;
  }
  if (m_displayStartIndex >= m_commandBus.history().size()) {
    m_orphanSystemLines.emplace_back(line);
  } else {
    const usize visibleIndex =
        m_commandBus.history().size() - m_displayStartIndex - 1;
    appendAttachmentToVisibleEntry(visibleIndex, line);
    queueSystemLineAttachment(line);
  }
  m_scrollToBottom = true;
}

std::vector<std::string> ConsolePanel::persistedHistory() const {
  return m_inputController.persistedHistory();
}

bool ConsolePanel::consumePersistedHistoryDirty() {
  return m_inputController.consumePersistedHistoryDirty();
}

void ConsolePanel::setInputText(std::string_view text) { m_inputController.setInputText(text); }

std::string ConsolePanel::getInputText() const { return m_inputController.inputText(); }

std::vector<CommandBus::HistoryEntry> ConsolePanel::displayedEntries() const {
  syncPendingSystemAttachments();
  const auto &history = m_commandBus.history();
  if (m_displayStartIndex >= history.size()) {
    return {};
  }
  return std::vector<CommandBus::HistoryEntry>(history.begin() + m_displayStartIndex,
                                               history.end());
}

std::vector<ConsolePanel::DisplayEntry> ConsolePanel::displayedDisplayEntries() const {
  syncPendingSystemAttachments();
  const auto &history = m_commandBus.history();
  if (m_displayStartIndex >= history.size()) {
    return {};
  }

  std::vector<DisplayEntry> entries;
  entries.reserve(history.size() - m_displayStartIndex);
  for (usize historyIndex = m_displayStartIndex; historyIndex < history.size();
       ++historyIndex) {
    const usize visibleIndex = historyIndex - m_displayStartIndex;
    DisplayEntry entry{.historyEntry = history[historyIndex], .attachments = {}};
    if (visibleIndex < m_entryAttachments.size()) {
      entry.attachments = m_entryAttachments[visibleIndex];
    }
    entries.emplace_back(std::move(entry));
  }
  return entries;
}

std::string ConsolePanel::displayedText() const {
  std::string output;
  const auto entries = displayedDisplayEntries();
  for (const auto &entry : entries) {
    if (!output.empty()) {
      output += "\n\n";
    }
    output += "> ";
    output += entry.historyEntry.line;
    output += '\n';
    output += entry.historyEntry.result.message;
    for (const auto &attachment : entry.attachments) {
      output += '\n';
      output += attachment;
    }
  }
  for (const auto &line : m_orphanSystemLines) {
    if (!output.empty()) {
      output += "\n\n";
    }
    output += line;
  }
  const std::string helperText = m_inputController.helperOutputText();
  if (!helperText.empty()) {
    if (!output.empty()) {
      output += "\n\n";
    }
    output += helperText;
  }
  return output;
}

void ConsolePanel::queueSystemLineAttachment(std::string_view line) {
  const usize historySize = m_commandBus.history().size();
  if (!m_pendingSystemAttachments.empty() &&
      m_pendingSystemAttachments.back().historySizeBeforeOwner == historySize &&
      m_pendingSystemAttachments.back().sourceHistoryIndex + 1 == historySize) {
    m_pendingSystemAttachments.back().lines.emplace_back(line);
    return;
  }

  PendingSystemAttachment attachment;
  attachment.sourceHistoryIndex = historySize - 1;
  attachment.historySizeBeforeOwner = historySize;
  attachment.lines.emplace_back(line);
  m_pendingSystemAttachments.emplace_back(std::move(attachment));
}

void ConsolePanel::appendAttachmentToVisibleEntry(const usize visibleIndex,
                                                  std::string_view line) const {
  if (m_entryAttachments.size() <= visibleIndex) {
    m_entryAttachments.resize(visibleIndex + 1);
  }
  m_entryAttachments[visibleIndex].emplace_back(line);
}

void ConsolePanel::removeTrailingAttachmentsFromVisibleEntry(
    const usize visibleIndex, const std::vector<std::string> &lines) const {
  if (visibleIndex >= m_entryAttachments.size()) {
    return;
  }
  auto &attachments = m_entryAttachments[visibleIndex];
  if (attachments.size() < lines.size()) {
    return;
  }
  const usize startIndex = attachments.size() - lines.size();
  for (usize i = 0; i < lines.size(); ++i) {
    if (attachments[startIndex + i] != lines[i]) {
      return;
    }
  }
  attachments.resize(startIndex);
}

void ConsolePanel::syncPendingSystemAttachments() const {
  if (m_pendingSystemAttachments.empty()) {
    return;
  }

  const usize historySize = m_commandBus.history().size();
  for (const auto &attachment : m_pendingSystemAttachments) {
    const bool ownerVisible =
        historySize > attachment.historySizeBeforeOwner &&
        attachment.historySizeBeforeOwner >= m_displayStartIndex;
    if (ownerVisible) {
      const usize sourceVisibleIndex =
          attachment.sourceHistoryIndex - m_displayStartIndex;
      removeTrailingAttachmentsFromVisibleEntry(sourceVisibleIndex,
                                                attachment.lines);
      usize ownerHistoryIndex = attachment.historySizeBeforeOwner;
      while (ownerHistoryIndex + 1 < historySize &&
             resultsMatchForAttachment(
                 m_commandBus.history()[ownerHistoryIndex].result,
                 m_commandBus.history()[ownerHistoryIndex + 1].result)) {
        ++ownerHistoryIndex;
      }
      const usize visibleIndex = ownerHistoryIndex - m_displayStartIndex;
      for (const auto &line : attachment.lines) {
        appendAttachmentToVisibleEntry(visibleIndex, line);
      }
      continue;
    }
  }

  m_pendingSystemAttachments.clear();
}

void ConsolePanel::drawOutputRegion(const float reservedInputHeight) {
  if (!ImGui::BeginChild("console_output", ImVec2(0.0f, -reservedInputHeight),
                         true)) {
    ImGui::EndChild();
    return;
  }

  for (const auto &entry : displayedDisplayEntries()) {
    drawDisplayEntry(entry);
    ImGui::Spacing();
  }

  for (const auto &line : m_orphanSystemLines) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(line.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
  }

  const std::string helperText = m_inputController.helperOutputText();
  if (!helperText.empty()) {
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(helperText.c_str());
    ImGui::PopTextWrapPos();
  }

  if (m_scrollToBottom) {
    ImGui::SetScrollHereY(1.0f);
    m_scrollToBottom = false;
  }

  ImGui::EndChild();
}

void ConsolePanel::drawDisplayEntry(const DisplayEntry &entry) const {
  ImGui::PushTextWrapPos(0.0f);
  const std::string commandLine = "> " + entry.historyEntry.line;
  ImGui::TextUnformatted(commandLine.c_str());
  ImGui::TextUnformatted(entry.historyEntry.result.message.c_str());
  for (const auto &attachment : entry.attachments) {
    ImGui::TextUnformatted(attachment.c_str());
  }
  ImGui::PopTextWrapPos();
}

bool ConsolePanel::isOpen() const { return m_open; }

void ConsolePanel::setOpen(const bool open) { m_open = open; }

int ConsolePanel::inputTextCallback(ImGuiInputTextCallbackData *data) {
  if (!data || !data->UserData) {
    return 0;
  }
  auto *controller =
      static_cast<ConsoleInputController *>(data->UserData);
  const int result =
      controller->handleCallbackEvent(data->EventFlag, data->EventKey);
  const std::string text = controller->inputText();
  const size_t copyLength =
      std::min(text.size(), static_cast<size_t>(data->BufSize - 1));
  std::memset(data->Buf, 0, static_cast<size_t>(data->BufSize));
  if (copyLength > 0) {
    std::memcpy(data->Buf, text.data(), copyLength);
  }
  data->BufTextLen = static_cast<int>(copyLength);
  data->CursorPos = data->BufTextLen;
  data->SelectionStart = data->BufTextLen;
  data->SelectionEnd = data->BufTextLen;
  data->BufDirty = true;
  return result;
}

} // namespace LX_core
