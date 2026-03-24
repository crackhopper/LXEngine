#pragma once
#include <functional>
#include <vulkan/vulkan.h>
#include "core/platform/window.hpp"
namespace LX_infra {
class Window: public LX_core::Window {
public:
  static void Initialize(); // 初始化窗口系统

  Window(const char *title, int width, int height);
  ~Window();

  int getWidth() const override;
  int getHeight() const override;
  void getRequiredExtensions(std::vector<const char *> &extensions) const override;

  void *createGraphicsHandle(GraphicsAPI api, void *graphicsInstance) const override;
  void destroyGraphicsHandle(GraphicsAPI api, void *graphicsInstance, void *handle) const override;

  // 暴露 Vulkan surface
  VkSurfaceKHR getVulkanSurface(VkInstance instance) const;

  bool shouldClose() const;

  void onClose(std::function<void()> cb) override;

private:
  struct Impl; // PImpl 隐藏 SDL/GLFW
  Impl *pImpl;
};
} // namespace LX_infra