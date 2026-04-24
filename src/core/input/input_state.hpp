#pragma once
#include "core/input/key_code.hpp"
#include "core/input/mouse_button.hpp"
#include "core/math/vec.hpp"
#include <memory>

namespace LX_core {

class IInputState {
public:
  virtual ~IInputState() = default;

  // ---- 键盘 ----
  virtual bool isKeyDown(KeyCode code) const = 0;

  // ---- 鼠标按键 ----
  virtual bool isMouseButtonDown(MouseButton button) const = 0;

  // ---- 鼠标位置（窗口客户区像素坐标，左上为 0,0）----
  virtual Vec2f getMousePosition() const = 0;

  // ---- 自上一输入帧累计的鼠标位移 ----
  virtual Vec2f getMouseDelta() const = 0;

  // ---- 自上一输入帧��计的滚轮位移 ----
  virtual float getMouseWheelDelta() const = 0;

  // ---- 帧推进 ----
  virtual void nextFrame() = 0;

  // ---- UI capture 协调 ----
  // 默认返回 false。上层 UI（如 ImGui）声明希望独占输入时，具体实现可覆写
  // 这两个方法（例如返回 ImGui::GetIO().WantCapture*），相机控制器等消费者
  // 据此决定是否让出鼠标/键盘控制权。
  virtual bool isUiCapturingMouse() const { return false; }
  virtual bool isUiCapturingKeyboard() const { return false; }
};

using InputStateSharedPtr = std::shared_ptr<IInputState>;

} // namespace LX_core
