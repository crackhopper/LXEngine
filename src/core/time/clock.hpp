#pragma once

#include <chrono>
#include <cstdint>

namespace LX_core {

class Clock {
public:
  Clock() = default;

  void tick();

  float deltaTime() const { return m_deltaTime; }
  double totalTime() const { return m_totalTime; }
  uint64_t frameCount() const { return m_frameCount; }

private:
  using Clk = std::chrono::steady_clock;

  Clk::time_point m_startTime{};
  Clk::time_point m_lastTickTime{};
  bool m_firstTick = true;
  float m_deltaTime = 0.0f;
  double m_totalTime = 0.0;
  uint64_t m_frameCount = 0;
};

} // namespace LX_core
