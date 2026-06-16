# Realtime 与 Offline：共享同一条 compiler 主线

Realtime viewport 和 offline renderer 可以先想成同一座工厂里的两种工位：实时工位接着窗口和 swapchain，追求每帧快速交付；离线工位接着 headless device 和 readback buffer，追求可复现输出。当前代码让两者共享同一条 post-FramePass 主线：`RenderWorkCompiler` 生成 typed input，再由 `RenderInputDesc` 驱动 pipeline、upload 和 command recording。

```text
FramePass input contract
  -> RenderWorkCompiler::buildInputs()
  -> RenderInput[] payloads
  -> RenderWorkCompiler::prepare()
  -> RenderInputDesc[] facts
  -> validatePreparedRenderInputs(descs)
  -> buildRenderUploadPlan(inputs, descs)
  -> VulkanResourceManager::getOrCreatePipeline(desc)
  -> command buffer executes typed input with desc facts
```

这条主线不再区分“realtime queue”和“offline queue”。差异保留在 domain context 和 typed payload 里。

## Realtime 从 RenderPathGraph 进入 FramePass

实时路径的结构入口是 `assets/render_paths/*.render-path.yaml`：

```text
RenderPathGraph input
  -> RenderPassNode
  -> FramePass input contract
  -> RenderWorkCompiler
```

Forward / Deferred graph 的 draw pass 使用 `input.kind: scene-renderables`。Compiler 会从 scene 里筛选 renderables，检查 material type、object render class、geometry contract 和 scene-level resources，然后生成 `RenderDrawInput[]`。

PostProcess / Bloom / DeferredLighting 这类 fullscreen pass 使用 `input.kind: fullscreen-triangle`。Compiler 不遍历 scene，只生成一个内置 fullscreen draw input。

Vulkan realtime metadata 当前暴露 `renderInputStats`。关键字段包括 `compilerInputCount`、`acceptedInputCount`、`rejectedInputCount`、`submittedDrawCount`、`submittedDispatchCount` 和 `fallbackObservedCount`。

## Offline 当前使用 file-local OfflineCompute pass

当前 offline software-compute 还没有默认 OfflineRT RenderPathGraph asset。它使用 `src/core/offline/offline_render_work_graph.cpp` 里的 file-local `OfflineCompute` pass：

```text
OfflineRenderJob
  -> createOfflineRenderFrameGraph()
  -> FramePass { name = OfflineCompute, stage = compute, dispatch = compute,
                 input.kind = compute-dispatch }
  -> RenderWorkCompiler
  -> RenderComputeInput
  -> RenderInputDesc
```

`RenderWorkCompiler::buildInputs()` 在 offline domain 下根据 output width/height 生成 compute group count，并把 readback resource 设为 `OutputPixels`。`prepare()` 再通过 `OfflineRenderJob::offlineShader` 和 `offline::buildOfflineSceneStorageResources(job)` 准备 shader facts、descriptor resources、pipeline build desc 和 resource dependencies。

这说明当前 offline 已经不走旧的 queue/item 路径，也不再通过 `Pass_OfflineRayTrace` token 选择 work。它仍然保留 `OfflineRenderJob::offlineShader` / provider 作为 shader side channel；把 shader URI、compute block、profile/output resource 完全迁到 OfflineRT graph asset，是 [REQ-074-h](../../requirements/074-h-offlinert-render-path-graph-compute-path.md) 的后续工作。

## 共享的 backend 顺序

Realtime raster、fullscreen raster 和 offline compute 进入 Vulkan backend 后，正向事实都来自 desc：

| 阶段 | 当前输入 | 作用 |
|---|---|---|
| validation | `RenderInputDesc[]` | `validatePreparedRenderInputs()` 校验 accepted desc、pipeline facts、binding/resource facts |
| upload planning | `RenderInput[]` + `RenderInputDesc[]` | `buildRenderUploadPlan()` 从 accepted desc 和 typed input 收集资源 |
| pipeline lookup | `RenderInputDesc` | `VulkanResourceManager::getOrCreatePipeline(desc)` 使用 `desc.pipelineBuildDesc` |
| command recording | typed `RenderInput` + `RenderInputDesc` | draw input 记录 draw；compute input 记录 dispatch；desc 提供 pipeline/binding facts |

这样做的好处是，pipeline 创建不再从 raw input 反推，也不需要 queue-derived preload。一个 input 如果失败，会留下 rejected desc 和 diagnostic；它不会被当成“没有 work”悄悄跳过。

## 两个入口仍保留的差异

| 维度 | Realtime | Offline software-compute |
|---|---|---|
| 结构入口 | RenderPathGraph asset | file-local `OfflineCompute` FramePass |
| domain context | `RenderWorkBuildContext::realtime(scene, options)` | `RenderWorkBuildContext::offline(job)` |
| typed payload | `RenderDrawInput` 为主，fullscreen 也是 draw input | `RenderComputeInput` |
| shader 来源 | pass shader URI + source variant / reflection facts | 当前 `OfflineRenderJob::offlineShader` / provider |
| resource 来源 | scene renderables、material resources、scene camera/lights、frame graph sampled resources | `SceneResourceTable` upload view 派生的 offline storage resources 和 `OutputPixels` |
| 输出 | swapchain / offscreen attachment | storage buffer readback |

这些差异是 domain payload 的差异，不是第二套 work pipeline。后续 OfflineRT graph hard cut 应继续复用 `FramePass`、`RenderWorkCompiler`、`RenderComputeInput` 和 `RenderInputDesc`，而不是重新引入 offline-only compiler 或 queue。

## 当前边界

| 已实现 | 后续边界 |
|---|---|
| Realtime RenderPathGraph pass 的 `input` 合同进入 `FramePass` | 更多非 opaque / transparent policy 仍由后续 REQ 扩展 |
| `RenderWorkCompiler` 生成 scene-renderable draw、fullscreen draw 和 compute dispatch input | OfflineRT graph asset、compute block 和 shader URI hard cut 属于 `REQ-074-h` |
| `RenderInputDesc` 驱动 validation、pipeline lookup、upload planning 和 command recording | async multi-queue / automatic barrier 属于 `REQ-078-a` |
| realtime metadata 使用 `renderInputStats` | 更复杂的 package readiness / Helmet-BMW smoke 属于 `REQ-074-i` |

## 继续阅读

- [FrameGraph：一帧的 Pass 排程表](framegraph.md)
- [RenderPathGraph：渲染路线说明书](render-path-graph.md)
- [RenderWorkCompiler：FramePass 之后的唯一工单编译器](render-work-compiler.md)
- [Render Target：Pass 的输出形状](render-target.md)
- [Vulkan Backend](../../subsystems/vulkan-backend.md)
