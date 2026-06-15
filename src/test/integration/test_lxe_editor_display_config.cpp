#include "core/platform/window.hpp"
#include "editor/app/display_launch_options.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {
int failures = 0;

#define EXPECT(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg \
                << " (" #cond ")\n";                                          \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

LX_core::DisplayInfo makeTestDisplay(const int index, std::string key,
                                      std::string label) {
  return LX_core::DisplayInfo{
      .index = index,
      .backend = "test",
      .name = label,
      .bounds = {.x = index * 1000, .y = 0, .width = 1000, .height = 800},
      .usableBounds = {.x = index * 1000, .y = 0, .width = 1000, .height = 760},
      .contentScale = 1.0f,
      .key = std::move(key),
      .label = std::move(label),
  };
}

void testDisplayKeyAndLabelAreStableFallbacks() {
  LX_core::DisplayInfo display;
  display.index = 1;
  display.backend = "sdl";
  display.name = "DELL U2720Q";
  display.usableBounds = {.x = 1920, .y = 0, .width = 3840, .height = 2160};
  display.contentScale = 1.5f;

  LX_core::finalizeDisplayInfo(display);

  EXPECT(display.key == "sdl:1:DELL U2720Q:3840x2160:1.50",
         "display key should include backend, index, name, usable size, scale");
  EXPECT(display.label == "1: DELL U2720Q (3840x2160 @ 1.50x)",
         "display label should be human readable");
}

void testDisplayKeyAndLabelUseDisplayNameFallback() {
  LX_core::DisplayInfo display;
  display.index = 3;
  display.backend = "sdl";
  display.usableBounds = {.x = 0, .y = 0, .width = 800, .height = 600};
  display.contentScale = 1.0f;

  LX_core::finalizeDisplayInfo(display);

  EXPECT(display.key == "sdl:3:Display:800x600:1.00",
         "empty display name should fall back to Display in key");
  EXPECT(display.label == "3: Display (800x600 @ 1.00x)",
         "empty display name should fall back to Display in label");
}

void testDefaultPlacementUsesSelectedDisplayUsableBounds() {
  const LX_core::DisplayInfo display{
      .index = 2,
      .backend = "sdl",
      .name = "Side",
      .bounds = {.x = 1900, .y = 0, .width = 2560, .height = 1440},
      .usableBounds = {.x = 1920, .y = 40, .width = 2520, .height = 1360},
      .contentScale = 1.0f,
      .key = "sdl:2:Side:2520x1360:1.00",
      .label = "2: Side (2520x1360 @ 1.00x)",
  };

  const auto placement =
      LX_core::makeDefaultWindowPlacementForDisplay(display, 1280, 720);

  EXPECT(placement.width == 1280, "default placement should keep width");
  EXPECT(placement.height == 720, "default placement should keep height");
  EXPECT(placement.x == 2540, "default placement should center x");
  EXPECT(placement.y == 360, "default placement should center y");
}

void testDefaultPlacementClampsOversizeWindowToUsableBounds() {
  const LX_core::DisplayInfo display{
      .index = 0,
      .backend = "sdl",
      .name = "Primary",
      .usableBounds = {.x = 10, .y = 20, .width = 1024, .height = 768},
  };

  const auto placement =
      LX_core::makeDefaultWindowPlacementForDisplay(display, 4096, 2048);

  EXPECT(placement.x == 10, "oversize placement should start at usable x");
  EXPECT(placement.y == 20, "oversize placement should start at usable y");
  EXPECT(placement.width == 1024,
         "oversize placement should clamp to usable width");
  EXPECT(placement.height == 768,
         "oversize placement should clamp to usable height");
}

void testDefaultPlacementKeepsPositiveDimensionsForInvalidInputs() {
  const LX_core::DisplayInfo display{
      .index = 0,
      .backend = "sdl",
      .name = "Invalid",
      .usableBounds = {.x = 100, .y = 200, .width = 0, .height = -25},
  };

  const auto placement =
      LX_core::makeDefaultWindowPlacementForDisplay(display, 0, -50);

  EXPECT(placement.width == 1,
         "invalid requested width should produce positive width");
  EXPECT(placement.height == 1,
         "invalid requested height should produce positive height");
  EXPECT(placement.x == 100, "invalid usable width should use usable x");
  EXPECT(placement.y == 200, "invalid usable height should use usable y");
}

