# REQ-073-e2: Render Work Compiler Single Path Hard Cut

> 2026-06-14 补正：本 REQ 是 `REQ-073-e` 的底层架构补正，不属于 OfflineRT 专项。`REQ-073-e` 负责 realtime opaque batching；本 REQ 负责把 FrameGraph 之后的 render work 概念收敛成一条单轨。完成态固定采用 `FramePass` / `CompiledFrameGraph` / `RenderWorkCompiler` / `RenderInput` / `RenderInputDesc` 这套 public 词表；旧 `RenderWorkQueue`、old union-like `RenderWorkItem`、`RenderBatch` / `RenderBatchAnalysis` public output 或只服务 opaque 的过渡术语必须 hard cut。
>
> 2026-06-15 Task 8/9 校准：代码 hard cut 已完成。`src/core/frame_graph/render_queue.hpp/.cpp` 和旧 bridge audit test 已删除；`FramePass` 直接持有 `RenderPassInputContract input`；`RenderWorkCompiler::buildInputs()` / `prepare()` 产出 `RenderInput[]` 和 `RenderInputDesc[]`；Vulkan realtime metadata 使用 `renderInputStats`。本文早期“当前代码仍...”段落保留为需求立项背景，实施状态以文末为准。

## 背景

本 REQ 立项时，FrameGraph 后的 work 路径经历过多轮 agent/需求演进，出现过多套职责相近的概念。`REQ-073-e` 当时已经让 realtime material-source geometry 走到 `RenderDrawInput`、prepared candidate、旧 public batch result 和 Vulkan indirect submission；这证明 opaque batching 能跑，但它仍然不是本 REQ 的完成态单轨模型。

- hard cut 前，`FramePass` 保存 pass identity、target、reads/writes、shader URI、stage/dispatch、geometry/attachment/render state 等 graph contract，同时还内嵌旧 queue owner。
- `FrameGraph::compile()` 已产出 ordered pass records，并通过内部 producer/consumer edges 排序；barrier/resource-state plan 仍是后续扩展点。
- hard cut 前，旧 queue build 同时承担 scene traversal、pass-local context assembly、realtime draw input 填充和 offline compute item 创建，因此成为 `FramePass` 之外的第二 per-pass owner。
- hard cut 前，realtime material-source geometry 会先解析为过渡 prepared candidate，再由旧 batch result 进入 Vulkan indirect submission。
- hard cut 前，旧 union-like work item 把 direct helper raster、offline compute dispatch、descriptor resources、shader info、pipeline key 和 backend submission payload 塞在同一个结构里。
- hard cut 前，pipeline desc、pipeline lookup 和 resource upload 仍可从旧 public payload / queue 派生。
- hard cut 前，Helmet realtime smoke 从 batch-named metadata 读取 coverage；本 REQ 要把这些观测点迁到 `RenderInputDesc` / backend desc consumption。
- `REQ-073-g` 需要 compute pass；如果它重新扩展旧 queue 或把 compute 塞进 `RenderDrawInput`，会再次发明一套近似类。

这些都是立项期历史事实，不是当前完成态。完成态必须只有一条通用 work pipeline，realtime raster、future realtime compute、OfflineRT compute 都复用同一组底层概念。

## 目标

1. 明确 `FrameGraph::compile()` 的唯一职责：构建 pass/resource DAG、ordered pass list 和 barrier/resource-state planning 所需依赖；不生成 draw/dispatch input。
2. 把 `FramePass` 定义为 per-pass / per-node 的 pass/input contract record；删除独立旧 queue owner。
3. 建立唯一 `RenderWorkCompiler` 抽象，根据 `RenderDomain`、`FramePass.stage`、`FramePass.dispatch`、node contract 和 shader reflection 选择 raster 或 compute compiler。
4. 建立唯一 `RenderInput` family：`RenderDrawInput` 只服务 raster draw，`RenderComputeInput` 只服务 compute dispatch。
5. 建立唯一 `RenderInputDesc`：prepare / validate 后产出 pipeline-facing 描述、diagnostics 和 stats，pipeline cache / executor 只消费这一类 prepared result。
6. 删除旧 union-like `RenderWorkItem` / `RenderWorkKind` / `.kind` / `RenderBatch` / `RenderBatchAnalysis` / `RenderIndirectBatch` / `ComputeAnalysis` 作为代码概念；不得保留 production type、helper、adapter、alias 或 fallback。
7. 把旧概念中已经实现且仍然正确的逻辑迁移到统一后的新概念中，不因删除旧类型而丢失行为覆盖。
8. 在 notes 概念文档中记录唯一术语、唯一流程和旧概念删除边界，避免后续 agent 重新发明同义类。

