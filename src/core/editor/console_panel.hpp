#pragma once

#include "core/editor/command_bus.hpp"

#include <array>
#include <optional>
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

  [[nodiscard]] bool isOpen() const;
  void setOpen(bool open);

private:
  void setInputFromHistoryIndex(usize historyIndex);
  void markCommandDispatched();
  [[nodiscard]] static std::string trim(std::string_view text);
  [[nodiscard]] static std::string commonPrefix(const std::string &a,
                                                const std::string &b);

  CommandBus &m_commandBus;
  std::array<char, 512> m_inputBuffer{};
  usize m_displayStartIndex = 0;
  std::optional<usize> m_historyBrowseIndex;
  bool m_open = true;
  bool m_scrollToBottom = false;
};

} // namespace LX_core
