# Realtime 与 Offline：共享同一条 compiler / executor 主线

Realtime viewport 和 offline renderer 可以先想成同一座工厂里的两种工位：实时工位接着窗口和 swapchain，追求每帧快速交付；离线工位接着 headless device 和 readback payload，追求可复现输出。当前代码让两者共享同一条 RenderPathGraph 主线：graph 生成 FramePass，`RenderWorkCompiler` 生成 typed work 和 `RenderInputDesc`，Vulkan backend 用 `FrameGraphExecutor` 执行。

```text
RenderPathGraph asset
  -> FrameGraph::compile()
  -> RenderWorkCompiler::buildInputs()
  -> prepared pass work
  -> RenderWorkCompiler::prepare()
  -> RenderInputDesc[] facts
  -> VulkanFrameGraphExecutor
  -> attachment/storage readback payload when requested
```

这条主线不再区分“realtime queue”和“offline queue”。差异保留在 domain/options、output profile、target/readback contract 和 typed payload 里。

## Realtime 从 RenderPathGraph 进入 FramePass

实时路径的结构入口是 `assets/render_paths/*.render-path.yaml`：

```text
RenderPathGraph input
  -> RenderPassNode
  -> FramePass input contract
  -> RenderWorkCompiler
```

Forward / Deferred graph 的 draw pass 使用 `input.kind: scene-renderables`。Compiler 会从 scene 里筛选 renderables，检查 material type、object render class、geometry contract 和 scene-level resources，然后生成 draw work。

Bloom / DeferredLighting 这类 fullscreen pass 使用 `input.kind: fullscreen-triangle`。Compiler 不遍历 scene，只生成一个内置 fullscreen draw input。

Vulkan realtime metadata 当前暴露 `renderInputStats`。关键字段包括 `compilerInputCount`、`acceptedInputCount`、`rejectedInputCount`、`submittedDrawCount`、`submittedDispatchCount` 和 `fallbackObservedCount`。

## Offline 也从 OutputProfile 的 RenderPathGraph 进入

离线路径不再创建 `OfflineRenderJob` 或 file-local offline graph。它使用 scene 里的 output profile：

```text
scene.outputProfiles.<name>.renderPathGraph
  -> RenderPathGraph asset
  -> FrameGraph::compile()
  -> RenderWorkCompiler
  -> VulkanFrameGraphExecutor
  -> FrameGraphExecutionPayload
  -> OfflineImageWriter
```

当前 `assets/scenes/generated/helmet_standard_pbr.scene.yaml` 用四个 profile 验证这条路径：

| Profile | Graph | Pass 形态 |
|---|---|---|
| `forward_no_ibl` | `assets/render_paths/forward_offline_direct.render-path.yaml` | graphics/raster |
| `ibl_only` | `assets/render_paths/forward_offline_ibl_only.render-path.yaml` | graphics/raster |
| `forward_ibl` | `assets/render_paths/forward_offline_direct_ibl.render-path.yaml` | graphics/raster |
| `raytrace` | `assets/render_paths/offline_standard_pbr_raytrace.render-path.yaml` | compute |

offline raster 和 offline compute 的差异由 graph/pass 表达，不由 C++ 入口硬编码。readback 不是 pass，而是 graph/output contract。

## 共享的 Backend 顺序

Realtime raster、fullscreen raster、offline raster 和 offline compute 进入 Vulkan backend 后，正向事实都来自 desc 和 prepared work：

| 阶段 | 当前输入 | 作用 |
|---|---|---|
| validation | `RenderInputDesc[]` | 校验 accepted desc、pipeline facts、binding/resource facts |
| resource sync | prepared work + desc resources | 从 typed work 和 desc 收集 buffer/image/material/feature resources |
| pipeline lookup | `RenderInputDesc` | `VulkanResourceManager::getOrCreatePipeline(desc)` 使用 pipeline build desc |
| command recording | typed work + `RenderInputDesc` | draw work 记录 draw；compute work 记录 dispatch；desc 提供 pipeline/binding facts |
| readback | target/readback contract | 将 attachment 或 storage buffer 复制成 payload |

这样做的好处是，pipeline 创建不再从 raw input 反推，也不需要 queue-derived preload。一个 input 如果失败，会留下 rejected desc 和 diagnostic；它不会被当成“没有 work”悄悄跳过。

## 两个入口仍保留的差异

| 维度 | Realtime | Offline |
|---|---|---|
| 运行入口 | editor renderer / swapchain frame loop | `lxe_offline_render` / headless renderer |
| 结构入口 | active render path graph | selected output profile 的 render path graph |
| 输出 | swapchain / offscreen attachment | readback payload + EXR/PNG/JSON/raw |
| target 生命周期 | swapchain 与 renderer frame resources | executor 创建的 offscreen target |
| 验证重点 | frame stats、viewport、dump target | CLI smoke、payload、image writer |

这些差异是运行时和输出边界的差异，不是第二套 work pipeline。后续 OfflineRT / hardware RT 扩展应继续复用 `FramePass`、`RenderWorkCompiler`、`RenderInputDesc` 和 `FrameGraphExecutor`。

## 当前边界

| 已实现 | 后续边界 |
|---|---|
| Realtime RenderPathGraph pass 的 `input` 合同进入 `FramePass` | 更多非 opaque / transparent policy 仍由后续 REQ 扩展 |
| Offline output profile 指向 RenderPathGraph asset | editor 内离线任务面板属于后续需求 |
| `RenderWorkCompiler` 生成 scene-renderable draw、fullscreen draw 和 compute dispatch input | async multi-queue / automatic barrier 属于 `REQ-078-a` |
| `FrameGraphExecutor` 支持 headless raster/compute readback | hardware ray tracing pipeline 属于后续 OfflineRT 需求 |
| realtime metadata 使用 `renderInputStats` | 更复杂的 package readiness / Helmet-BMW smoke 属于 `REQ-074-i` |

## 继续阅读

- [FrameGraph：一帧的 Pass 排程表](framegraph.md)
- [RenderPathGraph：渲染路线说明书](render-path-graph.md)
- [RenderWorkCompiler：FramePass 之后的唯一工单编译器](render-work-compiler.md)
- [Render Target：Pass 的输出形状](render-target.md)
- [Vulkan Backend](../../subsystems/vulkan-backend.md)
