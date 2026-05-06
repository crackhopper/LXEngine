# REQ-041-i: DebugDraw v2 — persistent draw + 整 mesh 线框一行调用

> 拆分自 2026-05-06 整理：原 [REQ-039-a](039-a-debug-draw-subsystem.md) v1 取"每帧瞬时 draw + 几何原语 only"最小子集，把 persistent draw 与 `wireMesh` 显式留给 v2。本 REQ 收口 v2 路径。

## 背景

[REQ-039-a](039-a-debug-draw-subsystem.md) 的 DebugDraw v1 把 `drawLine / wireSphere / wireBox / frustum / cone / arrow / axis` 一行调用做出来了，但所有 draw 命令都只活当前帧。两类常见编辑器 / 调试场景这个语义不够：

1. **跨帧停留的命中线**：picking 命中 ray、AS（[Phase 5 物理](../roadmaps/main-roadmap/phase-5-physics.md)）射线、shadow cascade 边界 / BVH 调试线，常希望"画一次然后在场景里停 5 秒 / 永久 / 直到下一次清屏"，方便调相机绕一圈观察
2. **整 mesh 线框**：debug 选中物体或者比对 culling 错误时希望一行 `DebugDraw::wireMesh(node)` 把整个 mesh 的三角面线框画出来；v1 文档建议"用户手动遍历三角面调 `drawTriangle`"，但 [REQ-038-b](041-h-mesh-level-triangle-picking.md) 落地后 mesh 已带 CPU vertex / index，封装一行就是顺手事

本 REQ 在 v1 单一 line topology pipeline 之上加 lifetime / 整 mesh 两个能力，**不**重构 v1 的渲染主路径，**不**引入新 pipeline。

## 目标

1. `DebugDraw` 公开 `lifetime` 概念：每条 draw 命令都可指定 "frame" / "duration: float seconds" / "persistent until clear"
2. 一行调用 `wireMesh(SceneNode &)` 与 `wireMesh(const Mesh &, const Mat4f &world)` 把整 mesh 的三角面以线框形式累计
3. 提供 `clearPersistent()` 命令显式清空 persistent 队列（编辑器命令 + ImGui 按钮）
4. v1 主路径（每帧瞬时 draw）行为完全不变；v2 是叠加，不破坏 v1 的"调用即画即丢"心智

## 需求

### R1: 带 lifetime 的 draw 队列

`src/core/debug_draw/debug_draw.hpp` 升级公共 API：

```cpp
struct Lifetime {
  enum class Kind { Frame, Duration, Persistent };
  Kind  kind = Kind::Frame;
  float seconds = 0.0f;          // 仅 Duration 时使用
};

class DebugDraw {
 public:
  // v1 既有 API：默认 Lifetime::Frame
  void drawLine(Vec3f a, Vec3f b, Color c);

  // 显式 lifetime 版本（重载，不改 v1 调用点）
  void drawLine(Vec3f a, Vec3f b, Color c, Lifetime life);

  // wireSphere / wireBox / frustum / cone / arrow / axis 同样加 Lifetime 重载

  void clearPersistent();
};
```

- 内部存储分两段：`m_frameLines`（每帧 begin 时清）与 `m_persistentLines`（带剩余 lifetime；duration 类型每帧扣秒，<= 0 时移除；persistent 类型仅 `clearPersistent()` 移除）
- 渲染时把两段拼成一个 vertex buffer 提交一次（line topology pipeline 不变）

### R2: `wireMesh`

```cpp
class DebugDraw {
 public:
  void wireMesh(const Mesh &mesh, const Mat4f &world, Color c,
                Lifetime life = {});
  void wireMesh(const SceneNode &node, Color c,
                Lifetime life = {});             // 内部：MeshComponent + worldTransform
};
```

