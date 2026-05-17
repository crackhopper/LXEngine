# REQ-042-a: 教程支撑 — 光源资产与自定义光源注册入口

> 2026-05-17：本需求已从 active 队列移入 pending。v0.1.1 active 队列先收敛到 FrameGraph v1、Directional Shadow、CSM、教程支撑和架构文档展开；本扩展注册模型等待 v0.1.1 完成后重新排序。

## 背景

`v0.1.0` 已经具备 `DirectionalLight`、`PointLight`、`SpotLight`、`SceneLightsUBO`、`SceneDocument.light.kind` 与 `lxe_editor` 作者入口。我们已经能教新人如何使用现有三类光源，也能解释新增 C++ 光源大致要碰哪些模块。

但如果教程要把“自定义灯光”讲成一条顺滑路径，当前代码仍然过于手工：新 light kind 需要同时改 core light 类型、scene document、scene runtime、Inspector、CommandBus、DebugDraw helper 和 shader binding 合同。教程可以讲原理，但不能把这种手工串改伪装成稳定扩展 API。

## 目标

1. 给教程提供一个可教学的 light preset / custom light 注册模型。
2. 让 `.scene.yaml` 或未来 light asset 能引用稳定的 light kind。
3. 让 editor 能通过注册表发现 light 类型、默认参数、Inspector 字段和 debug helper。
4. 保持现有 Directional / Point / Spot 行为不回归。

## 需求

### R1: Light kind 注册表

新增 light kind registry，至少能声明：

| 字段 | 含义 |
|---|---|
| `kind` | scene document / asset 中使用的稳定名字 |
| `displayName` | editor 显示名 |
| `defaults` | 创建新 light 时的默认参数 |
| `inspectorFields` | Inspector 可编辑字段 |
| `debugShape` | editor helper 使用的可视化形状 |

### R2: Scene document 与 registry 对齐

`SceneNodeDocument.light.kind` 读取时应通过 registry 校验。未知 kind 应给出稳定错误，错误中包含 scene path、kind 和可用 kind 列表。

### R3: Light preset asset 最小形状

定义 light preset YAML 的最小形状，例如：

```yaml
kind: Spot
color: [1.0, 0.95, 0.8]
intensity: 3.0
range: 8.0
innerConeDegrees: 20.0
outerConeDegrees: 35.0
```

### R4: Editor 创建入口复用 registry

Toolbar / CommandBus 创建 light 时不再硬编码三类光源列表，而是从 registry 读取可创建类型。

### R5: 测试覆盖

覆盖：

- 已注册 kind 可创建、保存、重新加载。
- 未知 kind 返回可诊断错误。
- registry 中的 Inspector 字段能生成编辑入口。
- Directional / Point / Spot 旧场景 round-trip 不回归。

## 修改范围

- `src/core/scene/light.*`
- `src/demos/lxe_editor/scene_document.*`
- `src/demos/lxe_editor/scene_runtime.*`
- `src/core/editor/commands/builtin_commands.*`
- `src/core/editor/inspector_panel.*`
- `src/demos/lxe_editor/ui_overlay.*`
- 相关 tests

## 边界与约束

- 本 REQ 不实现新的光照公式。
- 本 REQ 不要求热加载 light preset。
- 本 REQ 不改变现有 `SceneLightsUBO` 上限。

## 依赖

- `REQ-041-g` 已完成的多类型光源底座。

## 后续工作

- 自定义 shader 对新 light kind 的消费合同。
- light preset 热重载。

## 实施状态

Pending，未开始。当前仅作为教程中“未来顺滑工作流”的后续支撑需求。
