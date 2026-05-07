#pragma once

#include "core/platform/types.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace LX_core {

struct CommandResult {
  bool ok = false;
  std::string message;
  std::string structured;
};

using CommandHandler =
    std::function<CommandResult(std::vector<std::string> args)>;

class CommandBus final {
public:
  struct HistoryEntry {
    std::string line;
    CommandResult result;
    u64 timestampMs = 0;
  };

  void registerHandler(std::string verb, std::string brief, CommandHandler handler);
  void unregisterHandler(const std::string &verb);

  [[nodiscard]] CommandResult dispatch(const std::string &line);
  [[nodiscard]] std::vector<CommandResult>
  dispatchScript(std::string_view multiLineText);

  [[nodiscard]] std::vector<std::string> listVerbs() const;
  [[nodiscard]] std::string brief(const std::string &verb) const;
  [[nodiscard]] const std::vector<HistoryEntry> &history() const;

private:
  [[nodiscard]] static u64 currentTimestampMs();
  [[nodiscard]] static CommandResult makeParseError(std::string message);
  [[nodiscard]] static CommandResult dispatchTokens(
      const std::vector<std::string> &tokens,
      const std::unordered_map<std::string, CommandHandler> &handlers);

  std::unordered_map<std::string, CommandHandler> m_handlers;
  std::unordered_map<std::string, std::string> m_briefs;
  std::vector<HistoryEntry> m_history;
};

} // namespace LX_core
