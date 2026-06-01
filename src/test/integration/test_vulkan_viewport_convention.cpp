#include "backend/vulkan/details/commands/command_buffer.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testViewportUsesPositiveHeightBecauseProjectionOwnsBackendYFlip() {
  setenv("LX_RENDER_FLIP_VIEWPORT_Y", "1", 1);

  const VkViewport viewport =
      LX_core::backend::makeVulkanViewport(1920u, 1008u);

  EXPECT(viewport.x == 0.0f, "viewport x should stay at the left edge");
  EXPECT(viewport.y == 0.0f, "viewport y should stay at the top edge");
  EXPECT(viewport.width == 1920.0f, "viewport width should match input width");
  EXPECT(
      viewport.height == 1008.0f,
      "viewport height should be positive because Vulkan projection flips y");
  EXPECT(viewport.minDepth == 0.0f, "viewport minDepth should stay zero");
  EXPECT(viewport.maxDepth == 1.0f, "viewport maxDepth should stay one");

  unsetenv("LX_RENDER_FLIP_VIEWPORT_Y");
}

} // namespace

int main() {
  testViewportUsesPositiveHeightBecauseProjectionOwnsBackendYFlip();
  if (failures != 0) {
    return 1;
  }
  std::cout << "[PASS] vulkan viewport convention tests passed.\n";
  return 0;
}
