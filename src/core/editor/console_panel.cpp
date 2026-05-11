#include "core/editor/console_panel.hpp"

#include <algorithm>
#include <cfloat>
#include <cstring>

#include <imgui.h>

namespace LX_core {

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
  if (ImGui::BeginChild("console_output", ImVec2(0.0f, -inputHeight), true)) {
    std::string output = displayedText();
    output.push_back('\0');
    ImGui::PushItemWidth(-FLT_MIN);
    ImGui::InputTextMultiline("##console_output_text", output.data(),
                              output.size(), ImVec2(-FLT_MIN, -FLT_MIN),
                              ImGuiInputTextFlags_ReadOnly);
    ImGui::PopItemWidth();

    if (m_scrollToBottom) {
      ImGui::SetScrollHereY(1.0f);
      m_scrollToBottom = false;
    }
  }
  ImGui::EndChild();

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

void ConsolePanel::setInputText(std::string_view text) { m_inputController.setInputText(text); }

std::string ConsolePanel::getInputText() const { return m_inputController.inputText(); }

std::vector<CommandBus::HistoryEntry> ConsolePanel::displayedEntries() const {
  const auto &history = m_commandBus.history();
  if (m_displayStartIndex >= history.size()) {
    return {};
  }
  return std::vector<CommandBus::HistoryEntry>(history.begin() + m_displayStartIndex,
                                               history.end());
}

std::string ConsolePanel::displayedText() const {
  std::string output;
  const auto entries = displayedEntries();
  for (usize i = 0; i < entries.size(); ++i) {
    output += "< ";
    output += entries[i].line;
    output += '\n';
    output += "> ";
    output += entries[i].result.message;
    if (i + 1 < entries.size()) {
      output += "\n\n";
    }
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
