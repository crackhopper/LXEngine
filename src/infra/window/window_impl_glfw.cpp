#ifdef USE_GLFW
#include "window.hpp"
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

  bool shouldClose() const { return glfwWindowShouldClose(window); }

  VkSurfaceKHR getVulkanSurface(VkInstance instance) const {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) !=
        VK_SUCCESS)
      throw std::runtime_error("Failed to create Vulkan surface");
    return surface;
  }

  void getRequiredExtensions(std::vector<const char *> &extensions) const {
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions;

    // 获取 GLFW 运行 Vulkan 所需的扩展列表（如 VK_KHR_surface 等）
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    if (glfwExtensions == nullptr) {
      throw std::runtime_error(
          "GLFW could not find required Vulkan extensions");
    }

    // 将这些扩展添加到传入的 vector 中
    for (uint32_t i = 0; i < glfwExtensionCount; i++) {
      extensions.push_back(glfwExtensions[i]);
    }
  }
};

void Window::Initialize() {}

Window::Window(const char *title, int width, int height)
    : pImpl(new Impl(title, width, height)) {}

Window::~Window() { delete pImpl; }
int Window::getWidth() const { return pImpl->width; }
int Window::getHeight() const { return pImpl->height; }
bool Window::shouldClose() const {
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

void *Window::createGraphicsHandle(GraphicsAPI api,
                                   void *graphicsInstance) const {
  if (api == GraphicsAPI::Vulkan) {
    return new VkSurfaceKHR(getVulkanSurface(*(VkInstance *)graphicsInstance));
  }
  return nullptr;
}

void Window::destroyGraphicsHandle(GraphicsAPI api, void *graphicsInstance,
                                   void *handle) const {
  if (api == GraphicsAPI::Vulkan && handle) {
  }
}

void Window::getRequiredExtensions(std::vector<const char *> &extensions) const {
  pImpl->getRequiredExtensions(extensions);
}



} // namespace LX_infra
#endif