#ifdef USE_SDL
#include "window.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdexcept>

namespace LX_infra {

struct Window::Impl {
  int width;
  int height;
  const char *title;
  SDL_Window *window = nullptr;

  Impl(const char *t, int w, int h) : width(w), height(h), title(t) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
      throw std::runtime_error(SDL_GetError());
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, width, height,
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
      if (event.type == SDL_QUIT)
        return true;
    }
    return false;
  }

  VkSurfaceKHR getVulkanSurface(VkInstance instance) const {
    VkSurfaceKHR surface;
    if (!SDL_Vulkan_CreateSurface(window, instance, &surface))
      throw std::runtime_error("Failed to create Vulkan surface");
    return surface;
  }
};

Window::Window(int width, int height, const char *title)
    : pImpl(new Impl(width, height, title)) {}

Window::~Window() { delete pImpl; }
int Window::getWidth() const { return pImpl->width; }
int Window::getHeight() const { return pImpl->height; }
bool Window::shouldClose() const { return pImpl->shouldClose(); }
VkSurfaceKHR Window::getVulkanSurface(VkInstance instance) const {
  return pImpl->getVulkanSurface(instance);
}

} // namespace LX_infra

#endif