## 非目标

- 不改变 Material v3 source contract、BSDF 字段或 shader ABI；这些由 `REQ-073-a` / `REQ-073-b` / `REQ-073-c` 处理。
- 不实现 OfflineRT graph asset、offline shader URI 迁移或 offline scene loading hard cut；由 `REQ-073-g` 处理。
- 不扩展 transparent/BMW realtime path；由 `REQ-073-f` / `REQ-073-h` 处理。
- 不实现 Vulkan pipeline cache blob 持久化；由 `REQ-074-e` 处理。
- 不把旧 `RenderWorkQueue` 改名成另一个 public owner 后保留。

## 需求

### R1: Unique Render Work Vocabulary

完成态只允许以下 public 底层概念和类名。命名选择以改动小、语义清楚为准：保留已经存在的 `RenderDrawInput` 名称，把它纳入 `RenderInput` family；新增 compute 对应的 `RenderComputeInput`；用 `RenderInputDesc` 表达唯一 pipeline-facing prepared result；用 `RenderWorkCompiler` 表达 FrameGraph 后的唯一 work 编译入口。不得再引入等价同义 public API。

| 概念 / 类名 | 唯一职责 |
|---|---|
| `RenderPathGraph` | asset-level pass/source/target/shader contract |
| `FrameGraph` | frame-level pass collection and graph compile entry |
| `FramePass` | per-pass / per-node pass and input contract record |
| `CompiledFrameGraph` | ordered pass records with source pass indices / refs、producer/consumer DAG、barrier/resource-state planning dependencies |
| `RenderDomain` | domain selection fact for realtime / offline / future domain-specific payload selection |
| `RenderWorkCompiler` | typed compiler selection and compile entry |
| `RenderInput` | typed input base with shared identity / diagnostic context only |
| `RenderDrawInput` | raster draw input only |
| `RenderComputeInput` | compute dispatch input only |
| `RenderInputDesc` | 唯一 prepared/validated result：`inputIndex`、pipeline-facing descriptor、binding/resource facts、diagnostics、stats |
| `PipelineBuildDesc` | backend pipeline build input derived from `RenderInputDesc` |

旧概念处理：

- `RenderWorkQueue` 类型、字段、文件和 API 必须从 production code 删除；`FramePass` 是唯一 per-pass pass/input contract record。
- `RenderWorkItem` 类型必须从 production code 删除；不得改名成新的 union-like DTO。
- `RenderWorkKind` / `.kind` / `DirectRasterPass` / `ComputeDispatch` / `RayTracingDispatch` route selector 必须从 production code 删除；routing 只来自 typed input / typed compiler selection。
- `DirectRasterWorkPayload`、`ComputeDispatchWorkPayload` 等旧 `RenderWorkItem` payload 类型必须删除；仍然需要的 draw / dispatch 字段移动到 `RenderDrawInput` 或 `RenderComputeInput`。
- `RenderBatch`、`RenderBatchAnalysis`、`RenderBatchDiagnostic`、`RenderBatchStats`、`RenderBatchPipelineFacts`、`RenderBatchGeometryResources`、`RenderIndirectBatch` 等 public batch/result 类型必须删除；它们承载的 prepared candidate 和 indirect command 数据进入 typed `RenderInput`，diagnostic、stats、pipeline facts 和 geometry binding validation 进入 `RenderInputDesc`。
- `RenderInputAnalysis`、`RenderBatchAnalysis`、`ComputeAnalysis` 或任何同义 analysis/result 类型不得存在；唯一 prepared result 是 `RenderInputDesc`。
- `Opaque*Batch`、`Opaque*Geometry`、`Opaque*Indirect`、`Offline*Compiler`、`Offline*Work` 等 domain-only duplicate types 在完成态必须不存在；现有命中删除，新命中禁止。不得通过改名、adapter、wrapper 或 namespace 移动保留第二条 work pipeline。

