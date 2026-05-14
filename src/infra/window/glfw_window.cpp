#ifdef USE_GLFW
#include "window.hpp"
#include "core/input/dummy_input_state.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>

namespace LX_infra {

namespace {

[[nodiscard]] long long squaredDistanceToRect(const int rectX, const int rectY,
                                              const int rectWidth,
                                              const int rectHeight,
                                              const long long x,
                                              const long long y) {
  const long long rectMinX = static_cast<long long>(rectX);
  const long long rectMinY = static_cast<long long>(rectY);
  const long long rectMaxX =
      rectMinX + static_cast<long long>(rectWidth) - 1LL;
  const long long rectMaxY =
      rectMinY + static_cast<long long>(rectHeight) - 1LL;
  const long long clampedX = std::clamp(x, rectMinX, rectMaxX);
  const long long clampedY = std::clamp(y, rectMinY, rectMaxY);
  const long long dx = x - clampedX;
  const long long dy = y - clampedY;
  return dx * dx + dy * dy;
}

[[nodiscard]] std::optional<LX_core::WindowPlacement>
resolveInitialPlacement(const int width, const int height,
                        const WindowCreateOptions& options) {
  if (options.initialPlacement.has_value()) {
    return options.initialPlacement;
  }

  if (!options.displayKey.has_value()) {
    return std::nullopt;
  }

  for (const auto& display : Window::enumerateDisplays()) {
    if (display.key == *options.displayKey) {
      return LX_core::makeDefaultWindowPlacementForDisplay(display, width,
                                                           height);
    }
  }

  return std::nullopt;
}

} // namespace

struct Window::Impl {
  int width;
  int height;
  const char *title;
  GLFWwindow *window = nullptr;
  std::function<bool()> closeCallback;

  Impl(const char *t, int w, int h,
       const std::optional<LX_core::WindowPlacement>& initialPlacement)
      : width(w), height(h), title(t) {
    if (!glfwInit())
      throw std::runtime_error("GLFW init failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
      throw std::runtime_error("GLFW create window failed");
    if (initialPlacement.has_value()) {
      applyPlacement(*initialPlacement);
    }
    glfwShowWindow(window);
  }

  ~Impl() {
    if (window)
      glfwDestroyWindow(window);
    glfwTerminate();
  }

  bool shouldClose() const {
    glfwPollEvents();
    return glfwWindowShouldClose(window);
  }

  VkSurfaceKHR getVulkanSurface(VkInstance instance) const {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) !=
        VK_SUCCESS)
      throw std::runtime_error("Failed to create Vulkan surface");
    return surface;
  }

  void getRequiredExtensions(std::vector<const char *> &extensions) const {
    u32 glfwExtensionCount = 0;
    const char **glfwExtensions;

    // 获取 GLFW 运行 Vulkan 所需的扩展列表（如 VK_KHR_surface 等）
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    if (glfwExtensions == nullptr) {
      throw std::runtime_error(
          "GLFW could not find required Vulkan extensions");
    }

    // 将这些扩展添加到传入的 vector 中
    for (u32 i = 0; i < glfwExtensionCount; i++) {
      extensions.push_back(glfwExtensions[i]);
    }
  }

  [[nodiscard]] LX_core::WindowPlacement getPlacement() const {
    LX_core::WindowPlacement placement{};
    glfwGetWindowPos(window, &placement.x, &placement.y);
    glfwGetWindowSize(window, &placement.width, &placement.height);
    placement.maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
    return placement;
  }

  [[nodiscard]] LX_core::WindowUsableBounds getUsableBounds() const {
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    GLFWmonitor* monitor =
        glfwGetWindowMonitor(window) ? glfwGetWindowMonitor(window)
                                     : glfwGetPrimaryMonitor();
    if (monitor == nullptr && monitorCount > 0) {
      monitor = monitors[0];
    }

    if (monitor != nullptr) {
      int x = 0;
      int y = 0;
      int widthPx = 0;
      int heightPx = 0;
      glfwGetMonitorWorkarea(monitor, &x, &y, &widthPx, &heightPx);
      if (widthPx > 0 && heightPx > 0) {
        return LX_core::WindowUsableBounds{
            .x = x,
            .y = y,
            .width = widthPx,
            .height = heightPx,
        };
      }
    }

    return LX_core::WindowUsableBounds{
        .x = 0,
        .y = 0,
        .width = width > 0 ? width : 1280,
        .height = height > 0 ? height : 720,
    };
  }

