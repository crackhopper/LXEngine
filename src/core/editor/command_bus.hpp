#pragma once

#include "core/platform/types.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace LX_core {

inline constexpr std::string_view kCommandResultClearRedoOnSuccessMetadataKey =
    "clearRedoOnSuccess";
inline constexpr std::string_view kCommandResultClearUndoOnSuccessMetadataKey =
    "clearUndoOnSuccess";

struct CommandResult {
  bool ok = false;
  std::string message;
  std::string structured;
  std::unordered_map<std::string, std::string> metadata;
};

struct ParsedCommand {
  std::string verb;
  std::vector<std::string> args;
  std::string line;
};

struct CompletionContext {
  std::string partialToken;
  std::vector<std::string> precedingArgs;
};

struct CompletionResult {
  std::vector<std::string> candidates;
  std::string commonPrefix;
};

using CommandHandler =
    std::function<CommandResult(std::vector<std::string> args)>;
using CompletionProvider =
    std::function<std::vector<std::string>(const CompletionContext &)>;
using InverseFn = std::function<std::optional<std::string>(
    const ParsedCommand &executed, const CommandResult &result)>;

struct CommandMetadata {
  std::string brief;
  InverseFn inverse;
  bool mutatesState = false;
};

class CommandBus final {
public:
  struct HistoryEntry {
    std::string line;
    CommandResult result;
    u64 timestampMs = 0;
  };

  void registerHandler(std::string verb, std::string brief, CommandHandler handler);
  void registerHandler(std::string verb, CommandMetadata metadata,
                       CommandHandler handler);
  void registerCompleter(std::string verb, usize argIndex, CompletionProvider provider);
  void unregisterHandler(const std::string &verb);

  [[nodiscard]] CommandResult dispatch(const std::string &line);
  [[nodiscard]] std::vector<CommandResult>
  dispatchScript(std::string_view multiLineText);
  [[nodiscard]] CommandResult undo();
  [[nodiscard]] CommandResult redo();
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;

  [[nodiscard]] CompletionResult complete(std::string_view line) const;
  [[nodiscard]] std::vector<std::string> listVerbs() const;
  [[nodiscard]] std::string brief(const std::string &verb) const;
  [[nodiscard]] const std::vector<HistoryEntry> &history() const;

private:
  struct RegisteredCommand {
    CommandHandler handler;
    CommandMetadata metadata;
  };

  struct UndoEntry {
    std::string redoLine;
    std::string inverseLine;
  };

  struct DispatchOptions {
    bool recordHistory = true;
    bool trackUndo = true;
    bool clearUndoOnSuccess = true;
    bool clearRedoOnSuccess = true;
  };

  [[nodiscard]] static u64 currentTimestampMs();
  [[nodiscard]] static CommandResult makeParseError(std::string message);
  [[nodiscard]] static std::string commonPrefix(const std::vector<std::string> &values);

  [[nodiscard]] CommandResult dispatchInternal(const std::string &line,
                                              const DispatchOptions &options);
  [[nodiscard]] std::vector<CommandResult>
  dispatchScriptInternal(std::string_view multiLineText, const DispatchOptions &options);

  std::unordered_map<std::string, RegisteredCommand> m_commands;
  std::unordered_map<std::string, std::unordered_map<usize, CompletionProvider>>
      m_completers;
  std::vector<HistoryEntry> m_history;
  std::vector<UndoEntry> m_undoStack;
  std::vector<UndoEntry> m_redoStack;
};

} // namespace LX_core
