#pragma once

#include "editor/commands/command_bus.hpp"

#include <string>
#include <string_view>

namespace LX_demo::lxe_editor {

[[nodiscard]] LX_core::CommandResult
makeEditorCommandOk(std::string message, std::string structured = {});
[[nodiscard]] LX_core::CommandResult
makeEditorCommandError(std::string message);
[[nodiscard]] std::string editorCommandJsonEscape(std::string_view text);

} // namespace LX_demo::lxe_editor