命名约束：

- `RenderWorkCompiler`、`RenderInput`、`RenderDrawInput`、`RenderComputeInput`、`RenderInputDesc` 是本 REQ 的最终 public 类名。
- 代码、测试、REQ 和概念文档必须统一到这套词表，不得再引入 `FramePassCompiler`、`RenderSubmissionDesc`、`RenderBatchResult`、`ComputeWorkInput` 等同义 public API。
- 可以把当前过渡 `RenderDrawInput final` 演进成最终 raster input 类型；但旧 owner / union / batch-result / pass-name special-case 类型不得复用为新角色。
- R10 zero-hit audit 不得因为新类名落地而放松；新增类名必须替代旧词，而不是和旧词并存。

### R2: Existing Logic Moves Into The Unified Concepts

本 REQ 是概念统一和 hard cut，不是把已有正确能力丢掉后重新发明一套。删除旧类型时，旧路径里仍然需要的逻辑必须移动到统一后的对应概念。下表记录的是 hard cut 前的迁移来源，不是当前 active API：

| 旧逻辑来源 | 迁移到 |
|---|---|
| `RenderWorkQueue::build()` 中的 pass-local scene traversal / filter / context assembly | `FramePass` context build + `RenderWorkCompiler` input build |
| `RenderWorkItem` 中仍然有效的 shader、descriptor、target、pipeline identity、debug identity 数据 | draw / dispatch data 进入 typed `RenderInput`；pipeline / binding / dependency / diagnostic facts 进入 desc |
| `compileIndirectBatches()` / `RenderBatchAnalysis` / `RenderBatch` 中仍然正确的排序、合批、indirect command coverage、diagnostics / stats | `RenderWorkCompiler` raster policy；commands / coverage 进入 typed `RenderInput`，diagnostics / stats 进入 `RenderInputDesc` |
| old compute item 中仍然正确的 dispatch group、local size、readback/output mapping | `RenderComputeInput` + compute `RenderInputDesc` |
| `PipelineBuildDesc::fromRenderWorkItem()` 中仍然正确的 pipeline build facts | `RenderInputDesc.pipelineBuildDesc` 构建逻辑 |
| `PipelineBuildDesc::fromRenderBatch()` / backend `getOrCreatePipeline(RenderBatch, context)` 中仍然正确的 raster pipeline facts | `RenderInputDesc.pipelineBuildDesc` 构建逻辑 |
| backend `getOrCreatePipeline(RenderWorkItem)` / command recording / `executeRenderBatch()` 中仍然正确的 pipeline cache、descriptor binding、draw/dispatch submission steps | 消费 `RenderInputDesc` 的 shared pipeline/resource manager/executor path |
| `buildRenderUploadPlan(RenderWorkQueue)` 中仍然正确的 resource sync collection | 消费 `RenderInputDesc` source/target/descriptor resource refs 的 shared upload/sync plan |
| `VulkanRealtimeRenderBatchStats` / metadata `renderBatchStats` 中仍然正确的 coverage counters | `RenderInputDesc` / executor stats metadata，字段名不再使用 batch-only identity |

要求：

- 迁移后的逻辑必须挂在 `FramePass`、`RenderWorkCompiler`、`RenderInput`、`RenderInputDesc` 或 `PipelineBuildDesc` 的职责边界内。
- 不得为了保留旧实现而新建 wrapper、adapter、compat path、shadow DTO 或 domain-only public class。
- 已有行为测试或 smoke 覆盖的能力，在 hard cut 后必须由新路径继续覆盖。
- 如果旧逻辑和新模型冲突，以新模型为准；冲突逻辑删除，不用改名保留。

### R3: FrameGraph Compile Boundary

`FrameGraph::compile()` SHALL 产出 `CompiledFrameGraph`，职责限于 graph 层：

- 校验 graph-declared reads/writes 的 vocabulary、producer/consumer DAG、imported/source/target/write-mode 规则。
- 输出 ordered pass records with source pass indices / refs。
- 保留 barrier / resource-state planning 所需的 source/target dependency edges。
- 保留 pass id、target、reads/writes、shader URI、stage/dispatch、node signature 等 structural facts。

`FrameGraph::compile()` SHALL NOT：

