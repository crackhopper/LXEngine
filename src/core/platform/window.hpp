#pragma once
#include <functional>
#include <memory>
#include "../platform/types.hpp"
namespace LX_core {
class Window {
public:
  static void Initialize(); // 初始化窗口系统

  Window(const char *title, int width, int height);
  ~Window();

  virtual int getWidth() const = 0;
  virtual int getHeight() const = 0;

  virtual void *getHandle(GraphicsAPI api) const = 0;
  virtual void onClose(std::function<void()> cb) = 0;
};
using WindowPtr = std::shared_ptr<Window>;
} // namespace LX_core