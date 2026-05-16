# 扩展场景节点：给舞台增加一种新道具

`SceneNode` 像舞台上的道具：它有名字、位置、层级，也可能带 mesh、材质、camera 或 light 这样的能力。这个系列先教我们理解当前节点的共同操作，再讲未来如何让一种自定义节点 kind 自动兼容选择、移动、复制、保存、debug draw 和 API 状态。

## 新增节点 kind 为什么会牵动整套 editor

新增节点不是只加一个 C++ 类型。只要它要进入 editor，就要能保存、加载、选择、移动、复制、删除、debug draw，还要能被 API 状态看见。这个系列先把 `SceneNode` 的通用底座讲清楚，再说明未来 node kind registry 应该把哪些规则集中声明。

## 当前手工路径暴露出的重复劳动

| 触点 | 新节点需要提供什么 |
|---|---|
| scene document | kind-specific payload 的保存形状 |
| runtime factory | 从 payload 构建 runtime node |
| Inspector / command | 编辑 payload 的入口 |
| DebugDraw / picking | 看得见、点得到 |
| duplicate / copy-paste | payload 复制语义 |
| API summary | 远程诊断能识别节点 kind |

## 当前可实践章节

| 章节 | 我们学什么 |
|---|---|
| [01 SceneNode 心智模型](01-scene-node-model.md) | 节点、组件、transform、path 的关系 |
| [02 当前节点如何保存和重建](02-document-and-runtime.md) | scene document 到 runtime node 的数据流 |
| [03 新增节点 kind 的当前触点](03-current-custom-node-touchpoints.md) | 手工扩展要同步哪些模块 |
| [04 Debug draw 与 picking](04-debug-draw-and-picking.md) | 新节点怎样被看见、选中和诊断 |
| [05 未来节点注册表](05-future-node-kind-registry.md) | kind metadata 如何统一 editor 操作 |

## 未来 registry 章节为什么放在最后

| 路径 | 状态 | 说明 |
|---|---|---|
| 操作现有节点 | 当前可用 | select、move、rename、copy、paste、remove、save/load 已有路径 |
| 手工扩展一种节点语义 | 当前可讲原理 | 需要同步 scene document、runtime、Inspector、command、debug draw |
| custom node kind registry | 未来能力 | 由 [REQ-042-c](../../requirements/042-c-tutorial-custom-scene-node-registry.md) 跟踪 |

我们先学当前节点操作，是为了理解 registry 不是“新增一个配置文件”，而是把 editor 已有操作需要的规则集中起来。

## 完成后我们能判断什么

这个系列会把“节点本身”和“节点携带的能力”分开讲。节点给我们统一操作面，能力决定它能渲染、发光、当相机，或执行未来自定义行为。

## 下一步

进入 [01 SceneNode 心智模型](01-scene-node-model.md)。
