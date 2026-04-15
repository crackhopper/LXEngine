# Frame Graph

> `FrameGraph` 描述场景初始化后的一帧渲染结构。它本身不录制命令，而是把 `Scene` 按 pass 展开成多个 `RenderQueue`，并提前汇总 pipeline 预构建输入。
>
> 权威 spec: `openspec/specs/frame-graph/spec.md`

## 它解决什么问题

- 明确当前 scene 会产出哪些 pass。
- 统一 `RenderingItem` 的构造入口。
- 在真正 draw 前就把所有 `PipelineBuildDesc` 收集出来。

## 核心对象

- `FramePass`：`{name, target, queue}`。
- `FrameGraph`：持有多个 `FramePass`。
- `RenderQueue`：按某个 pass 收集、排序并去重 `RenderingItem`。
- `RenderTarget`：pass 输出目标的 core 侧描述，当前字段是 `colorFormat`、`depthFormat`、`sampleCount`。

## 场景启动期的数据流

1. `VulkanRenderer::initScene(scene)`
2. renderer 准备好某个 `RenderTarget`
3. `m_frameGraph.addPass(FramePass{passName, target, {}})`
4. `m_frameGraph.buildFromScene(scene)`
5. `m_frameGraph.collectAllPipelineBuildDescs()`
6. backend preload pipelines

构建完成后，每帧 `draw()` 只遍历已有的 `passes × queue.items`，不重复 `buildFromScene()`。

## 关键约束

- `FrameGraph::buildFromScene(...)` 自己不构造 item，只把 `pass.name` 和 `pass.target` 透传给 `pass.queue.buildFromScene(scene, pass.name, pass.target)`。
- `RenderQueue::buildFromScene(...)` 是 `RenderingItem` 的唯一构造入口。
- scene-level 资源在 queue 层统一合并，不在 backend 临时注入。当前实现会先取一次 `scene.getSceneLevelResources(pass, target)`，然后把它追加到每个 item 的 `descriptorResources` 末尾。
- `buildFromScene` 是幂等的，重复调用不会累加旧 item。
- queue 内按 `PipelineKey` 去重一次，frame graph 层再跨 pass 按 `PipelineKey` 去重一次。
- `RenderQueue::sort()` 使用 `std::stable_sort`，按 `pipelineKey.id.id` 升序排，让相同 pipeline 的项相邻。

## 当前实现边界

- `FrameGraph` 支持多个 `FramePass`，但当前 `VulkanRenderer::initScene()` 只接入了一个 `Pass_Forward`。
- `FrameGraph` 更接近 scene-start 产物，而不是通用 game loop。每帧调度顺序由 `EngineLoop` 负责。
- `target` 当前只影响 scene-level 资源筛选，不影响 renderable 自身的 pass 过滤。renderable 是否进入 queue，仍只看 `IRenderable::supportsPass(pass)`。
- `collectAllPipelineBuildDescs()` 直接汇总各个 queue 的 `collectUniquePipelineBuildDescs()` 结果，不会重新扫描 scene。

## 从哪里改

- 想改 pass 过滤：看 `IRenderable::supportsPass(pass)`。
- 想改 `target` 透传或跨 pass 去重：看 `FrameGraph`。
- 想改 item 排序或 scene-level 资源注入：看 `RenderQueue`。
- 想改 pipeline 预构建输入：看 `collectAllPipelineBuildDescs()`。

## 关联文档

- `openspec/specs/frame-graph/spec.md`
- `notes/subsystems/scene.md`
- `notes/subsystems/pipeline-cache.md`
