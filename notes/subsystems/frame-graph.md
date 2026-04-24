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
- `RenderTarget`：pass 输出目标的 core 侧描述，字段是 `colorFormat`、`depthFormat`、`sampleCount`。

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
- `RenderQueue::buildFromScene(...)` 只消费 renderable 的 validated entry，不做首次 mesh/material/skeleton 结构校验。
- scene-level 资源在 queue 层统一合并：先取一次 `scene.getSceneLevelResources(pass, target)`，再追加到每个 item 的 `descriptorResources` 末尾。
- `RenderQueue::buildFromScene(...)` 会把当前 `target` 下所有匹配 camera 的 `cullingMask` 做 OR，然后只保留 `visibilityMask` 与之有交集的 renderable。
- `buildFromScene` 是幂等的，重复调用不会累加旧 item。
- queue 内按 `PipelineKey` 去重一次，frame graph 层再跨 pass 按 `PipelineKey` 去重一次。
- `RenderQueue::sort()` 使用 `std::stable_sort`，按 `pipelineKey.id.id` 升序排，让相同 pipeline 的项相邻。

## 当前实现边界

- `FrameGraph` 支持多个 `FramePass`，但当前 `VulkanRenderer::initScene()` 仍主要接入 `Pass_Forward`。
- `target` 现在既影响 scene-level 资源筛选，也决定当前参与可见性判断的 camera 集合；同 target 多 camera 采用 `cullingMask` 按位 OR 语义。
- 如果某个 `IRenderable` 没有对应 pass 的 validated 数据，queue 会直接跳过它，而不是在这里补救。
- `collectAllPipelineBuildDescs()` 直接汇总各个 queue 的 `collectUniquePipelineBuildDescs()` 结果，不会重新扫描 scene。

## 从哪里改

- 想改 pass 过滤语义：看 `IRenderable::supportsPass(pass)` 和 `SceneNode` 的 validated cache。
- 想改 `target` 透传或跨 pass 去重：看 `FrameGraph`。
- 想改 scene-level 资源注入或 item 装配：看 `RenderQueue`。

## 关联文档

- `notes/subsystems/scene.md`
- `notes/subsystems/pipeline-cache.md`
- `notes/subsystems/pipeline-identity.md`
