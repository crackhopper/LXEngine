# 多 Pass 材质怎样变成 RenderWork

旧模型里，一个多 pass 材质像一张分步骤菜谱：scene 先选择一条 technique，loader 再把这条 technique 里的多个 pass 拆成 runtime pass definitions。Material v2 的目标模型改为：SurfaceMaterial 只保存 pure envelope；多 pass 来自 active `RenderPathGraph`。同一个对象可能因为 graph 的 pass filter 参加 `GBuffer`、`ForwardTransparent` 或 `OfflineRayTrace`。LXEngine 不会把整张 graph 一次性交给 backend，而是把它拆成“某个 pass 下的一份 pipeline work”。

这个“某个 pass 下的一份 pipeline work”就是 `RenderWorkItem`。在 realtime raster 路径里，它通常表现为一次 draw；在 offline compute 路径里，它可以表现为一次 dispatch。

## 一个 RenderWorkItem 只属于一个 pass

当前 legacy `MaterialTemplate` 保存 selected technique 映射后的多个 runtime pass；Material v2 目标下这些 pass 来自 RenderPathGraph。无论来源如何，`RenderWorkItem` 的字段里都有明确的 `pass`：

| 字段 | 当前含义 |
|---|---|
| `pass` | 这次 draw 属于哪条 `FramePass` |
| `domain` / `kind` | 区分 realtime/offline，以及 raster draw、compute dispatch 等 work 类型 |
| `shaderInfo` | 当前 pass 使用的 shader |
| `descriptorResources` | 当前 pass 需要的材质资源 + scene-level 资源 |
| `pipelineKey` | 当前 pass 下的 pipeline identity |
| `material` | 运行时材质实例 |
| `raster.vertexBuffer` / `raster.indexBuffer` / `raster.drawData` | raster 几何和 per-draw 数据 |
| `compute.groupCountX/Y/Z` | compute dispatch 的 group count |

所以多 pass 不是“一个 draw 内部循环多个 pass”，而是“同一个 renderable 在不同 pass queue 里产生不同 realtime raster item”。同一套 `RenderWorkItem` 结构也能表达 offline compute item。

## RenderPathGraph pass 与 runtime pass 的对应

RenderPathGraph 里的 pass 名服务于 authoring。graph validation / build 会把它映射成 engine runtime pass：

| RenderPathGraph pass | 默认 runtime pass | 常见用途 |
|---|---|---|
| `Forward.Opaque` | `Pass_Forward` | forward 不透明物体 |
| `Forward.Transparent` | `Pass_ForwardTransparent` | forward 透明物体 |
| `Deferred.GBuffer` | `Pass_Deferred` | deferred 的 GBuffer 几何写入 |
| `Deferred.Transparent` | `Pass_ForwardTransparent` | deferred lighting 后的透明物体 |
| `OfflineRT.RayTrace` | `Pass_OfflineRayTrace` | offline compute/ray tracing work |

如果 pass 声明 `enginePass`，graph builder 使用显式值。这样一个 RenderPath 可以根据渲染路径需要定义多个 pass，同时 runtime 仍然用统一的 pass queue 和 `FrameGraph` 执行。SurfaceMaterial 不声明这些 pass。

## SceneNode 先做 pass-level validation

`SceneNode::rebuildValidatedCache()` 是对象进入 render queue 前的质检站。旧实现会遍历 `MaterialInstance::getEnabledPasses()`；Material v2 目标是遍历 active RenderPathGraph 中匹配该 object `RenderClass` / BSDF type 的 pass，对每个 pass 做校验：

| 校验 | 为什么在这里做 |
|---|---|
| pass 是否存在于 RenderPathGraph | 防止代码或 scene 引用无定义 pass |
| shader 是否存在 | 后端创建 pipeline 需要 shader stages |
| skinning variant 与 `Bones` binding 是否一致 | 防止 shader 宏和资源需求不匹配 |
| mesh vertex layout 是否满足 shader vertex inputs | pipeline vertex input state 必须可构建 |
| system-owned binding 类型是否正确 | `CameraUBO` 等名字有固定 ABI |
| material/feature-owned resource 是否齐全 | shader 需要的材质或 feature 参数必须由 SurfaceMaterial / RenderFeature envelope 提供 |

