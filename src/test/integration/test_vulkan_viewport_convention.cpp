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

void testViewportUsesNegativeHeightToPreserveOpenGlStyleNdcY() {
  setenv("LX_RENDER_FLIP_VIEWPORT_Y", "1", 1);

  const VkViewport viewport =
      LX_core::backend::makeVulkanViewport(1920u, 1008u);

  EXPECT(viewport.x == 0.0f, "viewport x should stay at the left edge");
  EXPECT(viewport.y == 1008.0f,
         "negative-height viewport should anchor y at the lower edge");
  EXPECT(viewport.width == 1920.0f, "viewport width should match input width");
  EXPECT(viewport.height == -1008.0f,
         "viewport height should be negative to flip clip-space y once");
  EXPECT(viewport.minDepth == 0.0f, "viewport minDepth should stay zero");
  EXPECT(viewport.maxDepth == 1.0f, "viewport maxDepth should stay one");

  unsetenv("LX_RENDER_FLIP_VIEWPORT_Y");
}

} // namespace

int main() {
  testViewportUsesNegativeHeightToPreserveOpenGlStyleNdcY();
  if (failures != 0) {
    return 1;
  }
  std::cout << "[PASS] vulkan viewport convention tests passed.\n";
  return 0;
}
