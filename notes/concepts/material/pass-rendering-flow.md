# 多 Pass 材质怎样变成 Draw

一个多 pass 材质像一张分步骤菜谱：同一个对象可能先参加 `Shadow`，再参加 `Forward`，以后还可能参加 `GBuffer` 或调试 pass。LXEngine 当前不会把整张菜谱一次性交给 backend，而是把它拆成“某个对象在某个 pass 下的一次 draw”。

这个“某个 pass 下的一次 draw”就是 `RenderingItem`。

## 一个 RenderingItem 只属于一个 pass

`MaterialTemplate` 可以定义多个 pass，但 `RenderingItem` 的字段里有明确的 `pass`：

| 字段 | 当前含义 |
|---|---|
| `pass` | 这次 draw 属于哪条 `FramePass` |
| `shaderInfo` | 当前 pass 使用的 shader |
| `descriptorResources` | 当前 pass 需要的材质资源 + scene-level 资源 |
| `pipelineKey` | 当前 pass 下的 pipeline identity |
| `material` | 运行时材质实例 |
| `vertexBuffer` / `indexBuffer` / `drawData` | 几何和 per-draw 数据 |

所以多 pass 不是“一个 draw 内部循环多个 pass”，而是“同一个 renderable 在不同 pass queue 里产生不同 item”。

## SceneNode 先做 pass-level validation

`SceneNode::rebuildValidatedCache()` 是材质进入 render queue 前的质检站。它会遍历 `MaterialInstance::getEnabledPasses()`，对每个启用 pass 做校验：

| 校验 | 为什么在这里做 |
|---|---|
| pass 是否存在于 template | 防止 instance 启用了无定义 pass |
| shader 是否存在 | 后端创建 pipeline 需要 shader stages |
| skinning variant 与 `Bones` binding 是否一致 | 防止 shader 宏和资源需求不匹配 |
| mesh vertex layout 是否满足 shader vertex inputs | pipeline vertex input state 必须可构建 |
| system-owned binding 类型是否正确 | `CameraUBO` 等名字有固定 ABI |
| material-owned resource 是否齐全 | shader 需要的材质 UBO/texture 必须由 instance 提供 |

校验成功后，节点会缓存 `ValidatedRenderablePassData`。这个缓存已经包含 shader、descriptor resources、object signature 和 `PipelineKey`。

## RenderQueue 是 per-pass 的

`FramePass` 持有自己的 `RenderQueue`：

```text
FramePass(name = Forward, target = ...)
  -> RenderQueue(items for Forward)

FramePass(name = Shadow, target = ...)
  -> RenderQueue(items for Shadow)
```

`RenderQueue::buildFromScene(scene, pass, target)` 会做三层筛选：

| 条件 | 当前含义 |
|---|---|
| `renderable->supportsPass(pass)` | 材质启用了该 pass，并且节点有对应 validated cache |
| visibility mask 命中当前 target camera | 当前 target 的相机能看见该对象 |
| `getValidatedPassData(pass)` 非空 | 节点已通过 pass-level validation |

入队时，queue 会把 scene-level resources 追加到 item 的 `descriptorResources` 末尾。材质资源在前，scene/camera/light 资源在后；backend 按 binding name 和 descriptor binding 匹配，不靠这个顺序表达语义。

## FrameGraph 当前按顺序构建 queue，并校验资源读写

当前 `FrameGraph` 是一个轻量 per-pass 调度器：

```text
FrameGraph::buildFromScene(scene)
  for each FramePass in m_passes:
    pass.queue.buildFromScene(scene, pass.name, pass.target)
```

它当前做的事是：

| 已实现 | 未实现 |
|---|---|
| 按 `FramePass` 顺序构建每个 pass 的 queue | 自动重排 pass |
| 把 pass name 和 target 传给 `RenderQueue` | 拓扑排序 |
| 汇总所有 queue 的 `PipelineBuildDesc` 并按 `PipelineKey` 去重 | 自动 barrier / semaphore 推导 |
| 记录 `FrameGraphRead` / `FrameGraphWrite` 并在 compile 阶段校验先写后读 | attachment aliasing |
| 保持 pass 提交顺序来自外层构建顺序 | task-based render graph execution |

因此，“被依赖的 pass 先执行、FrameGraph 根据资源边建立同步”是 roadmap 方向，不是当前实现。当前代码里 pass 顺序由创建 `FrameGraph` 时加入 `FramePass` 的顺序决定。

## PipelineBuildDesc 从 RenderingItem 派生

当 backend 需要预构建 pipeline 时，`RenderQueue::collectUniquePipelineBuildDescs()` 会按 `PipelineKey` 去重，再调用：

```text
PipelineBuildDesc::fromRenderingItem(item)
```

它从 item 中取出：

| 字段 | 来源 |
|---|---|
| shader stages | `item.shaderInfo->getAllStages()` |
| descriptor bindings | `item.shaderInfo->getReflectionBindings()` |
| vertex layout | item vertex buffer layout 按 shader vertex inputs 过滤 |
| render state | `item.material->getPassRenderState(item.pass)` |
| topology | item index buffer |
| key | `item.pipelineKey` |

也就是说，pipeline 构建输入不是直接从 `.material` 来的，而是从已经通过 scene validation 的 draw item 派生。

## 我们已经学会了什么

多 pass 材质在运行时被拆成多个 pass 视角。`SceneNode` 先把每个 pass 校验成缓存，`RenderQueue` 再按 pass 收集 `RenderingItem`，`FrameGraph` 当前按显式 pass 顺序构建这些 queue。真正到 backend 的单位始终是单 pass 的 draw item。

## 下一步

- [什么是 Pipeline](what-is-pipeline.md)
- [模板如何影响 Pipeline](template-and-pipeline.md)
- [FrameGraph 技术调研](../../roadmaps/research/frame-graph/README.md)