  [[nodiscard]] LX_core::WindowUsableBounds
  getUsableBoundsForPlacement(const LX_core::WindowPlacement& placement) const {
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    const long long anchorX = LX_core::windowPlacementCenterX(placement);
    const long long anchorY = LX_core::windowPlacementCenterY(placement);

    if (monitors != nullptr && monitorCount > 0) {
      GLFWmonitor* bestMonitor = monitors[0];
      long long bestDistance = std::numeric_limits<long long>::max();

      for (int i = 0; i < monitorCount; ++i) {
        int x = 0;
        int y = 0;
        int widthPx = 0;
        int heightPx = 0;
        glfwGetMonitorWorkarea(monitors[i], &x, &y, &widthPx, &heightPx);
        if (widthPx <= 0 || heightPx <= 0) {
          continue;
        }

        const long long distance =
            squaredDistanceToRect(x, y, widthPx, heightPx, anchorX, anchorY);
        if (distance < bestDistance) {
          bestDistance = distance;
          bestMonitor = monitors[i];
          if (distance == 0) {
            break;
          }
        }
      }

      int x = 0;
      int y = 0;
      int widthPx = 0;
      int heightPx = 0;
      glfwGetMonitorWorkarea(bestMonitor, &x, &y, &widthPx, &heightPx);
      if (widthPx > 0 && heightPx > 0) {
        return LX_core::WindowUsableBounds{
            .x = x,
            .y = y,
            .width = widthPx,
            .height = heightPx,
        };
      }
    }

    return getUsableBounds();
  }

  void applyPlacement(const LX_core::WindowPlacement& placement) {
    const auto sanitized =
        LX_core::sanitizeWindowPlacement(placement,
                                         getUsableBoundsForPlacement(placement));
    if (!sanitized.has_value()) {
      return;
    }

    glfwRestoreWindow(window);
    if (sanitized->width > 0 && sanitized->height > 0) {
      glfwSetWindowSize(window, sanitized->width, sanitized->height);
      width = sanitized->width;
      height = sanitized->height;
    }
    glfwSetWindowPos(window, sanitized->x, sanitized->y);
    if (sanitized->maximized) {
      glfwMaximizeWindow(window);
    }
  }
};

void Window::Initialize() {}

Window::Window(const char *title, int width, int height,
               std::optional<LX_core::WindowPlacement> initialPlacement)
    : Window(title, width, height,
             WindowCreateOptions{.initialPlacement = initialPlacement}) {}

Window::Window(const char *title, int width, int height,
               const WindowCreateOptions& options)
    : pImpl(std::make_unique<Impl>(
          title, width, height,
          resolveInitialPlacement(width, height, options))) {}

Window::~Window() = default;

