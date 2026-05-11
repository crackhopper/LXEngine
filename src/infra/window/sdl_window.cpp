#ifdef USE_SDL
#include "window.hpp"
#include "sdl3_input_state.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>

namespace LX_infra {

namespace {

[[nodiscard]] long long squaredDistanceToRect(const SDL_Rect& rect,
                                              const long long x,
                                              const long long y) {
  const long long rectMinX = static_cast<long long>(rect.x);
  const long long rectMinY = static_cast<long long>(rect.y);
  const long long rectMaxX = rectMinX + static_cast<long long>(rect.w) - 1LL;
  const long long rectMaxY = rectMinY + static_cast<long long>(rect.h) - 1LL;
  const long long clampedX = std::clamp(x, rectMinX, rectMaxX);
  const long long clampedY = std::clamp(y, rectMinY, rectMaxY);
  const long long dx = x - clampedX;
  const long long dy = y - clampedY;
  return dx * dx + dy * dy;
}

} // namespace

struct Window::Impl {
  int width;
  int height;
  const char *title;
  SDL_Window *window = nullptr;
  VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
  std::function<bool()> closeCallback;
  std::shared_ptr<Sdl3InputState> inputState;

  Impl(const char *t, int w, int h,
       const std::optional<LX_core::WindowPlacement>& initialPlacement)
      : width(w),
        height(h),
        title(t),
        inputState(std::make_shared<Sdl3InputState>()) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      auto errorstr = SDL_GetError();
      std::cerr << "Failed to initialize SDL: " << errorstr << "\n";
      throw std::runtime_error(errorstr);
    }
    window = SDL_CreateWindow(title, width, height,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                  SDL_WINDOW_HIDDEN);
    if (!window)
      throw std::runtime_error(SDL_GetError());

    if (initialPlacement.has_value()) {
      applyPlacement(*initialPlacement);
    }
    SDL_ShowWindow(window);
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

  [[nodiscard]] LX_core::WindowPlacement getPlacement() const {
    LX_core::WindowPlacement placement{};
    SDL_GetWindowPosition(window, &placement.x, &placement.y);
    SDL_GetWindowSize(window, &placement.width, &placement.height);
    placement.maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
    return placement;
  }

  [[nodiscard]] LX_core::WindowUsableBounds getUsableBounds() const {
    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (display == 0) {
      display = SDL_GetPrimaryDisplay();
    }

    SDL_Rect rect{};
    if (display != 0 && SDL_GetDisplayUsableBounds(display, &rect)) {
      return LX_core::WindowUsableBounds{
          .x = rect.x,
          .y = rect.y,
          .width = rect.w,
          .height = rect.h,
      };
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
    const long long anchorX = LX_core::windowPlacementCenterX(placement);
    const long long anchorY = LX_core::windowPlacementCenterY(placement);

    int displayCount = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
    if (displays != nullptr && displayCount > 0) {
      SDL_DisplayID bestDisplay = displays[0];
      long long bestDistance = std::numeric_limits<long long>::max();

      for (int i = 0; i < displayCount; ++i) {
        SDL_Rect rect{};
        if (!SDL_GetDisplayUsableBounds(displays[i], &rect) || rect.w <= 0 ||
            rect.h <= 0) {
          continue;
        }

        const long long distance = squaredDistanceToRect(rect, anchorX, anchorY);
        if (distance < bestDistance) {
          bestDistance = distance;
          bestDisplay = displays[i];
          if (distance == 0) {
            break;
          }
        }
      }

      SDL_Rect rect{};
      SDL_free(displays);
      if (SDL_GetDisplayUsableBounds(bestDisplay, &rect) && rect.w > 0 &&
          rect.h > 0) {
        return LX_core::WindowUsableBounds{
            .x = rect.x,
            .y = rect.y,
            .width = rect.w,
            .height = rect.h,
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

    SDL_RestoreWindow(window);
    if (sanitized->width > 0 && sanitized->height > 0) {
      SDL_SetWindowSize(window, sanitized->width, sanitized->height);
      width = sanitized->width;
      height = sanitized->height;
    }
    SDL_SetWindowPosition(window, sanitized->x, sanitized->y);
    if (sanitized->maximized) {
      SDL_MaximizeWindow(window);
    }
  }
};

void Window::Initialize() {}

Window::Window(const char *title, int width, int height,
               std::optional<LX_core::WindowPlacement> initialPlacement)
    : pImpl(
          std::make_unique<Impl>(title, width, height, initialPlacement)) {}

Window::~Window() = default;
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
    result = pImpl->closeCallback();
  }
  return result;
}

void Window::getRequiredExtensions(
    std::vector<const char *> &extensions) const {
  pImpl->getRequiredExtensions(extensions);
}

VkSurfaceKHR Window::getVulkanSurface(VkInstance instance) const {
  return const_cast<Impl *>(pImpl.get())->getVulkanSurface(instance);
}
void Window::onClose(std::function<bool()> cb) { pImpl->closeCallback = cb; }

LX_core::InputStateSharedPtr Window::getInputState() const {
  return pImpl->inputState;
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
    VkInstance vkInstance = (VkInstance)instance;
    VkSurfaceKHR surface = (VkSurfaceKHR)handle;
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    if (pImpl->vkSurface == surface) {
      pImpl->vkSurface = VK_NULL_HANDLE;
    }
  }
}

void Window::updateSize(bool *closed, int *width, int *height) {
  pImpl->updateSize(closed, width, height);
}

} // namespace LX_infra

#endif
