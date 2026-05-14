#pragma once
#include "core/input/input_state.hpp"
#include "core/platform/types.hpp"
#include <algorithm>
#include <functional>
#include <iomanip>
#include <locale>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
namespace LX_core {
using WindowGraphicsHandle = void *;
using GraphicsInstanceHandle = void *;  

struct WindowPlacement final {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool maximized = false;
};

struct WindowUsableBounds final {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct DisplayInfo final {
  int index = 0;
  std::string backend;
  std::string name;
  WindowUsableBounds bounds;
  WindowUsableBounds usableBounds;
  float contentScale = 1.0f;
  std::string key;
  std::string label;
};

[[nodiscard]] inline std::string formatDisplayScale(float scale) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::fixed << std::setprecision(2) << scale;
  return stream.str();
}

inline void finalizeDisplayInfo(DisplayInfo& display) {
  const std::string displayName =
      display.name.empty() ? "Display" : display.name;
  const std::string scale = formatDisplayScale(display.contentScale);

  display.key = display.backend + ":" + std::to_string(display.index) + ":" +
                displayName + ":" + std::to_string(display.usableBounds.width) +
                "x" + std::to_string(display.usableBounds.height) + ":" + scale;
  display.label = std::to_string(display.index) + ": " + displayName + " (" +
                  std::to_string(display.usableBounds.width) + "x" +
                  std::to_string(display.usableBounds.height) + " @ " + scale +
                  "x)";
}

[[nodiscard]] inline WindowPlacement
makeDefaultWindowPlacementForDisplay(const DisplayInfo& display, int width,
                                     int height) {
  const WindowUsableBounds& bounds = display.usableBounds;
  const int usableWidth = std::max(1, bounds.width);
  const int usableHeight = std::max(1, bounds.height);
  const int requestedWidth = std::max(1, width);
  const int requestedHeight = std::max(1, height);
  const int clampedWidth = std::min(requestedWidth, usableWidth);
  const int clampedHeight = std::min(requestedHeight, usableHeight);

  return WindowPlacement{
      .x = bounds.x + (usableWidth - clampedWidth) / 2,
      .y = bounds.y + (usableHeight - clampedHeight) / 2,
      .width = clampedWidth,
      .height = clampedHeight,
      .maximized = false,
  };
}

[[nodiscard]] inline long long
windowPlacementCenterX(const WindowPlacement& placement) {
  return static_cast<long long>(placement.x) +
         static_cast<long long>(placement.width) / 2LL;
}

[[nodiscard]] inline long long
windowPlacementCenterY(const WindowPlacement& placement) {
  return static_cast<long long>(placement.y) +
         static_cast<long long>(placement.height) / 2LL;
}

[[nodiscard]] inline std::optional<WindowPlacement>
sanitizeWindowPlacement(const WindowPlacement& placement,
                        const WindowUsableBounds& bounds) {
  if (placement.width <= 0 || placement.height <= 0 || bounds.width <= 0 ||
      bounds.height <= 0) {
    return std::nullopt;
  }

  WindowPlacement sanitized = placement;
  sanitized.width = std::min(placement.width, bounds.width);
  sanitized.height = std::min(placement.height, bounds.height);

  const int maxX = bounds.x + bounds.width - sanitized.width;
  const int maxY = bounds.y + bounds.height - sanitized.height;
  sanitized.x = std::clamp(placement.x, bounds.x, maxX);
  sanitized.y = std::clamp(placement.y, bounds.y, maxY);
  return sanitized;
}

class Window {
public:
  static void Initialize(); // 初始化窗口系统

protected:
  Window() = default;
  ~Window() = default;

public:
  virtual int getWidth() const = 0;
  virtual int getHeight() const = 0;
  virtual void updateSize(bool* closed, int *width, int *height) = 0;
  virtual void getRequiredExtensions(std::vector<const char *> &extensions) const = 0;

  /**
   * @brief 为特定的图形 API 准备渲染表面/句柄
   * @param api 指定目标 API (Vulkan, DX12, etc.)
   * @param graphicsInstance 对于 Vulkan，这里需要传入 VkInstance 的指针或句柄
   * @return 返回创建好的句柄（对于 Vulkan 则是 VkSurfaceKHR）
   */
  virtual WindowGraphicsHandle createGraphicsHandle(GraphicsAPI api,
                                     GraphicsInstanceHandle instance) const = 0;

  // 辅助销毁方法（因为 Surface 必须在 Instance 销毁前销毁）
  virtual void destroyGraphicsHandle(GraphicsAPI api, GraphicsInstanceHandle instance,
                                     WindowGraphicsHandle handle) const = 0;

  virtual InputStateSharedPtr getInputState() const = 0;

  virtual WindowPlacement getPlacement() const = 0;
  virtual WindowUsableBounds getUsableBounds() const = 0;
  virtual WindowUsableBounds
  getUsableBoundsForPlacement(const WindowPlacement& placement) const {
    (void)placement;
    return getUsableBounds();
  }
  virtual void applyPlacement(const WindowPlacement& placement) = 0;

  /**
   * @brief Returns the underlying native window handle as an opaque pointer.
   *
   * SDL backend returns `SDL_Window*`, GLFW backend returns `GLFWwindow*`.
   * The core layer intentionally keeps this as `void*` so it does not need to
   * depend on SDL/GLFW headers. Callers must know which backend is in use
   * before casting.
   */
  virtual void* getNativeHandle() const = 0;

  virtual void onClose(std::function<bool()> cb) = 0;
  virtual bool shouldClose() = 0;
};
using WindowSharedPtr = std::shared_ptr<Window>;
} // namespace LX_core
