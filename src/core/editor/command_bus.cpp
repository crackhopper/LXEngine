#include "core/editor/command_bus.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <sstream>
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

} // namespace

void CommandBus::registerHandler(std::string verb, std::string brief,
                                 CommandHandler handler) {
  if (verb.empty()) {
    throw std::invalid_argument("CommandBus verb must not be empty");
  }
  if (!handler) {
    throw std::invalid_argument("CommandBus handler must not be empty");
  }

  m_handlers[verb] = std::move(handler);
  m_briefs[verb] = std::move(brief);
}

void CommandBus::unregisterHandler(const std::string &verb) {
  m_handlers.erase(verb);
  m_briefs.erase(verb);
}

CommandResult CommandBus::dispatch(const std::string &line) {
  const TokenizeResult tokenized = tokenizeCommandLine(line);
  CommandResult result;
  if (!tokenized.ok) {
    result = makeParseError(tokenized.errorMessage);
  } else {
    result = dispatchTokens(tokenized.tokens, m_handlers);
  }

  m_history.push_back(HistoryEntry{line, result, currentTimestampMs()});
  return result;
}

std::vector<CommandResult>
CommandBus::dispatchScript(std::string_view multiLineText) {
  std::vector<CommandResult> results;
  usize lineStart = 0;

  while (lineStart <= multiLineText.size()) {
    const usize lineEnd = multiLineText.find('\n', lineStart);
    const usize rawEnd =
        lineEnd == std::string_view::npos ? multiLineText.size() : lineEnd;
    const std::string line = trim(multiLineText.substr(lineStart, rawEnd - lineStart));

    if (!line.empty() && line.front() != '#') {
      results.push_back(dispatch(line));
    }

    if (lineEnd == std::string_view::npos) {
      break;
    }
    lineStart = lineEnd + 1;
  }

  return results;
}

std::vector<std::string> CommandBus::listVerbs() const {
  std::vector<std::string> verbs;
  verbs.reserve(m_handlers.size());
  for (const auto &entry : m_handlers) {
    verbs.push_back(entry.first);
  }
  std::sort(verbs.begin(), verbs.end());
  return verbs;
}

std::string CommandBus::brief(const std::string &verb) const {
  const auto it = m_briefs.find(verb);
  if (it == m_briefs.end()) {
    return {};
  }
  return it->second;
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
  return CommandResult{false, std::move(message), {}};
}

CommandResult CommandBus::dispatchTokens(
    const std::vector<std::string> &tokens,
    const std::unordered_map<std::string, CommandHandler> &handlers) {
  if (tokens.empty()) {
    return CommandResult{false, "empty command", {}};
  }

  const auto handlerIt = handlers.find(tokens.front());
  if (handlerIt == handlers.end()) {
    return CommandResult{false, "unknown command: " + tokens.front(), {}};
  }

  std::vector<std::string> args;
  args.reserve(tokens.size() > 0 ? tokens.size() - 1 : 0);
  for (usize i = 1; i < tokens.size(); ++i) {
    args.push_back(tokens[i]);
  }

  try {
    return handlerIt->second(std::move(args));
  } catch (const std::exception &e) {
    return CommandResult{false, std::string("exception: ") + e.what(), {}};
  } catch (...) {
    return CommandResult{false, "exception: unknown", {}};
  }
}

} // namespace LX_core
