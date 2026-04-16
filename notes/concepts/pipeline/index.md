# 渲染管线

这篇文档面向引擎使用者，解释 pipeline 身份、pipeline 构建输入，以及它们如何从 scene 前端一路流到 backend。

当前概念区里凡是提到 `PipelineKey`、render signature、pipeline cache、`PipelineBuildDesc` 这些词，默认都应该索引到这篇文章来理解。

## 你会在什么场景接触它

你通常会在三种场景下直接碰到渲染管线：

- 改了 mesh layout、material pass 或 shader variant，发现 pipeline 被重新构建。
- 想知道为什么两个对象能复用同一条 pipeline，或者为什么它们必须分开。
- 调试 preload / cache miss / runtime build 时，想看 pipeline 身份到底是怎么来的。

## 它负责什么

当前项目里的“渲染管线”概念主要覆盖两件事：

- **pipeline 身份**：系统如何判断两个 draw 是否可以复用同一条 pipeline。
- **pipeline 构建输入**：系统如何从 renderable / material / shader reflection 里整理出 backend 真正需要的构建描述。

它不负责：

- 维护 mesh 或 material 的运行时状态本身。
- 决定 scene 里有哪些对象，这属于 [场景对象](../scene/index.md)。
- 执行 Vulkan 命令录制细节，那属于 backend 实现。

## 当前实现状态

- 已实现：`getRenderSignature()`、`PipelineKey`、`ValidatedRenderablePassData`、`PipelineBuildDesc::fromRenderingItem(...)`、`PipelineCache`。
- 已实现：`RenderQueue` 不再现场推断 pipeline 输入，而是消费前端已经验证过的 renderable 数据。
- 部分实现：少数兼容接口仍保留 Forward-only 过渡语义，但主路径已经是 pass-aware 的 scene -> queue -> pipeline build 链路。

## 核心原理

这条链路可以按 5 步理解：

1. [几何系统](../geometry/index.md) 提供 object-side render signature。
2. [材质系统](../material/index.md) 提供 material-side render signature，以及每个 pass 的 shader / render state。
3. [场景对象](../scene/index.md) 在 `SceneNode::rebuildValidatedCache()` 里对 enabled passes 做结构校验，并把两侧 signature 组合成 `PipelineKey`。
4. `RenderQueue::buildFromScene(scene, pass, target)` 从 validated 数据生成 `RenderingItem`。
5. `PipelineBuildDesc::fromRenderingItem(item)` 提取 backend 真正需要的顶点输入、shader stages、descriptor bindings、render state，再交给 cache / backend 去复用或构建。

这意味着：

- `PipelineKey` 负责回答“是不是同一条 pipeline”。
- `PipelineBuildDesc` 负责回答“如果要建，具体该怎么建”。

## 常见误解

### 误解 1：light 或 camera 会切 pipeline

一般不会。camera 和 light 是 scene-level 资源，更多影响 descriptor 装配和 draw 输入，而不是 `PipelineKey` 本身。真正会稳定切 key 的，仍然是 mesh layout、shader variants、render state、topology 这些结构性因素。

### 误解 2：改 material 参数就一定重建 pipeline

不是。改 `setFloat` / `setTexture` / `updateUBO()` 只是值变化，不是结构变化；普通参数更新通常不会改变 `PipelineKey`。真正会触发结构重算的是 pass enable、shader set、variants、render state 这类变化。

### 误解 3：pipeline 是 backend 才开始关心的东西

不是。backend 负责执行真正的构建，但 pipeline 身份和输入整理早在 core 层 scene / queue 阶段就已经确定了。

## 与其他概念的关系

- 和 [几何系统](../geometry/index.md)：几何系统提供 vertex layout / topology，也就是 object-side identity 的主要来源。
- 和 [材质系统](../material/index.md)：材质系统提供 pass 级 shader、render state、variant，也就是 material-side identity 的主要来源。
- 和 [场景对象](../scene/index.md)：`ValidatedRenderablePassData` 是 pipeline 链路的前端稳定输入包。
- 和 [相机系统](../camera/index.md) / [光源系统](../light/index.md)：两者主要影响 scene-level descriptor 资源，不是 `PipelineKey` 的核心来源。

## 继续阅读

- pipeline 身份组成：[`../../subsystems/pipeline-identity.md`](../../subsystems/pipeline-identity.md)
- pipeline 预构建与运行时 miss：[`../../subsystems/pipeline-cache.md`](../../subsystems/pipeline-cache.md)
- scene 到 queue 的装配路径：[`../../subsystems/frame-graph.md`](../../subsystems/frame-graph.md)
