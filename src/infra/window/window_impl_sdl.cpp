#ifdef USE_SDL
#include "window.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <functional>
#include <stdexcept>

namespace LX_infra {

struct Window::Impl {
  int width;
  int height;
  const char *title;
  SDL_Window *window = nullptr;
  std::function<void()> closeCallback;

  Impl(const char *t, int w, int h) : width(w), height(h), title(t) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
      throw std::runtime_error(SDL_GetError());
    window = SDL_CreateWindow(title, width, height,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window)
      throw std::runtime_error(SDL_GetError());
  }

  ~Impl() {
    if (window)
      SDL_DestroyWindow(window);
    SDL_Quit();
  }

  bool shouldClose() const {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT)
        return true;
    }
    return false;
  }

  VkSurfaceKHR getVulkanSurface(VkInstance instance) const {
    VkSurfaceKHR surface;
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
      throw std::runtime_error("Failed to create Vulkan surface");
    return surface;
  }

  void getRequiredExtensions(std::vector<const char *> &extensions) const {
    uint32_t count = 0;
    // 第一次调用：获取扩展数量
    const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&count);

    if (!sdlExtensions) {
      throw std::runtime_error(SDL_GetError());
    }

    // 将获取到的扩展名放入传入的 vector 中
    for (uint32_t i = 0; i < count; ++i) {
      extensions.push_back(sdlExtensions[i]);
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

void Window::getRequiredExtensions(
    std::vector<const char *> &extensions) const {
  pImpl->getRequiredExtensions(extensions);
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
    // Vulkan surfaces are destroyed automatically when the window is destroyed
  }
}

} // namespace LX_infra

#endif