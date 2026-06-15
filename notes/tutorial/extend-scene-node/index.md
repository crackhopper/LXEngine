# 扩展场景节点：给舞台增加一种新道具

`SceneNode` 像舞台上的道具：它有名字、位置、层级，也可能带 mesh、材质、camera 或 light 这样的能力。这个系列教我们理解当前节点的共同操作，以及新增一种节点语义时需要手工兼容哪些 editor 合同。

## 新增节点 kind 为什么会牵动整套 editor

新增节点不是只加一个 C++ 类型。只要它要进入 editor，就要能保存、加载、选择、移动、复制、删除、debug draw，还要能被 API 状态看见。这个系列先把 `SceneNode` 的通用底座讲清楚，再列出当前手工触点。

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
| [05 节点操作合同](05-node-operation-contract.md) | 当前新增节点语义时必须保持哪些 editor 操作一致 |

## 当前边界

| 路径 | 状态 | 说明 |
|---|---|---|
| 操作现有节点 | 当前可用 | select、move、rename、copy、paste、remove、save/load 已有路径 |
| 手工扩展一种节点语义 | 当前可讲原理 | 需要同步 scene document、runtime、Inspector、command、debug draw |
| custom node kind registry | 不在当前教程主线 | 当前没有稳定 metadata 注册 API，教程只讲现有手工路径 |

我们先学当前节点操作，是为了避免把“新增一个配置文件”误当成扩展节点的全部成本。真正要维护的是 editor 对节点的一组操作合同。

## 完成后我们能判断什么

这个系列会把“节点本身”和“节点携带的能力”分开讲。节点给我们统一操作面，能力决定它能渲染、发光、当相机，或执行未来自定义行为。

## 下一步

进入 [01 SceneNode 心智模型](01-scene-node-model.md)。