void testDisplaySelectionChoosesArgumentThenActiveThenFirst() {
  const std::vector<LX_core::DisplayInfo> displays{
      makeTestDisplay(0, "display-a", "Display A"),
      makeTestDisplay(1, "display-b", "Display B"),
  };

  std::string error;
  const auto explicitKey =
      LX_demo::lxe_editor::selectStartupDisplayIndex(displays, "display-b",
                                                     "display-a", error);
  EXPECT(explicitKey.has_value(), "explicit display key should select display");
  EXPECT(*explicitKey == 1, "explicit display key should select second display");
  EXPECT(error.empty(), "valid explicit display key should not set error");

  const auto explicitIndex =
      LX_demo::lxe_editor::selectStartupDisplayIndex(displays, "0", "display-b",
                                                     error);
  EXPECT(explicitIndex.has_value(),
         "explicit display index should select display");
  EXPECT(*explicitIndex == 0,
         "explicit display index should select first display");
  EXPECT(error.empty(), "valid explicit display index should not set error");

  const auto active =
      LX_demo::lxe_editor::selectStartupDisplayIndex(displays, std::nullopt,
                                                     "display-b", error);
  EXPECT(active.has_value(), "active display should select display");
  EXPECT(*active == 1, "active display should select second display");
  EXPECT(error.empty(), "valid active display should not set error");

  const auto fallback =
      LX_demo::lxe_editor::selectStartupDisplayIndex(displays, std::nullopt,
                                                     "missing-display", error);
  EXPECT(fallback.has_value(),
         "missing active display should fall back to first display");
  EXPECT(*fallback == 0, "missing active display should select first display");
  EXPECT(error.empty(), "fallback should not set error");
}

void testDisplayLaunchParsingSetsListFlag() {
  const auto parsed = LX_demo::lxe_editor::parseDisplayLaunchOptions(
      {"lxe_editor", "--display-list"});

  EXPECT(!parsed.error.has_value(), "--display-list should parse");
  EXPECT(parsed.options.listDisplays, "--display-list should set list flag");
  EXPECT(!parsed.options.displaySelector.has_value(),
         "--display-list should not set selector");
  EXPECT(parsed.remainingArgs.size() == 1,
         "--display-list should be removed before API parsing");
  EXPECT(!LX_demo::lxe_editor::shouldLoadDisplayConfigDocument(parsed.options),
         "--display-list should not require display config load");
}

void testDisplayLaunchParsingSetsDisplaySelector() {
  const auto parsed = LX_demo::lxe_editor::parseDisplayLaunchOptions(
      {"lxe_editor", "--display", "0"});

  EXPECT(!parsed.error.has_value(), "--display value should parse");
  EXPECT(parsed.options.displaySelector.has_value(),
         "--display should set selector");
  EXPECT(*parsed.options.displaySelector == "0",
         "--display should preserve selector value");
  EXPECT(parsed.remainingArgs.size() == 1,
         "--display should be removed before API parsing");
}

void testDisplayLaunchParsingRequiresDisplayValue() {
  const auto parsed = LX_demo::lxe_editor::parseDisplayLaunchOptions(
      {"lxe_editor", "--display"});

  EXPECT(parsed.error.has_value(), "missing --display value should error");
  EXPECT(*parsed.error == "missing value for --display",
         "missing --display value should report specific error");
}

void testDisplayLaunchParsingPreservesApiArguments() {
  const auto parsed = LX_demo::lxe_editor::parseDisplayLaunchOptions(
      {"lxe_editor", "--api-disable", "--api-host", "127.0.0.1",
       "--api-port", "4000"});

  EXPECT(!parsed.error.has_value(), "API arguments should not be display errors");
  EXPECT(parsed.remainingArgs.size() == 6,
         "API arguments should be preserved for API parser");
  EXPECT(parsed.remainingArgs[1] == "--api-disable",
         "API disable flag should be preserved");
  EXPECT(parsed.remainingArgs[2] == "--api-host",
         "API host flag should be preserved");
  EXPECT(parsed.remainingArgs[3] == "127.0.0.1",
         "API host value should be preserved");
  EXPECT(parsed.remainingArgs[4] == "--api-port",
         "API port flag should be preserved");
  EXPECT(parsed.remainingArgs[5] == "4000",
         "API port value should be preserved");
}

} // namespace

int main() {
  testDisplayKeyAndLabelAreStableFallbacks();
  testDisplayKeyAndLabelUseDisplayNameFallback();
  testDefaultPlacementUsesSelectedDisplayUsableBounds();
  testDefaultPlacementClampsOversizeWindowToUsableBounds();
  testDefaultPlacementKeepsPositiveDimensionsForInvalidInputs();
  testDisplaySelectionChoosesArgumentThenActiveThenFirst();
  testDisplayLaunchParsingSetsListFlag();
  testDisplayLaunchParsingSetsDisplaySelector();
  testDisplayLaunchParsingRequiresDisplayValue();
  testDisplayLaunchParsingPreservesApiArguments();

  if (failures != 0) {
    std::cerr << failures << " display config test(s) failed\n";
    return 1;
  }

  return 0;
}
