#pragma once
#include <functional>
#include <vulkan/vulkan.h>
#include "core/platform/window.hpp"
namespace LX_infra {
class WindowImpl: public LX_core::Window {
public:
  static void Initialize(); // 初始化窗口系统

  WindowImpl(const char *title, int width, int height);
  ~WindowImpl();

  int getWidth() const;
  int getHeight() const;

  // 暴露 Vulkan surface
  VkSurfaceKHR getVulkanSurface(VkInstance instance) const;

  bool shouldClose() const;

  void onClose(std::function<void()> cb);

private:
  struct Impl; // PImpl 隐藏 SDL/GLFW
  Impl *pImpl;
};
} // namespace LX_infra