校验成功后，节点会缓存 `ValidatedRenderablePassData`。这个缓存已经包含 shader、descriptor resources、object signature 和 `PipelineKey`。

## RenderWorkQueue 是 per-pass 的

`FramePass` 持有自己的 `RenderWorkQueue`：

```text
FramePass(name = Forward, target = ...)
  -> RenderWorkQueue(items for Forward)

FramePass(name = Shadow, target = ...)
  -> RenderWorkQueue(items for Shadow)
```

`RenderWorkQueue::build(context, pass, target)` 在 realtime domain 下会做筛选：

| 条件 | 当前含义 |
|---|---|
| `renderable->supportsPass(pass)` | active RenderPathGraph 的 pass filter 命中该 object，并且节点有对应 validated cache |
| visibility mask 命中当前 target camera | 当前 target 的相机能看见该对象 |
| `getValidatedPassData(pass)` 非空 | 节点已通过 pass-level validation |

入队时，queue 会把 material、feature 和 scene-level resources 组装到 item 的 resource view。bindless 目标下，item 应引用 `SceneResourceTable` upload view 的 handle / typed index，而不是 legacy per-object descriptor 资源。

offline domain 使用同一个函数签名。当前 `Pass_OfflineRayTrace` 会生成一个 `ComputeDispatch` item，它的 descriptor resources 来自 `SceneResourceTable` 上传视图、软件 BVH 和 output pixel buffer。

## FrameGraph 当前按顺序构建 queue，并校验资源读写

当前 `FrameGraph` 是一个轻量 per-pass 调度器：

```text
FrameGraph::build(context)
  for each FramePass in m_passes:
    pass.queue.build(context, pass.name, RenderTarget{pass.target})
```

它当前做的事是：

| 已实现 | 未实现 |
|---|---|
| 按 `FramePass` 顺序构建每个 pass 的 queue | 自动重排 pass |
| 把 pass name 和 target 传给 `RenderWorkQueue` | 拓扑排序 |
| 汇总所有 queue 的 `PipelineBuildDesc` 并按 `PipelineKey` 去重 | 自动 barrier / semaphore 推导 |
| 记录 `FrameGraphRead` / `FrameGraphWrite` 并在 compile 阶段校验先写后读 | attachment aliasing |
| 保持 pass 提交顺序来自外层构建顺序 | task-based render graph execution |

因此，“被依赖的 pass 先执行、FrameGraph 根据资源边建立同步”是 roadmap 方向，不是当前实现。当前代码里 pass 顺序由创建 `FrameGraph` 时加入 `FramePass` 的顺序决定。

## PipelineBuildDesc 从 RenderWorkItem 派生

当 backend 需要预构建 pipeline 时，`RenderWorkQueue::collectUniquePipelineBuildDescs()` 会按 `PipelineKey` 去重，再调用：

```text
PipelineBuildDesc::fromRenderWorkItem(item)
```

它从 item 中取出：

| 字段 | 来源 |
|---|---|
| build type | `item.kind`，graphics draw 转成 `Graphics`，compute dispatch 转成 `Compute` |
| shader stages | `item.shaderInfo->getAllStages()` |
| descriptor bindings | `item.shaderInfo->getReflectionBindings()` |
| vertex layout | raster item 的 vertex buffer layout 按 shader vertex inputs 过滤 |
| render state | raster item 的 `item.material->getPassRenderState(item.pass)` |
| topology | raster item 的 index buffer |
| key | `item.pipelineKey` |

也就是说，pipeline 构建输入不是直接从 `.material` 来的，而是从已经通过 scene validation 或 offline job 组装的 work item 派生。

## 我们已经学会了什么

多 pass 在运行时应来自 RenderPathGraph，而不是 SurfaceMaterial。`SceneNode` 先把每个匹配 pass 校验成缓存，`RenderWorkQueue` 再按 pass 收集 `RenderWorkItem`，`FrameGraph` 当前按显式 pass 顺序构建这些 queue，后续由 graph source/target 建 DAG。真正到 backend 的单位始终是单 pass 的 work item。

## 下一步

- [什么是 Pipeline](what-is-pipeline.md)
- [模板如何影响 Pipeline](template-and-pipeline.md)
- [Realtime 与 Offline：同一条 RenderWork 流水线](../../concepts-design/rendering-pipeline/realtime-offline-shared-flow.md)