- 生成 `RenderInput` 或 `RenderInputDesc`。
- 遍历 scene renderables、mesh、material、primitive 或 offline storage。
- 创建 pipeline key / pipeline build desc。
- 选择 raster/compute compiler。
- 根据 pass name、shader path substring 或 OfflineRT special case 改写 pass。

### R4: FramePass Stores Pass/Input Contract

`FramePass` SHALL 是 per-pass / per-node 的 contract record。它持有 graph contract 和编译所需 pass-local state，并把 typed input 生成交给 `RenderWorkCompiler`。

要求：

- hard cut 前的旧 queue 字段必须删除；public API 不得暴露、返回或要求调用方接触旧 queue owner。
- `FramePass` 不直接持有 backend pipeline、Vulkan command buffer 或 GPU object ownership。
- `FramePass` 不复制 per-draw material/object/mesh indices；这些由 preparation 从 `RenderInput` 解析得到。
- `FramePass` 可以提供 domain-neutral context builder，但不得按 pass name 特判 offline / shadow / debug / post process。

### R5: RenderWorkCompiler Selection

`RenderWorkCompiler` SHALL 是 FrameGraph 后的唯一 work compiler abstraction。

selection 输入：

- `RenderDomain`。
- `FramePass.stage`。
- `FramePass.dispatch`。
- RenderPath node contract：rendering / geometry / attachments / compute block / filters。
- resolved shader payload and shader reflection。
- domain context：realtime scene view、offline job/profile/storage view 或未来 compute source。

selection 规则：

- raster + draw 选择 raster draw/batch compiler。
- raster + fullscreen 选择 fullscreen raster compiler 或 raster fullscreen input path。
- compute + compute 选择 generic compute dispatch compiler。
- 不允许按 `pass == Pass_OfflineRayTrace`、shader URI substring、asset path substring 或 material template injection 决定 compiler。
- future realtime compute 和 OfflineRT compute 必须共用 compute compiler family；domain payload 可以不同。

### R6: RenderInput Family

`RenderInput` SHALL 是 typed input base，只保存所有 compiler 都真正共享的字段：

- stable input/debug identity。
- pass/node diagnostic context。
- optional link to `RenderInputDesc` 或 desc index。

不得放入：

- raster-only object / mesh / material refs。
- compute-only dispatch source / local size / output/readback refs。
- offline-only scene-wide storage refs。
- realtime-compute-only payload。

派生类：

- `RenderDrawInput`：object handle/ref、mesh handle/ref、material handle/ref、local primitive/submesh ref、debug identity、sort source data。
- `RenderComputeInput`：dispatch source、local size、shader reflection binding contract、source/target resource refs、readback/output contract、domain payload refs。

实现要求：

- C++ SHOULD 使用继承表达 input family 和 compiler family。
- ownership 使用值语义容器、`std::unique_ptr` 或 `std::shared_ptr`；不得引入 raw pointer ownership。
- 如局部使用 tagged variant，外部语义仍必须保持 typed derived input / typed compiler selection；不得退回 union-like `RenderWorkItem`。

### R7: RenderInputDesc As The Only Prepared Result

`RenderWorkCompiler` SHALL 产出 typed `RenderInput`，prepare / validate SHALL 把每个 input 转成 `RenderInputDesc`。`RenderInputDesc` 是唯一 public prepared / validated result；不得再引入 `RenderInputAnalysis`、`RenderBatchAnalysis`、`ComputeAnalysis` 或同义 result 类型。

`RenderInputDesc` 最低字段：

- accepted / rejected status。
- `inputIndex`，指向同一 pass-local `RenderInput[]` 中的 typed input。
- `PipelineKey`。
- `PipelineBuildDesc`。
- shader URI / final shader variant identity / reflection identity。
- descriptor/binding resolution result。
- source/target resource refs。
- barrier/readback mapping。
- backend submission structural signature。
- input/pass diagnostic identity。
- diagnostics。
- stats / coverage data。

要求：

