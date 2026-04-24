#ifdef USE_GLFW
#include "window.hpp"
#include "core/input/dummy_input_state.hpp"
#include <GLFW/glfw3.h>
#include <functional>
#include <stdexcept>

namespace LX_infra {

struct Window::Impl {
  int width;
  int height;
  const char *title;
  GLFWwindow *window = nullptr;
  std::function<void()> closeCallback;

  Impl(const char *t, int w, int h) : width(w), height(h), title(t) {
    if (!glfwInit())
      throw std::runtime_error("GLFW init failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
      throw std::runtime_error("GLFW create window failed");
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
};

void Window::Initialize() {}

Window::Window(const char *title, int width, int height)
    : pImpl(std::make_unique<Impl>(title, width, height)) {}

Window::~Window() = default;
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
    pImpl->closeCallback();
  }
  return result;
}
VkSurfaceKHR Window::getVulkanSurface(VkInstance instance) const {
  return pImpl->getVulkanSurface(instance);
}
void Window::onClose(std::function<void()> cb) { pImpl->closeCallback = cb; }

LX_core::InputStateSharedPtr Window::getInputState() const {
  static auto dummy = std::make_shared<LX_core::DummyInputState>();
  return dummy;
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
