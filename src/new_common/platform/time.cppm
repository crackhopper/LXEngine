module;

#include <chrono>

export module LX_New_Common.Platform:Time;

import :Types;

export namespace LX_New_Common {

class Clock {
private:
  std::chrono::high_resolution_clock::time_point m_StartTime;

public:
  Clock() { Reset(); }

  void Reset() { m_StartTime = std::chrono::high_resolution_clock::now(); }

  // 获取自 Reset 以来的微秒数 (us)
  u64 GetTimeMicroseconds() const {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now -
                                                                 m_StartTime)
        .count();
  }

  // 获取自 Reset 以来的毫秒数 (ms)
  f64 GetTimeMilliseconds() const {
    return static_cast<f64>(GetTimeMicroseconds()) / 1000.0;
  }

  // 获取自 Reset 以来的秒数 (s)，通常用于计算 Delta Time
  f32 GetTimeSeconds() const {
    return static_cast<f32>(GetTimeMicroseconds()) / 1000000.0f;
  }
};

} // namespace LX_New_Common
