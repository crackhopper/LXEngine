# RenderWorkCompiler：FramePass 之后的唯一工单编译器

我们可以把渲染提交想成一张工厂工单：`RenderPathGraph` 画出路线，`FrameGraph` 排出工序，`FramePass` 保存单道工序的合同。真正要把这道工序变成 draw 或 dispatch 时，当前代码只走 `RenderWorkCompiler`，不再经过旧的 per-pass queue。

REQ-073-e2 hard cut 后，`src/core/frame_graph/render_queue.hpp/.cpp` 已删除，`FramePass` 直接持有 `RenderPassInputContract input`，backend pipeline / upload / execute 都消费 `RenderInput[]` 和 `RenderInputDesc[]`。

## 当前主线

```text
RenderPathGraph input
  -> FramePass input contract
  -> RenderWorkCompiler
  -> RenderInput[] payloads
  -> RenderInputDesc[] validation/pipeline/binding facts
  -> Vulkan pipeline/upload/execute
```

这条线里，`RenderInput` 是执行 payload，`RenderInputDesc` 是准备和校验后的事实。二者故意分开：draw command、compute group count 这类“怎么执行”的数据留在 typed input；pipeline key、shader reflection、binding plan、resource dependencies、diagnostics 和 stats 进入 desc。

## FramePass 只保存图合同

`FramePass` 当前字段包括 pass identity、target、reads/writes、shader URI、stage/dispatch、rendering mode、attachments、render state、render path node signature 和 `input`。它不持有 Vulkan pipeline、command buffer、GPU object，也不持有 draw / dispatch payload。

这个边界让 `FrameGraph::compile()` 保持纯 graph 职责：校验 resource vocabulary、producer/consumer DAG、imported/source/target/write-mode 规则，并输出稳定 pass 顺序。它不会遍历 scene，不生成 `RenderInput`，也不创建 `PipelineBuildDesc`。

## input 合同决定输入从哪里来

scene renderables pass 使用 `input.kind: scene-renderables`：

```yaml
input:
  kind: scene-renderables
  material:
    type: [matte, uber, metal, substrate, standard-pbr]
    required: true
  geometry:
    vertex: position-only
    topology: triangle-list
```

如果 pass 只接收某类对象，使用 `input.object.renderClass`：

```yaml
input:
  kind: scene-renderables
  object:
    renderClass: [debug.mesh]
  material:
    required: false
  geometry:
    vertex: position-only
    topology: line-list
```

第二个例子是 debug mesh 的典型形态：`material.required: false` 表示这个 pass 可以接受没有材质的 renderable；它依赖显式 debug shader，而不是隐藏 fallback material。

fullscreen pass 使用内置三角形输入：

```yaml
input:
  kind: fullscreen-triangle
```

`fullscreen-triangle` 只允许 `stage: raster` + `dispatch: fullscreen`，并拒绝 `object`、`material`、`geometry` 子字段。compute pass 使用 `input.kind: compute-dispatch`，当前 offline software-compute path 会为它生成 `RenderComputeInput`。

## buildInputs 生成 typed payload

`RenderWorkCompiler::buildInputs()` 按 `FramePass.input.kind` 分派：

| `input.kind` | 当前 payload | 说明 |
|---|---|---|
| `scene-renderables` | `RenderDrawInput[]` | 遍历 realtime scene renderables，按 object/material/geometry 合同筛选 |
| `fullscreen-triangle` | 一个 `RenderDrawInput` | source 为 `FullscreenTriangle`，backend 记录内置三角形 draw |
| `compute-dispatch` | 一个 `RenderComputeInput` | offline domain 当前按输出尺寸生成 dispatch group，并设置 `OutputPixels` readback |

`RenderDrawInput` 保存 object、mesh、material、vertex/index buffer、draw commands、sort center、object render type 和 material type signature。`RenderComputeInput` 保存 dispatch group count 和 readback resource。compute 不塞进 draw 字段，draw 也不伪装成 compute。

## prepare 生成 RenderInputDesc

`RenderWorkCompiler::prepare()` 对每个 input 生成一个 desc。成功 input 得到 accepted desc；失败 input 得到 rejected desc 和 diagnostic，不能被当成 empty success。

| desc 字段族 | 当前用途 |
|---|---|
| `status` / `diagnostics` | 表达 accepted / rejected 结果和失败原因 |
| `inputIndex` / `pass` / `debugId` | 连接 desc 与 typed input、pass 诊断身份 |
| `pipelineKey` / `pipelineBuildDesc` | backend pipeline cache 的正向输入 |
| `shaderUri` / `shaderVariantKey` / `reflectionIdentity` | shader 和 binding 合同事实 |
| `bindingPlan.descriptors` | descriptor resource binding plan |
| `resourceDependencies` | 上传和同步需要的 GPU resource |
| `stats` | compiler / accepted / rejected / submitted / fallback 统计 |

`RenderInputStats` 当前字段是 `compilerInputCount`、`acceptedInputCount`、`rejectedInputCount`、`submittedDrawCount`、`submittedDispatchCount` 和 `fallbackObservedCount`。Vulkan realtime metadata 使用 `renderInputStats`，不再使用 batch 命名的统计输出。

`render_validation_contract` 的 `validatePreparedRenderInputs()` 直接校验 descs。也就是说，validation 观察的是 prepare 后的 pipeline/binding/resource 事实，而不是旧的 work item 或 queue。

## Vulkan 消费 desc，不反推 pipeline

Vulkan 侧的正向顺序是：

```text
RenderInputDesc.pipelineBuildDesc
  -> VulkanResourceManager::getOrCreatePipeline(desc)
  -> buildRenderUploadPlan(inputs, descs)
  -> command buffer records typed RenderInput with RenderInputDesc facts
```

pipeline 创建只看 `PipelineBuildDesc` / `RenderInputDesc`。upload plan 从 accepted desc 的 binding plan、resource dependencies 和 typed input resources 收集资源。executor 记录 draw 或 dispatch 时，payload 来自 `RenderInput`，pipeline/binding/validation 事实来自 `RenderInputDesc`。

## 已删除的历史词表

旧实现里的 `RenderWorkQueue`、`RenderWorkItem`、`RenderBatch`、`RenderIndirectBatch`、`renderBatchStats`、queue-derived pipeline preload、`Pass_OfflineRayTrace` pass-name branch 都不是当前正向工作流。它们只适合作为历史删除背景或审计 token，不应再出现在新的实现说明、正向测试或资产写法里。

当前 offline software-compute 仍有一个 file-local `OfflineCompute` pass builder，并且 shader 仍通过 `OfflineRenderJob::offlineShader` / provider 进入 compiler preparation；这属于 [REQ-074-h](../../requirements/074-h-offlinert-render-path-graph-compute-path.md) 的后续 OfflineRT graph hard cut，不是旧 queue/item 路径。

## 继续阅读

- [RenderPathGraph：渲染路线说明书](render-path-graph.md)
- [Realtime 与 Offline：共享同一条 compiler 主线](realtime-offline-shared-flow.md)
- [REQ-073-e2](../../requirements/073-e-render-work-compiler-single-path-hard-cut.md)
- [REQ-073-e](../../requirements/073-e-indirect-material-batching-and-diagnostics.md)
- [REQ-074-h](../../requirements/074-h-offlinert-render-path-graph-compute-path.md)