std::vector<LX_core::DisplayInfo> Window::enumerateDisplays() {
  if (!glfwInit()) {
    throw std::runtime_error("GLFW init failed");
  }

  int monitorCount = 0;
  GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
  if (monitors == nullptr || monitorCount <= 0) {
    return {};
  }

  std::vector<LX_core::DisplayInfo> result;
  result.reserve(static_cast<size_t>(monitorCount));
  for (int i = 0; i < monitorCount; ++i) {
    int x = 0;
    int y = 0;
    glfwGetMonitorPos(monitors[i], &x, &y);

    int widthPx = 0;
    int heightPx = 0;
    const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
    if (mode != nullptr) {
      widthPx = mode->width;
      heightPx = mode->height;
    }

    int workX = 0;
    int workY = 0;
    int workWidth = 0;
    int workHeight = 0;
    glfwGetMonitorWorkarea(monitors[i], &workX, &workY, &workWidth,
                           &workHeight);
    if (workWidth <= 0 || workHeight <= 0) {
      workX = x;
      workY = y;
      workWidth = widthPx;
      workHeight = heightPx;
    }

    float xScale = 1.0f;
    float yScale = 1.0f;
    glfwGetMonitorContentScale(monitors[i], &xScale, &yScale);
    float scale = std::max(xScale, yScale);
    if (scale <= 0.0f) {
      scale = 1.0f;
    }

    const char* name = glfwGetMonitorName(monitors[i]);
    LX_core::DisplayInfo info{
        .index = i,
        .backend = "glfw",
        .name = name != nullptr ? name : "",
        .bounds = {.x = x, .y = y, .width = widthPx, .height = heightPx},
        .usableBounds =
            {.x = workX, .y = workY, .width = workWidth, .height = workHeight},
        .contentScale = scale,
    };
    LX_core::finalizeDisplayInfo(info);
    result.push_back(std::move(info));
  }

  return result;
}

// Live-query to survive window resizes: swapchain rebuild asks getWidth/Height
// to pick up the new framebuffer extent.
int Window::getWidth() const {
  int w = pImpl->width;
  int h = pImpl->height;
  glfwGetFramebufferSize(pImpl->window, &w, &h);
  pImpl->width = w;
  pImpl->height = h;
  return w;
}
int Window::getHeight() const {
  int w = pImpl->width;
  int h = pImpl->height;
  glfwGetFramebufferSize(pImpl->window, &w, &h);
  pImpl->width = w;
  pImpl->height = h;
  return h;
}
bool Window::shouldClose() {
  bool result = pImpl->shouldClose();
  if (result && pImpl->closeCallback) {
    result = pImpl->closeCallback();
    if (!result) {
      glfwSetWindowShouldClose(pImpl->window, GLFW_FALSE);
    }
  }
  return result;
}
VkSurfaceKHR Window::getVulkanSurface(VkInstance instance) const {
  return pImpl->getVulkanSurface(instance);
}
void Window::onClose(std::function<bool()> cb) { pImpl->closeCallback = cb; }

LX_core::InputStateSharedPtr Window::getInputState() const {
  static auto dummy = std::make_shared<LX_core::DummyInputState>();
  return dummy;
}

LX_core::WindowPlacement Window::getPlacement() const {
  return pImpl->getPlacement();
}

LX_core::WindowUsableBounds Window::getUsableBounds() const {
  return pImpl->getUsableBounds();
}

LX_core::WindowUsableBounds
Window::getUsableBoundsForPlacement(
    const LX_core::WindowPlacement& placement) const {
  return pImpl->getUsableBoundsForPlacement(placement);
}

void Window::applyPlacement(const LX_core::WindowPlacement& placement) {
  pImpl->applyPlacement(placement);
}

void* Window::getNativeHandle() const {
  return static_cast<void*>(pImpl->window);
}

void *Window::createGraphicsHandle(GraphicsAPI api,
                                   void *graphicsInstance) const {
  if (api == GraphicsAPI::Vulkan) {
    return (WindowGraphicsHandle)getVulkanSurface((VkInstance)graphicsInstance);
  }
  return nullptr;
}

void Window::destroyGraphicsHandle(GraphicsAPI api, void *graphicsInstance,
                                   void *handle) const {
  if (api == GraphicsAPI::Vulkan && handle) {
    vkDestroySurfaceKHR((VkInstance)graphicsInstance, (VkSurfaceKHR)handle,
                        nullptr);
  }
}

void Window::getRequiredExtensions(std::vector<const char *> &extensions) const {
  pImpl->getRequiredExtensions(extensions);
}

void Window::updateSize(bool *closed, int *width, int *height) {
  glfwGetFramebufferSize(pImpl->window, width, height);
  pImpl->width = *width;
  pImpl->height = *height;
  *closed = glfwWindowShouldClose(pImpl->window);
}



} // namespace LX_infra
#endif
