#include "display_launch_options.hpp"

#include <algorithm>
#include <charconv>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] bool parseNonNegativeIndex(std::string_view text, usize &index) {
  if (text.empty()) {
    return false;
  }

  usize parsed = 0;
  const char *begin = text.data();
  const char *end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return false;
  }

  index = parsed;
  return true;
}

} // namespace

DisplayLaunchParseResult
parseDisplayLaunchOptions(const std::vector<std::string> &args) {
  DisplayLaunchParseResult result;
  if (args.empty()) {
    return result;
  }

  result.remainingArgs.push_back(args.front());
  for (usize i = 1; i < args.size(); ++i) {
    const std::string &arg = args[i];
    if (arg == "--display-list") {
      result.options.listDisplays = true;
      continue;
    }
    if (arg == "--display") {
      if (i + 1 >= args.size()) {
        result.error = "missing value for --display";
        return result;
      }
      result.options.displaySelector = args[++i];
      continue;
    }
    result.remainingArgs.push_back(arg);
  }

  return result;
}

bool shouldLoadDisplayConfigDocument(const DisplayLaunchOptions &options) {
  return !options.listDisplays;
}

std::optional<usize> selectStartupDisplayIndex(
    const std::vector<LX_core::DisplayInfo> &displays,
    const std::optional<std::string> &displaySelector,
    std::string_view activeDisplayKey, std::string &errorMessage) {
  errorMessage.clear();
  if (displays.empty()) {
    errorMessage = "no displays available";
    return std::nullopt;
  }

  if (displaySelector.has_value()) {
    usize requestedIndex = 0;
    if (parseNonNegativeIndex(*displaySelector, requestedIndex)) {
      const auto byIndex =
          std::find_if(displays.begin(), displays.end(),
                       [requestedIndex](const LX_core::DisplayInfo &display) {
                         return static_cast<usize>(display.index) ==
                                requestedIndex;
                       });
      if (byIndex != displays.end()) {
        return static_cast<usize>(std::distance(displays.begin(), byIndex));
      }
    }

    const auto byKey =
        std::find_if(displays.begin(), displays.end(),
                     [&displaySelector](const LX_core::DisplayInfo &display) {
                       return display.key == *displaySelector;
                     });
    if (byKey != displays.end()) {
      return static_cast<usize>(std::distance(displays.begin(), byKey));
    }

    errorMessage = "invalid display selector '" + *displaySelector +
                   "'; run with --display-list to see available displays";
    return std::nullopt;
  }

  if (!activeDisplayKey.empty()) {
    const auto active =
        std::find_if(displays.begin(), displays.end(),
                     [activeDisplayKey](const LX_core::DisplayInfo &display) {
                       return display.key == activeDisplayKey;
                     });
    if (active != displays.end()) {
      return static_cast<usize>(std::distance(displays.begin(), active));
    }
  }

  return 0;
}

} // namespace LX_demo::lxe_editor
