# REQ-014: Clock 收尾与平滑 deltaTime

## 背景

`Clock` 已经不是一个从零开始的新需求了。当前仓库里，基础时间推进能力已经存在，但还没有收尾到足以支撑调试面板和后续相机控制器的程度。

2026-04-16 按当前代码核查：

- [src/core/time/clock.hpp](../../src/core/time/clock.hpp) 与 [src/core/time/clock.cpp](../../src/core/time/clock.cpp) 已存在
- `tick()`、`deltaTime()`、`totalTime()`、`frameCount()` 已实现
- [src/core/gpu/engine_loop.cpp](../../src/core/gpu/engine_loop.cpp) 已在 `tickFrame()` 中调用 `m_clock.tick()`
- `Clock` 还没有 `smoothedDeltaTime()`
- 仓库里还没有专门覆盖 `Clock` 行为的集成测试

这意味着：

- `REQ-016` 的 FreeFly 相机已经有 `deltaTime()` 前置能力
- `REQ-019` 通过 `EngineLoop::getClock()` 也已经有基础时间源
- 但 `REQ-018` 的调试统计面板还缺一个更稳定的 FPS 输入
- 当前 `Clock` 的行为也缺少单独测试保护

[Phase 2 REQ-206](../../notes/roadmaps/main-roadmap/phase-2-foundation-layer.md) 规划了更完整的时间系统（fixed step accumulator、timeScale、pause 等）。本 REQ 只做当前 `Clock` 的收尾：补平滑 deltaTime 和测试，不扩展到 Phase 2 的完整时间管理能力。

## 目标

1. 保留现有 `Clock` 的最小契约：`tick()` / `deltaTime()` / `totalTime()` / `frameCount()`
2. 为 `Clock` 补 `smoothedDeltaTime()`，供 FPS / 调试统计使用
3. 为 `Clock` 补独立测试，固定当前行为
4. 不重复引入新的手写主循环接线；正式时间推进入口仍以 `EngineLoop` 为准

## 需求

### R1: 维持现有 `Clock` 契约

`src/core/time/clock.hpp/.cpp` 保持现有最小接口：

```cpp
class Clock {
public:
  void tick();

  float deltaTime() const;
  double totalTime() const;
  uint64_t frameCount() const;
};
```

现有行为约束继续成立：

- 第一次 `tick()`：
  - `deltaTime() == 0`
  - `totalTime() == 0`
  - `frameCount() == 0`
- 后续 `tick()`：
  - `deltaTime()` 为当前 tick 与上一次 tick 的时间差
  - `totalTime()` 为自首次 tick 起累计的真实时长
  - `frameCount()` 每次后续 tick 自增 1

本 REQ 不改变这些已存在语义。

### R2: 新增 `smoothedDeltaTime()`

在现有 `Clock` 上新增：

```cpp
float smoothedDeltaTime() const;
```

用途：

- 给调试统计或 FPS 显示提供一个比单帧 `deltaTime()` 更稳定的读数

实现约束：

- 用固定窗口的滑动平均实现
- 窗口大小固定为 `60` 帧
- 当样本数为 `0` 时，返回当前 `deltaTime()`
- 当样本数不足 `60` 时，按已有样本平均

允许的内部状态扩展包括：

- 固定大小的 recent delta 环形缓冲
- 已填充样本数
- 当前写入游标

### R3: `tick()` 写入平滑窗口

`tick()` 在保持现有行为不变的前提下，需要额外把后续帧的 `deltaTime()` 写入平滑窗口。

约束：

- 第一次 `tick()` 不写入平滑窗口
- 从第二次 `tick()` 开始，将 `deltaTime()` 写入 recent delta 环形缓冲
- `smoothedDeltaTime()` 只统计实际写入过的样本

### R4: 正式接线以 `EngineLoop` 为准

本 REQ 不再要求修改 `src/test/test_render_triangle.cpp` 去演示一个展开式 while-loop。

原因：

- 当前 [src/core/gpu/engine_loop.cpp](../../src/core/gpu/engine_loop.cpp) 已经是正式的运行时入口
- `EngineLoop::tickFrame()` 已按正确顺序先调 `m_clock.tick()`
- 再回到手写 while-loop 只会让文档同时维护两套入口

因此，本 REQ 的正式接线要求是：

- 继续由 `EngineLoop` 持有和推进 `Clock`
- `EngineLoop::getClock()` 继续作为上层调试 UI、demo 和 update hook 的读取入口

### R5: 集成测试

新增 `src/test/integration/test_clock.cpp`，至少覆盖：

- `first_tick_has_zero_delta`
  - 首次 `tick()` 后 `deltaTime() == 0`
  - 首次 `tick()` 后 `frameCount() == 0`
- `second_tick_has_nonzero_delta`
  - 两次 `tick()` 之间 sleep 少量时间
  - 第二次 `tick()` 后 `deltaTime() > 0`
  - `frameCount() == 1`
- `total_time_monotonically_increases`
  - 后续 `tick()` 后 `totalTime()` 大于第一次记录值
- `smoothed_delta_falls_back_to_delta_when_empty`
  - 尚无样本时 `smoothedDeltaTime() == deltaTime()`
- `smoothed_delta_averages_recent_samples`
  - 连续产生若干样本后，`smoothedDeltaTime()` 为 recent delta 的平均值

测试约束：

- 允许用 `sleep_for` 做粗粒度时间验证
- 不要求高精度到纳秒级
- 重点验证单调性、非负性和统计语义

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/core/time/clock.hpp` | 补 `smoothedDeltaTime()` 声明和内部平滑窗口状态 |
| `src/core/time/clock.cpp` | 补平滑窗口写入与 `smoothedDeltaTime()` 实现 |
| `src/core/CMakeLists.txt` | 如有需要，确保 `clock.cpp` 已在 sources 中 |
| `src/test/integration/test_clock.cpp` | 新增 |
| `src/test/CMakeLists.txt` | 注册新测试 |

## 边界与约束

- 不做 fixed step accumulator
- 不做 timeScale / pause
- 不做帧率限制
- 不做高精度 profiler
- 不重构 `EngineLoop`
- 不回退到手写 while-loop 作为正式入口

## 依赖

- 无

## 下游

- `REQ-016`：FreeFly 相机继续依赖 `deltaTime()`
- `REQ-018`：调试统计面板用 `smoothedDeltaTime()` 显示更稳定的 FPS
- `REQ-019`：demo_scene_viewer 通过 `EngineLoop::getClock()` 读取时间信息
- Phase 2 更完整时间系统：在当前 `Clock` 基础上扩 fixed step / timeScale / pause

## 实施状态

2026-04-16 实施完成。

- `Clock` 类型已存在，`tick()` / `deltaTime()` / `totalTime()` / `frameCount()` 已实现
- `EngineLoop::tickFrame()` 已推进 `Clock`
- `smoothedDeltaTime()` 已实现（60 帧滑动平均环形缓冲）
- `test_clock` 集成测试已通过（5 个测试场景）
