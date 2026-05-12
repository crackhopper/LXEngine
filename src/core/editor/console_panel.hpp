#pragma once

#include "core/editor/command_bus.hpp"
#include "core/editor/console_input_controller.hpp"

#include <imgui.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace LX_core {

class ConsolePanel final {
public:
  struct DisplayEntry final {
    CommandBus::HistoryEntry historyEntry;
    std::vector<std::string> attachments;
  };

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
  void setPersistedHistory(std::vector<std::string> historyLines);
  void recordPersistedHistoryLine(std::string_view line);
  void appendSystemLine(std::string_view line);
  [[nodiscard]] std::vector<std::string> persistedHistory() const;
  [[nodiscard]] bool consumePersistedHistoryDirty();

  void setInputText(std::string_view text);
  [[nodiscard]] std::string getInputText() const;
  [[nodiscard]] std::vector<CommandBus::HistoryEntry> displayedEntries() const;
  [[nodiscard]] std::vector<DisplayEntry> displayedDisplayEntries() const;
  [[nodiscard]] std::string displayedText() const;

  [[nodiscard]] bool isOpen() const;
  void setOpen(bool open);

private:
  [[nodiscard]] static int inputTextCallback(ImGuiInputTextCallbackData *data);
  void appendAttachmentToDispatchOwner(u64 dispatchOwnerId, std::string_view line);
  void drawOutputRegion(float reservedInputHeight);
  void drawDisplayEntry(const DisplayEntry &entry) const;

  CommandBus &m_commandBus;
  ConsoleInputController m_inputController;
  std::vector<std::string> m_orphanSystemLines;
  std::unordered_map<u64, std::vector<std::string>> m_dispatchAttachments;
  usize m_displayStartIndex = 0;
  bool m_open = true;
  bool m_scrollToBottom = false;
};

} // namespace LX_core
