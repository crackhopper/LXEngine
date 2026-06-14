# RenderPathGraph 怎样变成 RenderWork

Surface material 只说明“表面是什么”；真正的多 pass 来自 active `RenderPathGraph`。同一个 object 可能在 Shadow pass 写 depth，在 Forward pass 写 HDR color，在 PostProcess pass 之后由 DebugOverlay 追加调试线。LXEngine 不会把整张 graph 一次性交给 backend，而是把它拆成“某个 pass 下的一份 pipeline work”。

这个“某个 pass 下的一份 pipeline work”就是 `RenderWorkItem`。在 realtime raster 路径里，它通常表现为一次 draw；在 offline compute 路径里，它可以表现为一次 dispatch。

## RenderPathGraph pass 是结构真值

`assets/render_paths/forward_main.render-path.yaml` 当前包含这些 pass：

| Pass | 作用 | 关键 source / target |
|---|---|---|
| `Shadow` | 写 shadow depth | `scene.camera` / `scene.lights` -> `shadow.main` |
| `Forward` | 采样 material BSDF 并写 HDR color/depth | `material.bsdf`、`scene.camera`、`scene.lights` -> `hdr.color`、`depth.main` |
| `PostProcess` | 读 HDR color 和 tone mapping feature，写 swapchain | `hdr.color`、`feature.toneMapping` -> `swapchain.color` |
| `DebugOverlay` | 追加 debug overlay 线段 | `scene.camera` -> `debug.overlay` |

pass 节点同时声明 `stage`、`dispatch`、`shader`、`filters`、`rendering.attachments`、`geometry` 和 `renderState`。这些字段会进入 RenderPathNode signature，成为 pipeline identity 的一部分。

## 一个 RenderWorkItem 只属于一个 pass

`RenderWorkItem` 的字段里有明确的 `pass`：

| 字段 | 当前含义 |
|---|---|
| `domain` / `kind` | 区分 realtime/offline，以及 raster draw、compute dispatch 等 work 类型 |
| `pass` | 这次 work 属于哪条 `FramePass` |
| `target` | 当前 pass 的 runtime target |
| `shaderInfo` | 当前 pass 使用的 shader |
| `descriptorResources` | 当前 pass 需要的材质资源 + feature/scene-level 资源 |
| `materialTypeVariant` | material-side pipeline identity 输入 |
| `renderPathNodeSignature` | pass-side pipeline identity 输入 |
| `pipelineKey` | `PipelineKey::build(materialTypeVariant, renderPathNodeSignature)` |
| `raster.vertexBuffer` / `raster.indexBuffer` / `raster.drawData` | raster 几何和 per-draw 数据 |
| `compute.groupCountX/Y/Z` | compute dispatch 的 group count |

所以多 pass 不是“一个 draw 内部循环多个 pass”，而是“同一个 renderable 在不同 pass queue 里产生不同 item”。

## SceneNode 先做 pass-level validation

对象进入 render queue 前需要先被验证成 pass 可消费的事实：

| 校验 | 为什么在这里做 |
|---|---|
| graph pass filter 是否命中 object 的 render class / BSDF type | 防止 surface material 被错误 pass 消费 |
| shader payload 是否真实存在 | 不能用 metadata-only 或 placeholder payload 满足渲染依赖 |
| material source variant 是否已解析 | specialized shader variant 必须是 live typed payload |
| mesh vertex layout / topology 是否满足 geometry contract | pipeline vertex input 和 topology 必须可构建 |
| system-owned binding 类型是否正确 | `CameraUBO`、scene light、bones 等名字有固定 ABI |
| material/feature-owned resource 是否齐全 | shader 需要的 envelope 或 feature 参数必须真实注册 |

校验成功后，节点缓存 pass 数据；`RenderWorkQueue` 只消费这些已经通过验证的事实。

## RenderWorkQueue 是 per-pass 的

`FramePass` 持有自己的 `RenderWorkQueue`：

```text
FramePass(name = Shadow)
  -> RenderWorkQueue(items for Shadow)

FramePass(name = Forward)
  -> RenderWorkQueue(items for Forward)
```

`RenderWorkQueue::build(...)` 在 realtime domain 下会做筛选：

| 条件 | 当前含义 |
|---|---|
| `renderable->supportsPass(pass)` | active graph 的 pass filter 命中，并且节点有对应 validated cache |
| visibility mask 命中当前 target camera | 当前 target 的相机能看见该对象 |
| `getValidatedPassData(pass)` 非空 | 节点已通过 pass-level validation |

入队前通过 scene descriptor resolver 组装 `descriptorResources`：material binding、renderable-owned system binding、scene-level resources、IBL resources 和 graph/feature resources 在这里收口。

## FrameGraph 负责 queue 构建和资源依赖编译

当前 `FrameGraph` 已经不是只按插入顺序执行的 list。它有两步：

```text
FrameGraph::build(context)
  -> 每条 FramePass 调 RenderWorkQueue::build(...)

FrameGraph::compile(registry)
  -> 校验 graph resource source/target
  -> 非 imported source 连接到 producer
  -> 按资源依赖 DAG 排序
  -> 用 phase / stableOrder / 插入顺序做稳定兜底
```

它仍然不持有 backend attachment，也不做 attachment aliasing；backend 执行层负责把 compiled pass 转成具体 framebuffer/render pass/dynamic rendering 状态。

## PipelineBuildDesc 从 RenderWorkItem 派生

当 backend 需要预构建 pipeline 时，`RenderWorkQueue::collectUniquePipelineBuildDescs()` 会按 `PipelineKey` 去重，再调用：

```text
PipelineBuildDesc::fromRenderWorkItem(item)
```

它从 item 中取出 shader stages、reflection bindings、vertex layout、render state、topology、target、attachments 和 key。pipeline 构建输入不是直接从 `.material` 来的，而是从已经通过 scene/graph validation 组装好的 work item 派生。

## 我们已经学会了什么

多 pass 来自 RenderPathGraph，而不是 SurfaceMaterial。Scene/graph validation 先把 object + material + pass contract 校验成可消费事实，RenderWorkQueue 再按 pass 收集 `RenderWorkItem`，FrameGraph 负责构建 queue 并按资源依赖编译 pass 顺序。backend 最终消费的单位始终是单 pass work item。

## 下一步

- [什么是 Pipeline](what-is-pipeline.md)
- [Realtime 与 Offline：同一条 RenderWork 流水线](../../concepts-design/rendering-pipeline/realtime-offline-shared-flow.md)
