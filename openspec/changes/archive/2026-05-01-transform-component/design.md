## Context

当前场景层已经有稳定的 parent/child hierarchy、world 矩阵缓存、dirty 传播和 render queue 对接，但 local 变换仍是 `Mat4f`。这对渲染足够，对编辑器和命令系统不够，因为它们需要稳定、可单字段编辑的 TRS 值语义。仓库已有 `Quatf`，因此本变更不需要引入第三方数学库，重点是把 local 存储和公共接口迁到 `Transform`，同时维持现有 hierarchy/world 缓存行为不回归。

## Goals / Non-Goals

**Goals:**
- 让 `SceneNode` 的 local 变换成为可分解、可编辑、可增量修改的 `Transform` 值类型。
- 保持当前 world 矩阵缓存、dirty 传播、per-draw model 同步、hierarchy 组合行为稳定。
- 清掉 `setLocalTransform(Mat4f)` 长期兼容层，避免继续向上游暴露矩阵直写语义。
- 为历史矩阵迁移提供 `Transform::fromMat4()`，并在近似分解时给出显式 `WARN`。

**Non-Goals:**
- 不改变 dirty 传播算法或 scene ownership 模型。
- 不引入 transform-only scene node、动画插值、SLERP、shear 数据模型。
- 不保证任意输入矩阵经 `fromMat4().toMat4()` 后严格还原。

## Decisions

### 1. `SceneNode` 只保留 `Transform` / TRS setter，删除 Mat4 setter

保留 Mat4 setter 会把“矩阵直写”继续当成一等 API，和 REQ-035 要推动的 TRS 数据模型冲突。一次性迁现有调用点更干净，也能让后续 inspector/命令总线只围绕 `Transform` 设计。

备选方案：
- 保留过渡重载一段时间。放弃，原因是会持续鼓励新代码绕过 TRS 语义。

### 2. world 变换仍缓存为 `Mat4f`

renderer、per-draw data、现有 shader ABI 都直接消费 world 矩阵。把 world 也改成 `Transform` 不仅收益小，还会把非交换 scale / shear 的分解问题重新引进来。local 用 `Transform`，world 继续缓存 `Mat4f`，是最小改动路径。

备选方案：
- 同时存 local/world 两份 `Transform`。放弃，原因是会引入冗余和不安全分解。

### 3. `Transform::fromMat4()` 对非严格 TRS 输入走“近似重构 + WARN”

历史矩阵迁移和少量边界调用点仍需要从 `Mat4f` 回填 TRS。对剪切、负缩放、奇异基底这类输入，严格无损分解不现实；因此设计上接受“视觉合理”的近似结果，同时必须输出 `WARN`，防止调用方误判为无损 round-trip。

备选方案：
- 发现非严格 TRS 直接 fatal。放弃，原因是会让迁移成本过高，也不利于处理历史资产/测试矩阵。
- 静默近似分解。放弃，原因是调试成本高，容易掩盖数据问题。

### 4. capability 级行为落在 `scene-transform-hierarchy` 和 `math-correctness`

场景层行为变化主要是 local/world 变换合同；数学层变化主要是 matrix<->TRS 分解与回归测试。无需单独再引入一个新的 top-level capability。

## Risks / Trade-offs

- [非严格 TRS 输入 round-trip 不相等] → 文档显式声明近似语义，并要求 `WARN` 可观测。
- [删除 Mat4 setter 会带来较大调用点改动] → 在同一变更内完成迁移，并优先用 `setTranslation` / `Transform{...}` 保持代码可读性。
- [极分解/反射修正实现细节出错] → 用 focused math tests 覆盖 identity、纯平移、标准 TRS、负缩放修正、shear 告警。
- [SceneNode API 变化影响后续 REQ] → 统一让后续 REQ 直接消费 `Transform`，减少再次重构。
