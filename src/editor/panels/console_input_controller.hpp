#pragma once

#include "editor/commands/command_bus.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiInputTextCallbackData;

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
  void setPersistedHistory(std::vector<std::string> historyLines);
  void recordPersistedHistoryLine(std::string line);
  [[nodiscard]] std::vector<std::string> persistedHistory() const;
  [[nodiscard]] bool consumePersistedHistoryDirty();

  void setInputText(std::string_view text);
  [[nodiscard]] std::string inputText() const;
  [[nodiscard]] char *inputBufferData();
  [[nodiscard]] usize inputBufferSize() const;

  [[nodiscard]] std::string helperOutputText() const;
  void clearHelperOutput();
  [[nodiscard]] std::string sanitizedInputText() const;
  void syncCallbackBuffer(ImGuiInputTextCallbackData &data) const;

  [[nodiscard]] int handleCallbackEvent(int eventFlag, int eventKey,
                                        unsigned int eventChar);

private:
  void beginHistoryBrowseIfNeeded();
  void setInputFromHistoryIndex(usize historyIndex);
  void markCommandDispatched();
  void appendPersistedHistoryLine(std::string line);
  void appendHelperLine(std::string line);
  [[nodiscard]] static std::string sanitizeSubmittedLine(std::string_view text);
  [[nodiscard]] static std::string trim(std::string_view text);
  [[nodiscard]] static std::string commonPrefix(const std::string &a,
                                                const std::string &b);

  CommandBus &m_commandBus;
  std::array<char, 512> m_inputBuffer{};
  std::optional<usize> m_historyBrowseIndex;
  std::optional<std::string> m_historyBrowseDraft;
  std::vector<std::string> m_helperLines;
  std::vector<std::string> m_persistedHistory;
  bool m_persistedHistoryDirty = false;
};

} // namespace LX_core