- 每个 input 要么产出 accepted `RenderInputDesc`，要么产出 rejected `RenderInputDesc` with diagnostic。
- 失败 input 不得被当作 empty queue success。
- pipeline preload / cache lookup 只从 `RenderInputDesc.pipelineBuildDesc` 和 `PipelineKey` 获取信息。
- executor 不能从 raw `RenderInput` 重新推导 pipeline key、descriptor contract、batch compatibility 或 readback mapping。
- 如果实现需要容器，它只能是 `std::vector<RenderInputDesc>` 或普通 pass-local value container；不得给这个容器命名成新的概念类型。

### R8: Pipeline And Executor Consumption

backend pipeline path SHALL 从 `RenderInputDesc` 获取 pipeline。

要求：

- `PipelineBuildDesc::fromRenderWorkItem()` 必须删除。
- `PipelineBuildDesc::fromRenderBatch()` 必须删除；raster batch pipeline facts 必须从 `RenderInputDesc.pipelineBuildDesc` 取得。
- `VulkanResourceManager::getOrCreatePipeline(RenderWorkItem)` 必须删除；完成态正向 API 消费 `PipelineBuildDesc` / `RenderInputDesc`。
- `VulkanResourceManager::getOrCreatePipeline(RenderBatch, context)` 必须删除；不能保留 batch-specific pipeline cache entry。
- hard cut 前从 old queue/item 收集 pipeline desc 的路径必须删除；新 preload 入口只能消费 `RenderInputDesc[]`。
- Vulkan command recording 消费 typed `RenderInput` + `RenderInputDesc`，不得再读取旧 per-item raster data、old batch data 或 compute data。
- `VulkanCommandBuffer::executeRenderBatch()` 必须删除；indirect draw submission 由 desc-backed raster execution path 表达。
- realtime raster、fullscreen、compute 和 OfflineRT 都通过同一条 pipeline cache/resource manager 入口。

### R9: Concept Documentation

新增或更新 concepts 文档，说明唯一流程：

```text
RenderPathGraph
  -> FrameGraph { FramePass[] }
  -> FrameGraph::compile()
       => CompiledFrameGraph { ordered pass records with source pass indices / refs, resource DAG, barrier/resource-state plan }
  -> for each compiled FramePass:
       RenderWorkCompiler::compile(pass, domainContext)
       => typed RenderInput[]
       prepare / validate
       => RenderInputDesc[] { inputIndex, pipeline build data, binding/resource facts, diagnostics, stats }
  -> pipeline preload / resolve from RenderInputDesc.pipelineBuildDesc
  -> executor records draw/dispatch from typed RenderInput + RenderInputDesc
```

Desc records 不拥有也不携带执行数据；除 `inputIndex` 外，它们不保存 typed-input reference。draw / dispatch data 留在同一 pass-local `RenderInput[]` 侧。

文档要求：

- 明确 current code facts 和 active `REQ-073-e2` target 的差异。
- 列出保留概念、必须删除的旧概念和禁止重命名保留的旧概念。
- 说明 `RenderDrawInput` 不是 compute input。
- 说明 `FramePass` 替代并删除旧 `RenderWorkQueue`。
- 链接 `REQ-073-e`、`REQ-073-g` 和本 REQ。

### R10: Legacy Concept Hard Cut Audit

完成时必须有 rg audit，证明旧概念已从代码、assets 和测试中删除干净：

```bash
rg -n "RenderWorkItem|RenderWorkKind|DirectRasterWorkPayload|ComputeDispatchWorkPayload|DirectRasterPassPurpose|RenderWorkQueue|RenderBatch\\b|RenderBatchAnalysis|RenderBatchDiagnostic|RenderBatchStats|RenderBatchPipelineFacts|RenderBatchGeometryResources|RenderIndirectBatch|compileIndirectBatches|executeRenderBatch|fromRenderBatch|getOrCreatePipeline\\(.*RenderWorkItem|getOrCreatePipeline\\(.*RenderBatch|fromRenderWorkItem|Pass_OfflineRayTrace|OfflinePrimaryRayCompute" src assets
rg -n "RenderInputAnalysis|ComputeAnalysis|OpaqueBatch|OpaqueGeometry|OpaqueIndirect|Offline.*Compiler|Offline.*Work|compilerBatch|renderBatchStats|VulkanRealtimeRenderBatchStats|VulkanRenderBatchSubmissionStats" src assets
```

