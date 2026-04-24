#ifdef USE_SDL
#include "window.hpp"
#include "sdl3_input_state.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace LX_infra {

struct Window::Impl {
  int width;
  int height;
  const char *title;
  SDL_Window *window = nullptr;
  VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
  std::function<void()> closeCallback;
  std::shared_ptr<Sdl3InputState> inputState;

  Impl(const char *t, int w, int h) : width(w), height(h), title(t), inputState(std::make_shared<Sdl3InputState>()) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      auto errorstr = SDL_GetError();
      std::cerr << "Failed to initialize SDL: " << errorstr << "\n";
      throw std::runtime_error(errorstr);
    }
    window = SDL_CreateWindow(title, width, height,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window)
      throw std::runtime_error(SDL_GetError());
  }

  ~Impl() {
    if (window){
      SDL_DestroyWindow(window);
      window = nullptr;
    }
    SDL_Quit();
  }

  bool shouldClose() {
    SDL_Event event;
    bool quit = false;
    // Forward each SDL event to ImGui (no-op until ImGui context exists),
    // then to the input state, then check for quit. Single poll loop so no
    // event is consumed twice.
    const bool imguiReady = ImGui::GetCurrentContext() != nullptr;
    while (SDL_PollEvent(&event)) {
      if (imguiReady) {
        ImGui_ImplSDL3_ProcessEvent(&event);
      }
      if (inputState->handleSdlEvent(event)) {
        quit = true;
      }
    }
    return quit;
  }

  VkSurfaceKHR getVulkanSurface(VkInstance instance) {
    if (vkSurface != VK_NULL_HANDLE) {
      return vkSurface;
    }
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &vkSurface))
      throw std::runtime_error("Failed to create Vulkan surface");
    return vkSurface;
  }

  void getRequiredExtensions(std::vector<const char *> &extensions) const {
    u32 count = 0;
    // 第一次调用：获取扩展数量
    const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&count);

    if (!sdlExtensions) {
      throw std::runtime_error(SDL_GetError());
    }

    // 将获取到的扩展名放入传入的 vector 中
    for (u32 i = 0; i < count; ++i) {
      extensions.push_back(sdlExtensions[i]);
    }
  }

  void updateSize(bool *closed, int *width, int *height) {
    *width = 0;
    *height = 0;
    // 获取窗口像素尺寸
    SDL_GetWindowSizeInPixels(window, width, height);

    // 如果窗口被最小化，像素尺寸可能为 0，需要等待
    SDL_Event event;
    while (*width == 0 || *height == 0) {
      // 等待事件
      if (SDL_WaitEvent(&event)) {
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
          SDL_GetWindowSizeInPixels(window, width, height);
        } else if (event.type == SDL_EVENT_QUIT) {
          *closed = true;
          return; // 用户关闭窗口
        }
      }
    }
    *closed = false;
  }
};

void Window::Initialize() {}

Window::Window(const char *title, int width, int height)
    : pImpl(new Impl(title, width, height)) {}

Window::~Window() { delete pImpl; }
// getWidth/getHeight query SDL for the live pixel size each call so swapchain
// rebuild after a resize sees the new extent, not the construction-time value.
// SDL_GetWindowSizeInPixels is a cheap local lookup; the per-frame cost is
// negligible.
int Window::getWidth() const {
  int w = pImpl->width;
  int h = pImpl->height;
  SDL_GetWindowSizeInPixels(pImpl->window, &w, &h);
  pImpl->width = w;
  pImpl->height = h;
  return w;
}
int Window::getHeight() const {
  int w = pImpl->width;
  int h = pImpl->height;
  SDL_GetWindowSizeInPixels(pImpl->window, &w, &h);
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

void Window::getRequiredExtensions(
    std::vector<const char *> &extensions) const {
  pImpl->getRequiredExtensions(extensions);
}

VkSurfaceKHR Window::getVulkanSurface(VkInstance instance) const {
  return const_cast<Impl *>(pImpl)->getVulkanSurface(instance);
}
void Window::onClose(std::function<void()> cb) { pImpl->closeCallback = cb; }

LX_core::InputStateSharedPtr Window::getInputState() const {
  return pImpl->inputState;
}

void* Window::getNativeHandle() const {
  return static_cast<void*>(pImpl->window);
}

WindowGraphicsHandle
Window::createGraphicsHandle(GraphicsAPI api,
                             GraphicsInstanceHandle instance) const {
  if (api == GraphicsAPI::Vulkan) {
    return (WindowGraphicsHandle)getVulkanSurface((VkInstance)instance);
  }
  return nullptr;
}

void Window::destroyGraphicsHandle(GraphicsAPI api,
                                   GraphicsInstanceHandle instance,
                                   WindowGraphicsHandle handle) const {
  if (api == GraphicsAPI::Vulkan && handle) {
    // Vulkan surfaces are destroyed automatically when the window is destroyed
  }
}

void Window::updateSize(bool *closed, int *width, int *height) {}

} // namespace LX_infra

#endif
