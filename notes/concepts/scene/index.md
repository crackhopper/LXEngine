# 场景对象

这篇文档面向引擎使用者，解释 `Scene`、`SceneNode` 和 `ValidatedRenderablePassData` 如何把资源组织成真正可渲染的对象。

## 你会在什么场景接触它

你通常会在两种地方直接碰到场景对象：

- 组一个 demo scene 时，把 mesh、material、camera、light 放进 `Scene`。
- 修改对象结构时，例如切换 mesh、material 或 skeleton，观察它如何影响 pass 校验和渲染结果。

当前主路径里，真正推荐的 renderable 模型是 `SceneNode`，不是旧的 `RenderableSubMesh`。

## 它负责什么

场景对象主要负责：

- 让 `Scene` 持有 renderables、camera、light，并提供 scene-level 资源。
- 让 `SceneNode` 聚合 mesh、material、skeleton、object push constant 等运行时对象。
- 在结构变化时为每个 enabled pass 建立 `ValidatedRenderablePassData` 缓存。
- 把“这个对象能不能参与某个 pass”前移到 scene 前端，而不是在 draw 时临时猜。

它不负责：

- 定义资源文件格式，这属于 [资产系统](../assets/index.md)。
- 定义材质蓝图和运行时参数语义，这属于 [材质系统](../material/index.md)。
- 决定 pipeline 如何缓存和构建，这属于 [渲染管线](../pipeline/index.md)。

## 当前实现状态

- 已实现：`Scene`、`SceneNode`、`ValidatedRenderablePassData`、scene-level camera/light 资源收集、shared material pass-state 传播。
- 已实现：`SceneNode` 会在结构变化时重建 validated cache，并对 enabled passes 做一致性校验。
- 部分实现：旧 `RenderableSubMesh` 兼容抽象仍然存在，但已不是推荐主路径，见 [`REQ-024`](../../requirements/024-remove-renderable-submesh-legacy-abstraction.md)。

## 常见使用方式

最常见的路径是：

1. 创建 mesh、material、可选 skeleton。
2. `SceneNode::create(nodeName, mesh, material, skeleton)`。
3. `scene->addRenderable(node)`。
4. 由 `RenderQueue::buildFromScene(scene, pass, target)` 消费 validated 结果。

这里最重要的一点是：`SceneNode` 不是一个“等 renderer 临时解释”的原始对象，而是一个会对自身结构合法性负责的 renderable。和 pipeline 相关的后续链路，继续看 [渲染管线](../pipeline/index.md)。

## 与其他概念的关系

- 和 [几何系统](../geometry/index.md)：`SceneNode` 会消费 mesh 的 vertex layout 与 topology。
- 和 [材质系统](../material/index.md)：enabled passes、shader reflection、descriptor resources 都从 material 侧进入 scene 校验。
- 和 [相机系统](../camera/index.md) / [光源系统](../light/index.md)：scene 负责收集 scene-level `CameraUBO` / `LightUBO`。
- 和 [渲染管线](../pipeline/index.md)：`ValidatedRenderablePassData` 是 `RenderingItem` 和 `PipelineBuildDesc` 的上游输入。

## 继续阅读

- 使用者视角的 scene 行为：[`../../subsystems/scene.md`](../../subsystems/scene.md)
- pass 组织与 queue 构建：[`../../subsystems/frame-graph.md`](../../subsystems/frame-graph.md)
