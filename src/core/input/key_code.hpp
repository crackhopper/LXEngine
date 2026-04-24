#pragma once
#include "core/platform/types.hpp"

namespace LX_core {

enum class KeyCode : u16 {
  Unknown = 0,

  // 字母
  A, B, C, D, E, F, G, H, I, J, K, L, M,
  N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

  // 数字
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

  // 控制键
  Escape, Space, LShift, RShift, LCtrl, RCtrl, LAlt, RAlt, Enter, Tab,

  // 方向
  Left, Right, Up, Down,

  // 功能键（Phase 1 只到 F4）
  F1, F2, F3, F4,

  Count
};

} // namespace LX_core
