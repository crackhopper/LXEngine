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
  struct DisplayEntry final {
    CommandBus::HistoryEntry historyEntry;
    std::vector<std::string> attachments;
  };

  struct PendingSystemAttachment final {
    usize sourceHistoryIndex = 0;
    usize historySizeBeforeOwner = 0;
    std::vector<std::string> lines;
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
  void queueSystemLineAttachment(std::string_view line);
  void appendAttachmentToVisibleEntry(usize visibleIndex, std::string_view line) const;
  void removeTrailingAttachmentsFromVisibleEntry(usize visibleIndex,
                                                 const std::vector<std::string> &lines) const;
  void syncPendingSystemAttachments() const;
  void drawOutputRegion(float reservedInputHeight);
  void drawDisplayEntry(const DisplayEntry &entry) const;

  CommandBus &m_commandBus;
  ConsoleInputController m_inputController;
  mutable std::vector<std::string> m_orphanSystemLines;
  mutable std::vector<std::vector<std::string>> m_entryAttachments;
  mutable std::vector<PendingSystemAttachment> m_pendingSystemAttachments;
  usize m_displayStartIndex = 0;
  bool m_open = true;
  bool m_scrollToBottom = false;
};

} // namespace LX_core
