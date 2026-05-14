#include "core/platform/window.hpp"
#include "infra/window/window.hpp"

#include <iostream>
#include <string>

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

void testWindowCanEnumerateDisplays() {
  const auto displays = LX_infra::Window::enumerateDisplays();

  EXPECT(!displays.empty(), "window backend should enumerate at least one display");
  for (const auto& display : displays) {
    EXPECT(!display.key.empty(), "display key should be populated");
    EXPECT(!display.label.empty(), "display label should be populated");
  }
}
} // namespace

int main() {
  testDisplayKeyAndLabelAreStableFallbacks();
  testDisplayKeyAndLabelUseDisplayNameFallback();
  testDefaultPlacementUsesSelectedDisplayUsableBounds();
  testDefaultPlacementClampsOversizeWindowToUsableBounds();
  testDefaultPlacementKeepsPositiveDimensionsForInvalidInputs();
  testWindowCanEnumerateDisplays();

  if (failures != 0) {
    std::cerr << failures << " display config test(s) failed\n";
    return 1;
  }

  return 0;
}
