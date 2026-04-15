#include "core/time/clock.hpp"

namespace LX_core {

void Clock::tick() {
  const Clk::time_point now = Clk::now();
  if (m_firstTick) {
    m_startTime = now;
    m_lastTickTime = now;
    m_deltaTime = 0.0f;
    m_totalTime = 0.0;
    m_frameCount = 0;
    m_firstTick = false;
    return;
  }

  m_deltaTime = std::chrono::duration<float>(now - m_lastTickTime).count();
  m_totalTime = std::chrono::duration<double>(now - m_startTime).count();
  m_lastTickTime = now;
  ++m_frameCount;
}

} // namespace LX_core
