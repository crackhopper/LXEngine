#include "editor/commands/register_lxe_editor_commands.hpp"

#include "editor/commands/lxe_editor_command_helpers.hpp"

#include <exception>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::string joinArgs(const std::vector<std::string> &args,
                                   const usize first) {
  std::string out;
  for (usize i = first; i < args.size(); ++i) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out += args[i];
  }
  return out;
}

[[nodiscard]] LX_core::CommandResult
makeDisplayResult(std::string successMessage, std::string structured) {
  if (structured.find("\"ok\":false") != std::string::npos) {
    return LX_core::CommandResult{false, "display command failed",
                                  std::move(structured)};
  }
  return makeEditorCommandOk(std::move(successMessage), std::move(structured));
}

[[nodiscard]] LX_core::CommandResult
makeDisplayHookError(const std::exception &error) {
  return makeEditorCommandError(std::string("display error: ") + error.what());
}

} // namespace

void registerDisplayCommands(LX_core::CommandBus &bus,
                             const LxeEditorCommandContext &context) {
  auto displayListJson = context.displayListJson;
  auto displayActiveJson = context.displayActiveJson;
  auto displayConfigGetJson = context.displayConfigGetJson;
  auto displayConfigSet = context.displayConfigSet;
  auto displaySelect = context.displaySelect;
  auto displayNext = context.displayNext;

  bus.registerHandler(
      "display",
      "display list|active|config get <key|active|default>|config set "
      "<key|default> <json-or-yaml-patch>|select <key>|next",
      [displayListJson, displayActiveJson, displayConfigGetJson,
       displayConfigSet, displaySelect,
       displayNext](std::vector<std::string> args) {
        if (args.size() == 1 && args[0] == "list") {
          if (!displayListJson) {
            return makeEditorCommandError("display list unavailable");
          }
          try {
            std::string structured = displayListJson();
            std::string message = structured;
            return makeDisplayResult(std::move(message), std::move(structured));
          } catch (const std::exception &error) {
            return makeDisplayHookError(error);
          }
        }
        if (args.size() == 1 && args[0] == "active") {
          if (!displayActiveJson) {
            return makeEditorCommandError("display active unavailable");
          }
          try {
            std::string structured = displayActiveJson();
            std::string message = structured;
            return makeDisplayResult(std::move(message), std::move(structured));
          } catch (const std::exception &error) {
            return makeDisplayHookError(error);
          }
        }
        if (args.size() == 3 && args[0] == "config" && args[1] == "get") {
          if (!displayConfigGetJson) {
            return makeEditorCommandError("display config get unavailable");
          }
          try {
            std::string structured = displayConfigGetJson(args[2]);
            std::string message = structured;
            return makeDisplayResult(std::move(message), std::move(structured));
          } catch (const std::exception &error) {
            return makeDisplayHookError(error);
          }
        }
        if (args.size() >= 4 && args[0] == "config" && args[1] == "set") {
          if (!displayConfigSet) {
            return makeEditorCommandError("display config set unavailable");
          }
          try {
            const std::string patch = joinArgs(args, 3);
            std::string structured = displayConfigSet(args[2], patch);
            return makeDisplayResult("display config saved",
                                     std::move(structured));
          } catch (const std::exception &error) {
            return makeDisplayHookError(error);
          }
        }
        if (args.size() == 2 && args[0] == "select") {
          if (!displaySelect) {
            return makeEditorCommandError("display select unavailable");
          }
          try {
            std::string structured = displaySelect(args[1]);
            return makeDisplayResult("display selected; restart required",
                                     std::move(structured));
          } catch (const std::exception &error) {
            return makeDisplayHookError(error);
          }
        }
        if (args.size() == 1 && args[0] == "next") {
          if (!displayNext) {
            return makeEditorCommandError("display next unavailable");
          }
          try {
            std::string structured = displayNext();
            return makeDisplayResult("display switched", std::move(structured));
          } catch (const std::exception &error) {
            return makeDisplayHookError(error);
          }
        }
        return makeEditorCommandError(
            "usage: display list|active|config get <key|active|default>|config "
            "set <key|default> <json-or-yaml-patch>|select <key>|next");
      });

  bus.registerCompleter("display", 0,
                        [](const LX_core::CompletionContext &context) {
                          static const std::vector<std::string> kActions = {
                              "list", "active", "config", "select", "next"};
                          std::vector<std::string> out;
                          for (const auto &action : kActions) {
                            if (action.rfind(context.partialToken, 0) == 0) {
                              out.push_back(action);
                            }
                          }
                          return out;
                        });
}

} // namespace LX_demo::lxe_editor
