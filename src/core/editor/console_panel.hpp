#pragma once

#include "core/editor/command_bus.hpp"
#include "core/editor/console_input_controller.hpp"

#include <imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace LX_core {

class ConsolePanel final {
public:
  explicit ConsolePanel(CommandBus &commandBus);

  void draw();

  void submitLine(std::string_view line);
  void submitCurrentInput();
  void clearDisplay();
  void browseHistoryOlder();
  void browseHistoryNewer();
  void autocompleteInput();
  void dispatchUndo();
  void dispatchRedo();

  void setInputText(std::string_view text);
  [[nodiscard]] std::string getInputText() const;
  [[nodiscard]] std::vector<CommandBus::HistoryEntry> displayedEntries() const;
  [[nodiscard]] std::string displayedText() const;

  [[nodiscard]] bool isOpen() const;
  void setOpen(bool open);

private:
  [[nodiscard]] static int inputTextCallback(ImGuiInputTextCallbackData *data);

  CommandBus &m_commandBus;
  ConsoleInputController m_inputController;
  usize m_displayStartIndex = 0;
  bool m_open = true;
  bool m_scrollToBottom = false;
};

} // namespace LX_core
