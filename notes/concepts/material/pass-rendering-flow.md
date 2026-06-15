# RenderPathGraph 怎样变成 RenderInput

Surface material 只说明“表面是什么”；真正的多 pass 来自 active `RenderPathGraph`。同一个 object 可能在 Shadow pass 写 depth，在 Forward pass 写 HDR color，在 PostProcess pass 之后由 DebugOverlay 追加调试线。LXEngine 不会把整张 graph 一次性交给 backend，而是把它拆成“某个 pass 下的一份 pipeline work”。

当前这份 work 分成两层：`RenderInput` 描述要画什么或 dispatch 什么；`RenderInputDesc` 描述这份输入对应的 pipeline、shader variant、binding plan 和诊断结果。Realtime raster 路径通常生成 `RenderDrawInput`；offline compute 路径生成 `RenderComputeInput`。

## RenderPathGraph pass 是结构真值

`assets/render_paths/forward_main.render-path.yaml` 当前包含这些 pass：

| Pass | 作用 | 关键 source / target |
|---|---|---|
| `Shadow` | 写 shadow depth | `scene.camera` / `scene.lights` -> `shadow.main` |
| `Forward` | 采样 material BSDF 并写 HDR color/depth | `material.bsdf`、`scene.camera`、`scene.lights` -> `hdr.color`、`depth.main` |
| `PostProcess` | 读 HDR color 和 tone mapping feature，写 swapchain | `hdr.color`、`feature.toneMapping` -> `swapchain.color` |
| `DebugOverlay` | 追加 debug overlay 线段 | `scene.camera` -> `debug.overlay` |

pass 节点同时声明 `stage`、`dispatch`、`shader`、`input`、`sources`、`targets`、`rendering.attachments` 和 `renderState`。这些字段会进入 RenderPathNode signature，成为 pipeline identity 的一部分。

## 一个 RenderInput 只属于一个 pass

`RenderWorkCompiler::buildInputs()` 按单个 `FramePass` 生成输入：

| 字段 | 当前含义 |
|---|---|
| `RenderInputKind` | `Draw` 或 `Compute` |
| `RenderDrawInput.source` | `SceneRenderable` 或 `FullscreenTriangle` |
| `object` / `mesh` / `material` handle | scene renderable draw 的 typed handle |
| `drawCommands` | draw index、index count、instance count 等 draw 数据 |
| `debugOnly` | debug overlay 类输入的可见性标记 |
| `RenderComputeInput.groupCountX/Y/Z` | compute dispatch 的 group count |
| `readbackResource` | offline compute 当前用 `OutputPixels` 指定 readback buffer |

所以多 pass 不是“一个 draw 内部循环多个 pass”，而是“同一个 renderable 在不同 pass 下产生不同 input”。Fullscreen/post pass 没有 mesh draw；offline compute pass 没有 graphics vertex/index 输入。

## SceneNode 先提供可验证事实

对象进入 render input 前需要先被验证成 pass 可消费的事实：

| 校验 | 为什么在这里做 |
|---|---|
| graph pass `input.material.type` 是否命中 BSDF type | 防止 surface material 被错误 pass 消费 |
| graph pass `input.object.renderClass` 是否命中 object render class | 防止 debug/object pass 消费错误对象 |
| shader payload 是否真实存在 | 不能用 metadata-only 或 placeholder payload 满足渲染依赖 |
| material source variant 是否已解析 | specialized shader variant 必须是 live typed payload |
| mesh vertex layout / topology 是否满足 input geometry contract | pipeline vertex input 和 topology 必须可构建 |
| system-owned binding 类型是否正确 | `CameraUBO`、scene light、bones 等名字有固定 ABI |
| material/feature-owned resource 是否齐全 | shader 需要的 envelope 或 feature 参数必须真实注册 |

校验成功后，节点缓存 pass 数据；`RenderWorkCompiler` 只消费这些已经通过验证的事实和 scene upload view。

## RenderWorkCompiler 是 per-pass 的

`FrameGraph::compile()` 只负责 pass 资源依赖和稳定顺序。编译后，backend 或 integrator 会按 `CompiledPass` 找回对应 `FramePass`，再调用 `RenderWorkCompiler`：

```text
FramePass(name = Shadow)
  -> RenderWorkCompiler::buildInputs(...)
  -> RenderWorkCompiler::prepare(...)

FramePass(name = Forward)
  -> RenderWorkCompiler::buildInputs(...)
  -> RenderWorkCompiler::prepare(...)
```

`RenderWorkCompiler::buildInputs(...)` 在 realtime domain 下会做筛选：

| 条件 | 当前含义 |
|---|---|
| `renderable->supportsPass(pass)` | active graph 的 pass input contract 命中，并且节点有对应 validated cache |
| visibility mask 命中当前 target camera | 当前 target 的相机能看见该对象 |
| `getValidatedPassData(pass)` 非空 | 节点已通过 pass-level validation |

`prepare(...)` 再把输入提升成 pipeline-facing 描述：`PipelineKey`、`PipelineBuildDesc`、shader URI、shader variant key、reflection identity、binding plan、resource dependencies、diagnostics 和 stats 都在 `RenderInputDesc` 中收口。

## FrameGraph 只负责编译资源依赖

当前 `FrameGraph` 已经不是只按插入顺序执行的 list。它的核心任务是：

```text
FrameGraph::compile(registry)
  -> 校验 graph resource source/target
  -> 非 imported source 连接到 producer
  -> 按资源依赖 DAG 排序
  -> 用 phase / stableOrder / 插入顺序做稳定兜底
```

它仍然不持有 backend attachment，也不做 attachment aliasing；backend 执行层负责把 compiled pass 转成具体 framebuffer/render pass/dynamic rendering 状态。

## PipelineBuildDesc 从 RenderInputDesc 提供

当 backend 需要预构建 pipeline 时，它从 `RenderWorkCompiler::prepare(...)` 的 accepted desc 里收集 `pipelineBuildDesc`，再按 `PipelineKey` 去重：

```text
RenderWorkCompiler::prepare(pass, context, inputs)
  -> RenderInputDesc(status = Accepted, pipelineBuildDesc = ...)
  -> PipelineCache::preload(descs)
```

`PipelineBuildDesc` 现在由 compiler/backend 过渡层明确构造。它携带 shader stages、reflection bindings、vertex layout、render state、topology、target、attachments 和 key。pipeline 构建输入不是直接从 `.material` 来的，而是从已经通过 scene/graph validation 组装好的 input desc 派生。

## 我们已经学会了什么

多 pass 来自 RenderPathGraph，而不是 SurfaceMaterial。Scene/graph validation 先把 object + material + pass contract 校验成可消费事实，FrameGraph 负责编译 pass 资源顺序，`RenderWorkCompiler` 再按 pass 生成 `RenderInput` 和 `RenderInputDesc`。backend 最终消费的单位始终是单 pass input。

## 下一步

- [什么是 Pipeline](what-is-pipeline.md)
- [Realtime 与 Offline：同一条输入准备流水线](../../concepts-design/rendering-pipeline/realtime-offline-shared-flow.md)
