#pragma once
#include "core/platform/types.hpp"
#include <functional>
#include <memory>
namespace LX_core {
using WindowGraphicsHandle = void *;
using GraphicsInstanceHandle = void *;  
class Window {
public:
  static void Initialize(); // 初始化窗口系统

protected:
  Window() = default;
  ~Window() = default;

public:
  virtual int getWidth() const = 0;
  virtual int getHeight() const = 0;
  virtual void updateSize(bool* closed, int *width, int *height) = 0;
  virtual void getRequiredExtensions(std::vector<const char *> &extensions) const = 0;

  /**
   * @brief 为特定的图形 API 准备渲染表面/句柄
   * @param api 指定目标 API (Vulkan, DX12, etc.)
   * @param graphicsInstance 对于 Vulkan，这里需要传入 VkInstance 的指针或句柄
   * @return 返回创建好的句柄（对于 Vulkan 则是 VkSurfaceKHR）
   */
  virtual WindowGraphicsHandle createGraphicsHandle(GraphicsAPI api,
                                     GraphicsInstanceHandle instance) const = 0;

  // 辅助销毁方法（因为 Surface 必须在 Instance 销毁前销毁）
  virtual void destroyGraphicsHandle(GraphicsAPI api, GraphicsInstanceHandle instance,
                                     WindowGraphicsHandle handle) const = 0;

  virtual void onClose(std::function<void()> cb) = 0;
  virtual bool shouldClose() = 0;
};
using WindowPtr = std::shared_ptr<Window>;
} // namespace LX_core