# finished 需求复核后的存疑项

本文记录 2026-05-13 清理 `notes/requirements/finished/` 时发现的“标记 finished，但完成度仍有疑问，且仍有实现价值”的需求点。

## REQ-039-a DebugDraw 调用线程安全

结论：DebugDraw 主功能已经落地，但原需求 R5 中“UI 线程与 render 线程都能调用”的线程安全承诺没有在当前代码中得到证实。

代码证据：

- `src/core/debug_draw/debug_draw.cpp` 使用一个函数内静态 `State` 作为全局状态。
- `State` 内部包含 `std::unordered_map<VisibilityLayerMask, std::vector<DebugLineVertex>> queuedVertices`、`buckets`、`currentMask`、`acceptedLines` 等可变字段。
- `DebugDraw::drawLine` / `beginFrame` / `endFrame` / `LayerScope` 都会读写这些全局字段。
- `src/core/debug_draw/` 中没有 `std::mutex` / `std::lock_guard` / `std::scoped_lock` 等同步保护。
- `src/test/integration/test_debug_draw.cpp` 覆盖了 geometry、capacity、flush、limit 等行为，但没有并发调用测试。

建议后续二选一：

| 方向 | 需要做的事 |
|---|---|
| 明确单线程合同 | 修改 DebugDraw 文档和 API 注释，声明所有 `DebugDraw::*` 调用必须发生在同一 frame/update 线程；删除 finished 需求中“UI 线程与 render 线程都能调用”的承诺。 |
| 补齐线程安全 | 给 DebugDraw 全局状态加同步策略，明确 `beginFrame` / `endFrame` 与 draw 调用的并发边界，并增加多线程提交测试。 |

当前推荐：先明确单线程合同。现有 engine/editor 路径看起来按 frame 内顺序调用 DebugDraw，补锁会引入提交顺序、frame 边界和性能语义，需要单独设计。
