#include "demos/lxe_editor/commands/lxe_editor_command_helpers.hpp"

#include <utility>

namespace LX_demo::lxe_editor {

LX_core::CommandResult makeEditorCommandOk(std::string message,
                                           std::string structured) {
  return LX_core::CommandResult{true, std::move(message),
                                std::move(structured)};
}

LX_core::CommandResult makeEditorCommandError(std::string message) {
  return LX_core::CommandResult{false, std::move(message), {}};
}

std::string editorCommandJsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

} // namespace LX_demo::lxe_editor
