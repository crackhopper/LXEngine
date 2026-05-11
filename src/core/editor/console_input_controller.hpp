#pragma once

#include "core/editor/command_bus.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_core {

class ConsoleInputController final {
public:
  explicit ConsoleInputController(CommandBus &commandBus);

  void submitLine(std::string_view line);
  void submitCurrentInput();
  void autocomplete();
  void browseHistoryOlder();
  void browseHistoryNewer();
  void cancelHistoryBrowse();
  void dispatchUndo();
  void dispatchRedo();

  void setInputText(std::string_view text);
  [[nodiscard]] std::string inputText() const;
  [[nodiscard]] char *inputBufferData();
  [[nodiscard]] usize inputBufferSize() const;

  [[nodiscard]] std::string helperOutputText() const;
  void clearHelperOutput();

  [[nodiscard]] int handleCallbackEvent(int eventFlag, int eventKey);

private:
  void beginHistoryBrowseIfNeeded();
  void setInputFromHistoryIndex(usize historyIndex);
  void markCommandDispatched();
  void appendHelperLine(std::string line);
  [[nodiscard]] static std::string trim(std::string_view text);
  [[nodiscard]] static std::string commonPrefix(const std::string &a,
                                                const std::string &b);

  CommandBus &m_commandBus;
  std::array<char, 512> m_inputBuffer{};
  std::optional<usize> m_historyBrowseIndex;
  std::optional<std::string> m_historyBrowseDraft;
  std::vector<std::string> m_helperLines;
};

} // namespace LX_core
