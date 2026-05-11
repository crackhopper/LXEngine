#include "core/editor/command_bus.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>

namespace LX_core {
namespace {

struct TokenizeResult {
  bool ok = true;
  std::vector<std::string> tokens;
  std::string errorMessage;
};

[[nodiscard]] bool isWhitespace(const char c) {
  return c == ' ' || c == '\t' || c == '\r';
}

[[nodiscard]] std::string trim(std::string_view text) {
  usize begin = 0;
  while (begin < text.size() && isWhitespace(text[begin])) {
    ++begin;
  }

  usize end = text.size();
  while (end > begin && isWhitespace(text[end - 1])) {
    --end;
  }

  return std::string(text.substr(begin, end - begin));
}

[[nodiscard]] TokenizeResult tokenizeCommandLine(const std::string &line) {
  TokenizeResult result;
  std::string current;
  bool inQuotes = false;
  bool escaping = false;

  for (const char c : line) {
    if (escaping) {
      switch (c) {
      case 'n':
        current.push_back('\n');
        break;
      case '\\':
        current.push_back('\\');
        break;
      case '"':
        current.push_back('"');
        break;
      default:
        current.push_back(c);
        break;
      }
      escaping = false;
      continue;
    }

    if (c == '\\') {
      escaping = true;
      continue;
    }

    if (c == '"') {
      inQuotes = !inQuotes;
      continue;
    }

    if (!inQuotes && isWhitespace(c)) {
      if (!current.empty()) {
        result.tokens.push_back(std::move(current));
        current.clear();
      }
      continue;
    }

    current.push_back(c);
  }

  if (escaping) {
    result.ok = false;
    result.errorMessage = "parse error: dangling escape";
    return result;
  }

  if (inQuotes) {
    result.ok = false;
    result.errorMessage = "parse error: unterminated quote";
    return result;
  }

  if (!current.empty()) {
    result.tokens.push_back(std::move(current));
  }

  return result;
}

[[nodiscard]] bool endsWithWhitespace(std::string_view text) {
  return !text.empty() && isWhitespace(text.back());
}

[[nodiscard]] bool parseMetadataBool(const std::string &value, bool fallback) {
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  return fallback;
}

} // namespace

void CommandBus::registerHandler(std::string verb, std::string brief,
                                 CommandHandler handler) {
  registerHandler(std::move(verb), CommandMetadata{std::move(brief), {}, false},
                  std::move(handler));
}

void CommandBus::registerHandler(std::string verb, CommandMetadata metadata,
                                 CommandHandler handler) {
  if (verb.empty()) {
    throw std::invalid_argument("CommandBus verb must not be empty");
  }
  if (!handler) {
    throw std::invalid_argument("CommandBus handler must not be empty");
  }

  m_commands[verb] = RegisteredCommand{std::move(handler), std::move(metadata)};
}

void CommandBus::registerCompleter(std::string verb, const usize argIndex,
                                   CompletionProvider provider) {
  if (verb.empty()) {
    throw std::invalid_argument("CommandBus completer verb must not be empty");
  }
  if (!provider) {
    throw std::invalid_argument("CommandBus completer must not be empty");
  }
  m_completers[std::move(verb)][argIndex] = std::move(provider);
}

void CommandBus::unregisterHandler(const std::string &verb) {
  m_commands.erase(verb);
  m_completers.erase(verb);
}

CommandResult CommandBus::dispatch(const std::string &line) {
  return dispatchInternal(line, DispatchOptions{});
}

std::vector<CommandResult>
CommandBus::dispatchScript(std::string_view multiLineText) {
  return dispatchScriptInternal(multiLineText, DispatchOptions{});
}

CommandResult CommandBus::undo() {
  if (m_undoStack.empty()) {
    return CommandResult{false, "nothing to undo", {}, {}};
  }

  const UndoEntry entry = m_undoStack.back();
  m_undoStack.pop_back();
  const std::vector<CommandResult> results =
      dispatchScriptInternal(entry.inverseLine,
                             DispatchOptions{false, false, false, false});
  if (results.empty()) {
    m_undoStack.push_back(entry);
    return CommandResult{false, "undo failed: empty inverse command", {}, {}};
  }
  for (const auto &result : results) {
    if (!result.ok) {
      m_undoStack.push_back(entry);
      return result;
    }
  }

  m_redoStack.push_back(entry);
  return CommandResult{true, "undid: " + entry.redoLine, {}, {}};
}

CommandResult CommandBus::redo() {
  if (m_redoStack.empty()) {
    return CommandResult{false, "nothing to redo", {}, {}};
  }

  const UndoEntry entry = m_redoStack.back();
  m_redoStack.pop_back();
  const std::vector<CommandResult> results =
      dispatchScriptInternal(entry.redoLine,
                             DispatchOptions{false, false, false, false});
  if (results.empty()) {
    m_redoStack.push_back(entry);
    return CommandResult{false, "redo failed: empty forward command", {}, {}};
  }
  for (const auto &result : results) {
    if (!result.ok) {
      m_redoStack.push_back(entry);
      return result;
    }
  }

  m_undoStack.push_back(entry);
  return CommandResult{true, "redid: " + entry.redoLine, {}, {}};
}

bool CommandBus::canUndo() const { return !m_undoStack.empty(); }

bool CommandBus::canRedo() const { return !m_redoStack.empty(); }

CompletionResult CommandBus::complete(std::string_view line) const {
  CompletionResult result;
  const std::string text(line);
  const TokenizeResult tokenized = tokenizeCommandLine(text);
  if (!tokenized.ok) {
    return result;
  }

  const bool trailingWhitespace = endsWithWhitespace(line);
  if (tokenized.tokens.empty()) {
    result.candidates = listVerbs();
    result.commonPrefix = commonPrefix(result.candidates);
    return result;
  }

  if (tokenized.tokens.size() == 1 && !trailingWhitespace) {
    const std::string &partialVerb = tokenized.tokens.front();
    for (const auto &verb : listVerbs()) {
      if (verb.rfind(partialVerb, 0) == 0) {
        result.candidates.push_back(verb);
      }
    }
    result.commonPrefix = commonPrefix(result.candidates);
    return result;
  }

  const std::string &verb = tokenized.tokens.front();
  const auto completerIt = m_completers.find(verb);
  if (completerIt == m_completers.end()) {
    return result;
  }

  CompletionContext context;
  usize argIndex = 0;
  if (trailingWhitespace) {
    argIndex = tokenized.tokens.size() - 1;
    for (usize i = 1; i < tokenized.tokens.size(); ++i) {
      context.precedingArgs.push_back(tokenized.tokens[i]);
    }
  } else {
    argIndex = tokenized.tokens.size() - 2;
    context.partialToken = tokenized.tokens.back();
    for (usize i = 1; i + 1 < tokenized.tokens.size(); ++i) {
      context.precedingArgs.push_back(tokenized.tokens[i]);
    }
  }

  const auto providerIt = completerIt->second.find(argIndex);
  if (providerIt == completerIt->second.end()) {
    return result;
  }

  result.candidates = providerIt->second(context);
  std::sort(result.candidates.begin(), result.candidates.end());
  result.candidates.erase(
      std::unique(result.candidates.begin(), result.candidates.end()),
      result.candidates.end());
  result.commonPrefix = commonPrefix(result.candidates);
  return result;
}

std::vector<std::string> CommandBus::listVerbs() const {
  std::vector<std::string> verbs;
  verbs.reserve(m_commands.size());
  for (const auto &entry : m_commands) {
    verbs.push_back(entry.first);
  }
  std::sort(verbs.begin(), verbs.end());
  return verbs;
}

std::string CommandBus::brief(const std::string &verb) const {
  const auto it = m_commands.find(verb);
  if (it == m_commands.end()) {
    return {};
  }
  return it->second.metadata.brief;
}

const std::vector<CommandBus::HistoryEntry> &CommandBus::history() const {
  return m_history;
}

u64 CommandBus::currentTimestampMs() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<u64>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

CommandResult CommandBus::makeParseError(std::string message) {
  return CommandResult{false, std::move(message), {}, {}};
}

std::string CommandBus::commonPrefix(const std::vector<std::string> &values) {
  if (values.empty()) {
    return {};
  }

  std::string prefix = values.front();
  for (usize i = 1; i < values.size(); ++i) {
    const usize limit = std::min(prefix.size(), values[i].size());
    usize j = 0;
    while (j < limit && prefix[j] == values[i][j]) {
      ++j;
    }
    prefix.resize(j);
    if (prefix.empty()) {
      break;
    }
  }
  return prefix;
}

CommandResult CommandBus::dispatchInternal(const std::string &line,
                                          const DispatchOptions &options) {
  const TokenizeResult tokenized = tokenizeCommandLine(line);
  CommandResult result;
  if (!tokenized.ok) {
    result = makeParseError(tokenized.errorMessage);
  } else if (tokenized.tokens.empty()) {
    result = CommandResult{false, "empty command", {}, {}};
  } else {
    ParsedCommand parsed;
    parsed.verb = tokenized.tokens.front();
    parsed.line = line;
    for (usize i = 1; i < tokenized.tokens.size(); ++i) {
      parsed.args.push_back(tokenized.tokens[i]);
    }

    const auto handlerIt = m_commands.find(parsed.verb);
    if (handlerIt == m_commands.end()) {
      result = CommandResult{false, "unknown command: " + parsed.verb, {}, {}};
    } else {
      try {
        result = handlerIt->second.handler(parsed.args);
      } catch (const std::exception &e) {
        result = CommandResult{false, std::string("exception: ") + e.what(), {}, {}};
      } catch (...) {
        result = CommandResult{false, "exception: unknown", {}, {}};
      }

      bool clearUndoOnSuccess = false;
      const auto clearUndoIt =
          result.metadata.find(std::string(kCommandResultClearUndoOnSuccessMetadataKey));
      if (clearUndoIt != result.metadata.end()) {
        clearUndoOnSuccess = parseMetadataBool(clearUndoIt->second, false);
      }

      bool clearRedoOnSuccess = handlerIt->second.metadata.mutatesState;
      const auto clearRedoIt =
          result.metadata.find(std::string(kCommandResultClearRedoOnSuccessMetadataKey));
      if (clearRedoIt != result.metadata.end()) {
        clearRedoOnSuccess =
            parseMetadataBool(clearRedoIt->second, clearRedoOnSuccess);
      }

      if (result.ok && clearUndoOnSuccess && options.clearUndoOnSuccess) {
        m_undoStack.clear();
      }
      if (result.ok && clearRedoOnSuccess && options.clearRedoOnSuccess) {
        m_redoStack.clear();
      }
      if (result.ok && options.trackUndo && handlerIt->second.metadata.inverse) {
        const std::optional<std::string> inverseLine =
            handlerIt->second.metadata.inverse(parsed, result);
        if (inverseLine.has_value() && !inverseLine->empty()) {
          std::string redoLine = parsed.line;
          const auto redoIt = result.metadata.find("redo.line");
          if (redoIt != result.metadata.end() && !redoIt->second.empty()) {
            redoLine = redoIt->second;
          }
          m_undoStack.push_back(UndoEntry{std::move(redoLine), *inverseLine});
        }
      }
    }
  }

  if (options.recordHistory) {
    m_history.push_back(HistoryEntry{line, result, currentTimestampMs()});
  }
  return result;
}

std::vector<CommandResult>
CommandBus::dispatchScriptInternal(std::string_view multiLineText,
                                   const DispatchOptions &options) {
  std::vector<CommandResult> results;
  usize lineStart = 0;

  while (lineStart <= multiLineText.size()) {
    const usize lineEnd = multiLineText.find('\n', lineStart);
    const usize rawEnd =
        lineEnd == std::string_view::npos ? multiLineText.size() : lineEnd;
    const std::string line = trim(multiLineText.substr(lineStart, rawEnd - lineStart));

    if (!line.empty() && line.front() != '#') {
      results.push_back(dispatchInternal(line, options));
    }

    if (lineEnd == std::string_view::npos) {
      break;
    }
    lineStart = lineEnd + 1;
  }

  return results;
}

} // namespace LX_core
