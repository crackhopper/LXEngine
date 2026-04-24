#pragma once
#include <functional>
#include <memory>
#include <vulkan/vulkan.h>
#include "core/platform/window.hpp"
namespace LX_infra {
using LX_core::WindowGraphicsHandle;
using LX_core::GraphicsInstanceHandle;
class Window: public LX_core::Window {
public:
  static void Initialize(); // 初始化窗口系统

  Window(const char *title, int width, int height);
  ~Window();

  int getWidth() const override;
  int getHeight() const override;
  void updateSize(bool* closed, int *width, int *height) override;
  void getRequiredExtensions(std::vector<const char *> &extensions) const override;

  WindowGraphicsHandle createGraphicsHandle(GraphicsAPI api, GraphicsInstanceHandle instance) const override;
  void destroyGraphicsHandle(GraphicsAPI api, GraphicsInstanceHandle instance, WindowGraphicsHandle handle) const override;

  // 暴露 Vulkan surface
  VkSurfaceKHR getVulkanSurface(VkInstance instance) const;

  LX_core::InputStateSharedPtr getInputState() const override;

  void* getNativeHandle() const override;

  bool shouldClose() override;

  void onClose(std::function<void()> cb) override;

private:
  struct Impl; // PImpl 隐藏 SDL/GLFW
  std::unique_ptr<Impl> pImpl;
};
} // namespace LX_infra