- 实现：遍历 `mesh.cpuIndices`（3 个一组），把每个三角面的 3 条边 `(p0,p1) (p1,p2) (p2,p0)` push 到对应 lifetime 队列
- 边去重：v2 **不**做（同一条边可能被两个三角面 push 两次，画两遍 — 视觉无差异，省 CPU）；如未来发现性能瓶颈再加哈希去重
- mesh 没有 `cpuIndices`（当前老 mesh / 加载流程未升级到 [REQ-038-b](041-h-mesh-level-triangle-picking.md)）→ 跳过并打 warning，**不** fallback 到只画 wireBox（避免视觉欺骗）

### R3: 编辑器 / 命令总线集成

- 命令总线 `debug clear` → 调 `DebugDraw::clearPersistent()`，由 [REQ-040-a](040-a-editor-command-bus.md) 的 `debug` verb 注册
- ImGui 控制台面板加一个"Clear Persistent"按钮，对应 `debug clear` 命令
- [REQ-038-b](041-h-mesh-level-triangle-picking.md) 落地后，picking 命中线可以默认 5s duration（点完一目了然），不在本 REQ 强制；只是把 API 备好

### R4: 测试覆盖

`src/test/integration/test_debug_draw_v2.cpp`（新）：

- 默认 lifetime（Frame）的 draw 在 `endFrame` 后被清；下一帧渲染顶点数减少
- duration = 2s 的 draw 在 1s 后仍存在；3s 后被清
- persistent 的 draw 在多帧后仍存在；`clearPersistent()` 后被清
- `wireMesh(node)` 在三角形 mesh 上画出 3 倍三角面数的边对（不去重的预期）
- v1 主 API（不带 Lifetime 参数）在 v2 加成熟度后行为完全不变

## 修改范围

- `src/core/debug_draw/debug_draw.hpp` / `.cpp`（加 `Lifetime` + `clearPersistent` + `wireMesh` 两份重载；保留 v1 单参数 API）
- `src/core/editor/commands/debug.cpp`（新或扩展，注册 `debug clear`）
- `src/core/editor/console_panel.cpp`（一个按钮）
- `src/test/integration/test_debug_draw_v2.cpp`（新）

## 边界与约束

- v2 **不**新增 pipeline / shader：仍是 v1 的 `LineList` topology 单一 pipeline
- v2 **不**做线宽（device feature `wideLines` 的接入仍延后）
- v2 **不**做去重 / instancing；wireMesh 的边按"每三角 3 条"朴素 push
- v2 **不**做 screen-space 字体（`drawText`）；与 v1 边界一致
- persistent / duration 类型的 draw 在场景 reload / scene 切换时**自动**清空（具体接入 `Scene` 生命周期事件，避免上一场景的线遗留到下一场景）
- 帧率累加：duration 用每帧 deltaTime（[REQ-014 Clock](finished/014-clock-and-delta-time.md)）扣秒；用 wall clock 不要用 frame count

## 依赖

- [REQ-039-a DebugDraw 子系统](039-a-debug-draw-subsystem.md) — line topology pipeline + 帧 begin/end 框架已就位
- [REQ-038-b mesh 三角面级 picking](041-h-mesh-level-triangle-picking.md) — `wireMesh` 需要 `Mesh::cpuPositions / cpuIndices`（038-b R1）
- [REQ-040-a 编辑器命令总线](040-a-editor-command-bus.md) — `debug clear` 命令注册路径
- [REQ-014 Clock + deltaTime](finished/014-clock-and-delta-time.md) — duration 扣秒

## 后续工作

- 边去重 / instancing 优化：等真出现 wireMesh 性能瓶颈再立项
- `drawText` screen-space 字体：等编辑器 / 调试有强需求再立项（与 ImGui overlay 边界要先讨论清）
- BVH / shadow cascade / AS 命中点的可视化都直接消费本 REQ 的 lifetime API，不需要再扩 DebugDraw

## 实施状态

待实施。立项窗口：[REQ-038-b](041-h-mesh-level-triangle-picking.md) 落地后开工（`wireMesh` 依赖它的 CPU mesh 数据）。整体优先级低于 Phase 1 / Phase 2 主线；编辑器在 v1 DebugDraw 上已经够用，本 REQ 是 polish。