这两组命令在 `src` 和 `assets` 中必须没有输出。测试代码也不得保留旧概念 token；如果需要验证旧路径不可用，测试应断言新 API 行为或编译边界，不在测试源码中继续引用旧类型名。旧术语只允许出现在 requirement / concept 文档的历史说明和审计命令里。若同名能力仍然需要，必须按 R2 迁移到 `FramePass`、`RenderWorkCompiler`、`RenderInput`、`RenderInputDesc` 的字段或派生类实现，而不是新建替代旧概念的同义类。

## 测试

### T1: Pre-Hard-Cut Characterization

立项时需要 characterization / failing tests 或 source audit 证明旧路径存在；hard cut 完成后，这些测试不得继续作为旧 API 的正向源码引用。历史泄漏点包括：

- `FramePass` 内嵌旧 queue owner。
- realtime material-source geometry 通过旧 public batch result positive path 提交，而不是 `RenderInputDesc`。
- direct helper raster / fullscreen / debug / IBL bake 等路径可从旧 union-like work item positive path 提交。
- offline compute 可通过旧 pass-name branch 创建旧 work item。
- pipeline 可从旧 work item 和旧 batch payload 派生。
- resource upload 可从旧 queue 派生。
- `RenderWorkCompiler` / `RenderInputDesc` / `RenderComputeInput` / base `RenderInput` 尚未落地。

这些 characterization 只用于驱动 hard cut。完成态必须删除对旧类型名的代码引用，最终审计以 R10 的 zero-hit rg 结果为准。

### T2: FrameGraph Compile Boundary

测试 `FrameGraph::compile()`：

- 输出 ordered `FramePass` / dependency plan。
- 不生成 `RenderInput` / `RenderInputDesc`。
- 不访问 scene/offline storage。
- 不创建 pipeline key / pipeline build desc。

### T3: Compiler Selection

构造 raster draw、raster fullscreen、compute pass，断言：

- selection 根据 domain + stage + dispatch + node contract + shader reflection。
- 不根据 pass name 或 shader URI substring。
- compute pass 选择 generic compute compiler。
- future realtime compute 和 OfflineRT compute 共用 compute compiler type / base API。

### T4: RenderInputDesc Preparation

测试 prepare / validate：

- 每个 input 有 accepted desc 或 diagnostic。
- `RenderInputDesc` 包含 accepted/rejected status、diagnostics、stats。
- pipeline desc 来自 `RenderInputDesc`。
- backend 不从 raw input 重新推导 pipeline key。

### T5: Backend Hard Cut

Vulkan focused tests / smoke：

- realtime geometry submission 消费 compiler-produced `RenderInputDesc`。
- compute dispatch submission 消费 compute `RenderInputDesc`。
- old work-item pipeline lookup 已从 production code 删除。
- old batch-specific pipeline lookup、old batch pipeline desc builder 和 old batch command execution 已从 production code 删除。
- old queue-derived upload/resource sync path 已从 production code 删除。
- `compilerBatch*`、`renderBatchStats`、`VulkanRealtimeRenderBatchStats`、`VulkanRenderBatchSubmissionStats` 等 batch-named positive metadata 已从 production code / smoke positive path 删除或改为 `RenderInputDesc` 命名。
- old direct/per-item fallback 不能让 test 成功。

### T6: Concept Uniqueness Audit

rg audit 断言旧概念在 `src` 和 `assets` 中 zero-hit，并且 docs 已指向本 REQ 的新模型。

### T7: Helmet Smoke

完成本 REQ 后必须运行 Helmet realtime smoke，断言：

- Helmet scene 通过 hard-cut 后的 `FramePass` / `RenderWorkCompiler` / `RenderInputDesc` 路径提交。
- backend 消费 compiler-produced `RenderInputDesc`，不经过旧 work item、旧 queue、old batch result 或旧 indirect batch。
- output non-black。
- `fallback-observed == 0`。
- smoke diagnostics 能显示 compiler input/desc count、pipeline preload/build count、submitted draw/dispatch count；测试源码和 metadata 不再使用 `compilerBatch*` / `RenderBatch*` 作为正向完成态术语。

## 修改范围

