# 资源怎样成为场景里的对象

这篇文档关心的不是抽象的 scene graph 理论，而是当前项目里的一个更实际的问题：mesh、material、skeleton 这些资源，是怎样被组织成真正可渲染对象的。

## 当前主路径里的对象模型

当前主路径里的场景对象模型，核心就是两层：

- `Scene`
- `SceneNode`

`Scene` 是容器，负责持有 renderables、camera、light，以及 scene-level 资源。

`SceneNode` 是对象，负责把 mesh、material、可选 skeleton、push constant 等内容组合起来，形成一个真正能参与渲染的 renderable。

所以，这里讲的“场景对象”，本质上是在讲当前这套运行时对象模型：资源怎样被组织、什么时候被校验、怎样进入后续 draw 路径。

## 这套系统解决什么问题

场景对象系统解决的是“资源如何变成 draw 前的稳定输入”这个问题。

如果没有这一层，renderer 每帧都要临时猜：

- 这个对象能不能跑某个 pass
- 它缺不缺 shader 需要的输入
- 它该带哪些 descriptor 资源

现在这些问题被前移到了 scene 前端。`SceneNode` 会在结构变化时重建自己的 pass 级缓存，而不是把这些判断拖到 draw 当场。

## 日常使用里的主路径

最常见的路径是：

1. 准备好 mesh、material、可选 skeleton
2. `SceneNode::create(nodeName, mesh, material, skeleton)`
3. `scene->addRenderable(node)`
4. 之后由 queue / renderer 消费它的 validated 结果

这里最重要的一点是：

`SceneNode` 不是一个“保存原始数据等 renderer 去解释”的壳。它会对自己的结构合法性负责。

## 当前代码已经走到哪一步

现在这条主路径已经比较稳定：

- `Scene`
- `SceneNode`
- `ValidatedRenderablePassData`
- scene-level camera / light 资源收集
- shared material pass-state 传播

这些都已经接起来了。

当前推荐的对象模型是 `SceneNode`。旧的 `RenderableSubMesh` 兼容层已移除，相关历史背景只保留在归档需求与评审记录中。

## 这套系统怎样和其他系统汇合

场景对象本身不生产资源，它消费的是别的系统给出的运行时对象：

- [资产系统](../assets/index.md) 提供 mesh、texture、skeleton 等资源入口
- [材质系统](../material/index.md) 提供 `MaterialInstance` 和 pass 语义
- [相机系统](../camera/index.md) / [光源系统](../light/index.md) 提供 scene-level 资源
- [材质系统里的 Pipeline 说明](../material/what-is-pipeline.md) 再把这些结果往后整理成 `RenderingItem` 和 pipeline 构建输入

## 往实现层再走一步

底层最关键的一步是 `SceneNode::rebuildValidatedCache()`。

它会在结构变化时，按当前 enabled passes 去检查：

- mesh / material / skeleton 是否存在
- shader 和 pass entry 是否有效
- vertex input 是否匹配
- descriptor 资源是否齐全

最后得到 `ValidatedRenderablePassData`，供 queue 和 pipeline 链路继续使用。

继续展开时，可以参考：

- [`../../subsystems/scene.md`](../../subsystems/scene.md)
- [`../../subsystems/frame-graph.md`](../../subsystems/frame-graph.md)
