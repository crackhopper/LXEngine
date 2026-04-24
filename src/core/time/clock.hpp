#pragma once

#include "core/platform/types.hpp"
#include <array>
#include <chrono>

namespace LX_core {

class Clock {
public:
  Clock() = default;

  void tick();

  float deltaTime() const { return m_deltaTime; }
  float smoothedDeltaTime() const;
  double totalTime() const { return m_totalTime; }
  u64 frameCount() const { return m_frameCount; }

private:
  using Clk = std::chrono::steady_clock;

  Clk::time_point m_startTime{};
  Clk::time_point m_lastTickTime{};
  bool m_firstTick = true;
  float m_deltaTime = 0.0f;
  double m_totalTime = 0.0;
  u64 m_frameCount = 0;

  static constexpr int kSmoothWindow = 60;
  std::array<float, kSmoothWindow> m_recentDeltas{};
  int m_smoothCursor = 0;
  int m_sampleCount = 0;
};

} // namespace LX_core