- `notes/concepts-design/rendering-pipeline/`
- `notes/nav.yml`
- `docs/superpowers/specs/2026-06-14-073-e-opaque-indirect-batching-design.md`
- `notes/requirements/073-e-indirect-material-batching-and-diagnostics.md`
- `notes/requirements/073-g-offlinert-render-path-graph-compute-path.md`
- `src/core/frame_graph/frame_graph*`
- deleted old frame-graph queue files。
- `src/core/frame_graph/render_input.*`
- `src/core/frame_graph/render_draw_input.*`
- `src/core/frame_graph/render_compute_input.*`
- `src/core/frame_graph/render_work_compiler.*`
- `src/core/pipeline/pipeline_build_desc.*`
- `src/backend/vulkan/details/resource_manager.*`
- `src/backend/vulkan/details/commands/command_buffer.*`
- realtime renderer submission path
- offline executor / integrator paths that consumed old work items before hard cut。
- render/framegraph/pipeline tests

## 边界与约束

- Use current repo facts only；不得只改命名不删双轨。
- 不新增第二套 public graph / contract system。
- 不把 `RenderWorkQueue` 改名后保留为第二 owner；旧 owner 类型必须删除。
- 不把 compute 塞进 `RenderDrawInput`。
- 不让 pass name、shader path substring 或 OfflineRT special case 决定 compiler。
- 不保留 old work item fallback success path；旧 item 类型和入口必须删除。
- 不把 docs 中的旧术语继续描述为推荐路径；旧事实只能作为历史/已删除说明，并必须标注 active hard cut。

## 依赖

- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。
- `REQ-073-e`: realtime opaque batching 的已有需求和测试上下文。

## 下游工作

- `REQ-073-f`: transparent / BMW realtime path 应使用本 REQ 的 single work compiler model。
- `REQ-073-g`: OfflineRT compute path 应依赖本 REQ 的 `RenderComputeInput` / compute compiler / `RenderInputDesc` 模型；不得新建 `Offline*Compiler` public path。
- `REQ-073-h`: hard cut 后的 Helmet/BMW offline smoke 和 package readiness gate。
- `REQ-074-e`: pipeline cache serialization 应从 `RenderInputDesc` / `PipelineBuildDesc` 收集 pipeline cache metadata。

## 实施状态

已实现，Task 9 文档与最终审计已关闭。原 Task 9 提交为 `3fc9ef214dd5701ac0ecbcbe06cb72103b634df9`；本次规格复审修正会以 `git commit --amend --no-edit` 更新同一个提交。由于 Git commit SHA 由提交内容计算，提交正文无法稳定自包含最终 amended SHA，最终 HEAD SHA 以本任务报告和 `git rev-parse HEAD` 为准。

已确认的完成态代码事实：

- `FramePass` 是 per-pass pass/input contract record；不再有旧 queue 字段。
- `RenderWorkCompiler` 是 FrameGraph 之后的唯一 work compiler 入口。
- `RenderDrawInput`、`RenderComputeInput` 是当前正向 typed input；`RenderInputDesc` 是当前正向 prepared result。
- `RenderInputDesc.pipelineBuildDesc` 驱动 pipeline lookup；upload plan 消费 `RenderInput[]` + `RenderInputDesc[]`。
- `validatePreparedRenderInputs()` 校验 descs；Helmet smoke 读取 `renderInputStats`。
- `Pass_OfflineRayTrace` 旧 token 已不在 `src` / `assets` 中；当前 offline 临时 graph pass 是 file-local `OfflineCompute`，后续 OfflineRT graph asset / shader side-channel hard cut 归 `REQ-073-g`。

Task 9 最终审计证据：

- 旧 queue/item/batch/offline pass token 在 `src` / `assets` 中 zero-hit。
- `scripts/notes/serve_site.sh --build` 完成；若站点存在 unrelated warnings，报告中单独列出。
- focused verification 覆盖 `test_render_path_graph_pass_contract`、`test_render_resource_parsers`、`test_frame_graph`、`test_pipeline_build_info`、`test_render_work_compiler`、`test_bindless_indirect_contract`、`test_bindless_validation_contract`、`test_vulkan_resource_manager`、`test_vulkan_command_buffer`。
- end-to-end smoke 覆盖 `lxe_editor`、`BuildTest`、`requires_video_device` CTest 和 Helmet standard-pbr realtime smoke。
- clean `build-review` gate 重新 configure/build，并验证 `assets/render_paths` 被同步到 build dir。
