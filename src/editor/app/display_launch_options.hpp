#pragma once

#include "core/platform/window.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_demo::lxe_editor {

struct DisplayLaunchOptions final {
  bool listDisplays = false;
  std::optional<std::string> displaySelector;
};

struct DisplayLaunchParseResult final {
  DisplayLaunchOptions options;
  std::vector<std::string> remainingArgs;
  std::optional<std::string> error;
};

[[nodiscard]] DisplayLaunchParseResult
parseDisplayLaunchOptions(const std::vector<std::string> &args);

[[nodiscard]] bool
shouldLoadDisplayConfigDocument(const DisplayLaunchOptions &options);

[[nodiscard]] std::optional<usize> selectStartupDisplayIndex(
    const std::vector<LX_core::DisplayInfo> &displays,
    const std::optional<std::string> &displaySelector,
    std::string_view activeDisplayKey, std::string &errorMessage);

} // namespace LX_demo::lxe_editor
