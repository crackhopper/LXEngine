#include "editor/commands/lxe_editor_command_helpers.hpp"

#include <iomanip>
#include <sstream>
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
  std::ostringstream escapedControl;
  for (const unsigned char c : text) {
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
      if (c < 0x20u) {
        escapedControl.str({});
        escapedControl.clear();
        escapedControl << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(c);
        out += escapedControl.str();
      } else {
        out.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  return out;
}

} // namespace LX_demo::lxe_editor
