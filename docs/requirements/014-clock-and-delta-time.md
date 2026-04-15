# REQ-014: Clock + deltaTime 模块

## 背景

当前所有 demo / 集成测试的主循环都没有时间概念：

- `src/test/test_render_triangle.cpp:104-156` 的 while 循环只是简单 `while (running)`，没有 deltaTime
- 任何"按时间推进"的逻辑（旋转、动画、物理）目前要么硬编码（"每帧加 0.01 弧度"），要么干脆没有
- REQ-016 的 FreeFly 相机控制器需要 `position += velocity * deltaTime`，没有 Clock 这件事根本没法做
- REQ-018 的 DebugPanel 想显示 FPS，也要 deltaTime / smoothed deltaTime
- REQ-020 的 `EngineLoop` 也需要一个稳定的 `tick()` 时间源来驱动每帧 update hook

[Phase 2 REQ-206](../../notes/roadmaps/phase-2-foundation-layer.md) 规划了完整的 `Clock` 类（含 fixed step accumulator、timeScale、frameCount）。本 REQ 是 Phase 2 REQ-206 的**最小可用前置版本**：只暴露 Phase 1 调试链路真正需要的字段（`tick / deltaTime / totalTime / frameCount`），fixed step / timeScale 等 Phase 2 再加。

## 目标

1. `core/time/clock.hpp` 提供一个 `Clock` 类
2. `tick()` 在每帧开头调用，更新内部时间戳
3. 提供 `deltaTime()` / `totalTime()` / `frameCount()` 三个 query
4. 接口签名是 Phase 2 REQ-206 的子集，向上兼容

## 需求

### R1: `Clock` 类

新建 `src/core/time/clock.hpp` + `src/core/time/clock.cpp`：

```cpp
#pragma once
#include <chrono>
#include <cstdint>

namespace LX_core {

class Clock {
public:
  Clock();

  /// 每帧开头调用一次。
  /// 第一次调用：deltaTime = 0，totalTime = 0
  /// 后续调用：deltaTime = now - lastTickTime，totalTime += deltaTime
  void tick();

  /// 上一帧的时长（秒）。第一帧为 0。
  float deltaTime() const { return m_deltaTime; }

  /// 自首次 tick() 起累计时长（秒）。
  double totalTime() const { return m_totalTime; }

  /// 自首次 tick() 起的帧编号。第一帧为 0。
  uint64_t frameCount() const { return m_frameCount; }

  /// 平滑的 deltaTime，用于 FPS 显示。
  /// 实现：滑动窗口 (last 60 frames) 取平均。
  /// 没有足够样本时返回最近一次 deltaTime()。
  float smoothedDeltaTime() const;

private:
  using Clk = std::chrono::steady_clock;
  Clk::time_point m_startTime;
  Clk::time_point m_lastTickTime;
  bool   m_firstTick = true;
  float  m_deltaTime = 0.0f;
  double m_totalTime = 0.0;
  uint64_t m_frameCount = 0;

  static constexpr size_t kSmoothWindow = 60;
  float  m_recentDeltas[kSmoothWindow] = {};
  size_t m_recentDeltasFilled = 0;
  size_t m_recentDeltasCursor = 0;
};

}
```

实现要点：

- `tick()` 第一次调用：`m_startTime = m_lastTickTime = now()`，`m_deltaTime = 0`，`m_totalTime = 0`，`m_firstTick = false`，**不**自增 frameCount
- 后续 `tick()`：`now = Clk::now()`；`m_deltaTime = duration<float>(now - m_lastTickTime).count()`；`m_totalTime = duration<double>(now - m_startTime).count()`；`m_lastTickTime = now`；`m_frameCount++`；环形写入 `m_recentDeltas`

`smoothedDeltaTime()` 实现：

- 取 `m_recentDeltas[0..m_recentDeltasFilled)` 的算术平均
- 当 filled == 0 返回 `m_deltaTime`

### R2: 与现有循环的接入示例

修改 `src/test/test_render_triangle.cpp`，在主循环里展示用法（**不**新增功能，只是预演 REQ-020 / REQ-019 / REQ-016 的调用顺序）：

```cpp
LX_core::Clock clock;
while (running) {
  clock.tick();
  if (window->shouldClose()) break;
  // ...原 camera 设置...
  renderer->uploadData();
  renderer->draw();
  window->nextFrame();
}
```

不依赖 `clock.deltaTime()` 改变现有渲染行为 —— 这只是接入点示范。

从长期架构看，这段 while-loop 应在 REQ-020 中收敛到 `EngineLoop::tickFrame()` 内部；这里保留展开版，只为锁定 `Clock` 的最小契约。

### R3: 单元测试

新建 `src/test/integration/test_clock.cpp`：

```cpp
TEST(Clock, first_tick_has_zero_delta) {
  Clock c;
  c.tick();
  EXPECT_FLOAT_EQ(c.deltaTime(), 0.0f);
  EXPECT_EQ(c.frameCount(), 0u);
}

TEST(Clock, second_tick_has_nonzero_delta) {
  Clock c;
  c.tick();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  c.tick();
  EXPECT_GT(c.deltaTime(), 0.005f);   // 至少 5ms
  EXPECT_LT(c.deltaTime(), 0.100f);   // 最多 100ms（防止超时机）
  EXPECT_EQ(c.frameCount(), 1u);
}

TEST(Clock, total_time_monotonically_increases) {
  Clock c;
  c.tick();
  double t0 = c.totalTime();
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  c.tick();
  EXPECT_GT(c.totalTime(), t0);
}

TEST(Clock, smoothed_delta_falls_back_to_delta_when_empty) {
  Clock c;
  EXPECT_FLOAT_EQ(c.smoothedDeltaTime(), c.deltaTime());
}
```

测试有 sleep 容差，不在 sanitizer / 高负载机器上要求 nanosecond 精度。

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/core/time/clock.hpp` | 新增 |
| `src/core/time/clock.cpp` | 新增 |
| `src/core/CMakeLists.txt` | 把新文件加进 sources |
| `src/test/test_render_triangle.cpp:104` | 主循环里加 `Clock clock; clock.tick();` 演示 |
| `src/test/integration/test_clock.cpp` | 新增 |
| `src/test/integration/CMakeLists.txt` | 注册新测试 |

## 边界与约束

- **不做** fixed step accumulator —— Phase 2 REQ-206
- **不做** timeScale / pause —— Phase 2 REQ-206
- **不做** 高分辨率 (sub-microsecond) 计时 —— `steady_clock` 在主流平台 ≥ µs 已经够 deltaTime 用
- **不做** 帧率上限 / 等待垂直同步控制 —— 那是 swapchain 的事
- 平滑窗口写死 60 帧，不做参数化。Phase 2 再开关

## 依赖

- 无

## 下游

- **REQ-016**：FreeFly 相机的 `position += velocity * deltaTime`
- **REQ-018**：DebugPanel 显示 `1.0f / smoothedDeltaTime()` 作为 FPS
- **REQ-020**：`EngineLoop` 的 `tickFrame()` 需要显式 `clock.tick()`
- **REQ-019**：demo_scene_viewer 将通过 `EngineLoop` 间接消费 `Clock`
- **Phase 2 REQ-206**：在本 REQ 上加 fixed step / timeScale

## 实施状态

未开始。
