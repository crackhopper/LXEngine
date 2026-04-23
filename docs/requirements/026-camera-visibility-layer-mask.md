# REQ-026: Camera Visibility Layer / Layer Mask

## 背景

当前相机系统已经支持按 `RenderTarget` 过滤 camera，但还没有“这台相机能看见哪些对象”的层级过滤能力。对一个完整但仍然小型的渲染引擎来说，camera visibility layer 是概念层必须能讲清楚的能力，否则：

- UI / gizmo / debug object 难以只让特定 camera 看见
- 多 camera scene 难以表达“主视图”和“小地图”看到的对象集合不同
- 相机系统只能按输出 target 过滤，不能按可见层过滤

## 目标

1. 给 renderable 定义稳定的 layer mask。
2. 给 camera 定义可见层过滤规则。
3. 让 queue 构建在进入 draw 前完成 layer 过滤。

## 需求

### R1: renderable 必须持有 layer mask

- `SceneNode` 或等价 renderable 主路径需要持有一个 layer mask 字段。
- 默认值必须代表“默认层”，且默认 camera 可以看见它。

### R2: camera 必须持有 culling mask

- `Camera` 需要持有一个可见层掩码。
- 一个 renderable 只有在 `(renderable.layerMask & camera.cullingMask) != 0` 时才被该 camera 看到。

### R3: 过滤发生在 queue 构建阶段

- `RenderQueue::buildFromScene(scene, pass, target)` 在确认 target 命中的 camera 后，需要额外应用 layer 过滤。
- layer 过滤不改变 `PipelineKey`，它只影响某个 camera/target 下的 draw 候选集。

### R4: scene-level camera 资源收集与可见层分开处理

- `CameraUBO` 仍按 target 过滤收集。
- layer mask 作用于 renderable 候选集，不作用于 `CameraUBO` 自身是否进入 scene-level resources。

### R5: 文档和示例必须覆盖多 camera 场景

至少包含一个示例：

- 主 camera 看默认层 + 调试层
- 次 camera 只看默认层

## 修改范围

- `src/core/scene/camera.hpp`
- `src/core/scene/object.hpp`
- `src/core/frame_graph/render_queue.*`
- `notes/concepts/camera/`
- `notes/concepts/scene/`

## 依赖

- 已有的 `RenderTarget` / multi-camera 过滤基础
- `notes/roadmaps/main-roadmap/phase-5-physics.md` 中已有 layer mask 的相关长期方向

## 实施状态

2026-04-16 核查结果：未开始。

- `Camera` 尚无 culling mask
- renderable 尚无 layer mask
- queue 构建阶段也还没有 layer 过滤

本次核查后，剩余工作统一并入 [`REQ-034`](034-remaining-validated-backlog.md)。
