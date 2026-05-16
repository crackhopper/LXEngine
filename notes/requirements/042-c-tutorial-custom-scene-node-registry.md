# REQ-042-c: 教程支撑 — 自定义场景节点类型注册入口

## 背景

当前 `SceneNode` 已经具备 transform、path、component、picking、DebugDraw helper、rename、duplicate、scene document capture/load 等基础。我们可以教学“一个节点要兼容编辑器操作，需要同时满足运行时、文档、命令和可视化合同”。

但新增一种节点语义仍然是手工接线：scene document 字段、runtime 构建、Inspector、CommandBus、DebugDraw、picking bounds、duplicate 语义都需要分散修改。教程可以讲清楚这些触点，但如果要让新人真正扩展节点，需要一个注册入口。

## 目标

1. 定义 custom scene node kind 的注册模型。
2. 让节点 kind 声明保存格式、runtime 构建、Inspector 字段、debug bounds/helper 和 duplicate 行为。
3. 让已有 primitive / camera / light 节点能逐步迁移到同一解释模型。

## 需求

### R1: Node kind metadata

每种节点 kind 声明：

| 字段 | 含义 |
|---|---|
| `kind` | scene document 中的稳定名字 |
| `displayName` | editor 显示名 |
| `components` | runtime 需要挂载的 component 类型 |
| `documentPayload` | 保存到 `.scene.yaml` 的负载 schema |
| `debugDraw` | 可选 debug helper |
| `boundsPolicy` | picking / selection bounds 来源 |
| `duplicatePolicy` | duplicate 时复制哪些 payload |

### R2: Scene document 使用 kind-aware payload

新增节点 kind 时，不应要求直接给 `SceneNodeDocument` 加一组专属字段。文档层需要能保存 kind-specific payload，并在加载时交给 registry 解释。

### R3: Runtime 构建通过 registry

`SceneRuntime` 构建节点时先识别 kind，再调用对应 factory。factory 返回 runtime node、component 和附加 scene-level 资源。

### R4: Editor 操作兼容

自定义节点必须兼容：

- select / deselect
- move / rotate / scale
- rename
- duplicate / copy / paste
- remove
- scene save / load
- DebugDraw helper
- API state summary

### R5: 测试覆盖

覆盖：

- 自定义节点 round-trip。
- duplicate 后 payload 独立。
- debug bounds 可用于 picking。
- 未知 kind 产生可诊断错误。

## 修改范围

- `src/demos/lxe_editor/scene_document.*`
- `src/demos/lxe_editor/scene_runtime.*`
- `src/core/editor/commands/builtin_commands.*`
- `src/core/editor/inspector_panel.*`
- `src/core/editor/scene_tree_panel.*`
- `src/demos/lxe_editor/scene_interaction_controller.*`
- 相关 tests

## 边界与约束

- 本 REQ 不引入完整 ECS。
- 本 REQ 不要求脚本定义节点类型。
- 本 REQ 不改变 `SceneNode` 的 transform/path 基础语义。

## 依赖

- `REQ-037-a`
- `REQ-038-a`
- `REQ-041-f`

## 后续工作

- 脚本化 node kind。
- asset-driven node prefab。

## 实施状态

未开始。当前仅作为教程中“未来顺滑工作流”的支撑需求。
