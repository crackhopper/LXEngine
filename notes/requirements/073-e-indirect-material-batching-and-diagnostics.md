# REQ-073-e: RenderPathNode Indirect Batching And Diagnostics

> 2026-06-14 现状校准：`REQ-073-d` 正在执行 `techniques/...` 到 `render_paths/...` 的 URI / 术语硬切。本 REQ 的正向路径 SHALL 以 `REQ-073-d` 完成后的 RenderPathGraph shader URI、RenderPathNodeSignature 和 legacy URI rejection 作为输入前提；不得继续依赖 `assets/shaders/glsl/techniques/...`、material-local technique/defaultTechnique、legacy resolver fallback 或旧兼容测试 fixture。
>
> 2026-06-14 设计收束：本 REQ 负责把 realtime geometry 默认路径切到 `RenderPathNodeContext` / `RenderPathNodeData` / `RenderBatchCompiler` / `RenderBatchAnalysis` 模型，并删除旧双轨。`REQ-073-f` 只负责 transparent/BMW follow-up，不再承担 073e 的 fallback cleanup。

## 背景

`REQ-073-b` 已提供 bindless-ready texture/material/object/draw/mesh table；`REQ-073-c` 已把 material source variant 和 RenderPathNode pipeline identity 建立起来；`REQ-073-d` 负责把 shader URI 与术语硬切到 RenderPathGraph。此时 realtime geometry 可以从 object + mesh + bound material 生成 handle/ref-level draw input，再由 preparation 解析为 prepared candidate，最后按 batch signature 走 indirect submission。

当前代码已有 `RenderWorkQueue::compileIndirectBatches()`、`RenderIndirectBatch`、typed `drawRecordIndex` / `materialRefIndex` 和 `PipelineKey::build(materialTypeVariant, renderPathNodeSignature)`，但这只是过渡实现。它仍可能把旧 `RenderWorkItem` DTO、`RenderWorkKind` / `RasterDraw` / `RasterBatch`、`DescriptorResourceList` equality、target/geometry buffer identity 或 direct/per-item submission 当作成功路径。本 REQ 要把这些双轨全部硬切掉或隔离到非默认、非 realtime geometry 的命名路径。

## 目标

1. `RenderWorkQueue` 成为 RenderPathNode 级 work owner，内部数据模型收敛为 `RenderPathNodeContext` + `RenderPathNodeData`。
2. `RenderPathNodeData` 携带 handle/ref-level `RenderDrawInput[]`，不预填 typed GPU table indices。
3. `RenderBatchPreparation` 从 `SceneResourceTableUploadView` 解析 `RenderDrawInput`，输出 `PreparedRenderDrawCandidate[]`。
4. `RenderBatchCompiler` 作为通用 compiler，按 context 的 sort policy 排序，再合并相邻 compatible prepared candidates。
5. opaque geometry batch signature 只由 object data signature + material pipeline signature 决定。
6. backend realtime geometry 默认提交走 Vulkan indirect draw；旧 direct/per-item geometry 成功路径删除或命名隔离，不能作为 fallback。
7. diagnostics 和 stats 覆盖每个 input draw / prepared candidate：成功 batch、明确 split、明确 rejection，不能静默跳过。
8. Helmet realtime smoke 只验证 073e 的 opaque indirect path；BMW/glass/transparent 留给 073f。

## 非目标

- 不实现 material storage / backend table upload foundation；由 `REQ-073-b` 处理。
- 不实现 shader source variant；由 `REQ-073-c` 处理。
- 不迁移 shader URI / RenderPath 术语；由 `REQ-073-d` 处理。
- 不实现 transparent pass、transparent sorting、glass material、BMW converter/shader/smoke；由 `REQ-073-f` 处理。
- 不处理 OfflineRT compute path；由 `REQ-073-g` 处理。
- 不实现 package、BC7、pipeline cache serialization 或 offline/realtime equivalence。

## 需求

### R1: RenderPathNode Queue Data Model

`RenderWorkQueue` SHALL 保持为 per-node/per-pass work owner，但不得继续把 realtime geometry 表达成 old union-like `RenderWorkItem`。

目标数据模型：

| 概念 | 说明 |
|---|---|
| `RenderPathNodeContext` | pass identity、rendering mode、sort policy、render state defaults、target/attachment contract、geometry contract、object data ABI resolver、material signature resolver、global geometry table view、backend indirect capability |
| `RenderPathNodeData` | 当前 node 的 handle/ref-level `RenderDrawInput[]` |
| `RenderDrawInput` | object handle/ref、mesh handle/ref、material handle/ref、local primitive/submesh ref、debug identity、sort source data |
| `RenderBatchPreparation` | queue-owned stage/helper；使用 `SceneResourceTableUploadView` 把 input references 解析成 GPU table indices/ranges 和 indirect command payload，不是新的 public hierarchy |
| `PreparedRenderDrawCandidate` | typed object/draw indices、typed mesh table range、typed material ref/source-local material indices、object data signature、material pipeline signature、final shader reflection identity used for readiness、indirect draw counts/offsets、sort key |
| `RenderBatchCompiler` | 通用 batch compiler，不带 `Opaque` 前缀 |
| `RenderBatchAnalysis` | batches、diagnostics、stats |

`target`、attachments、render-state defaults 和 pass identity 是 node context，不复制到每个 input/candidate 上参与 batch 比较。typed GPU table index 不允许由上游手写或从旧 `RenderWorkItem` 字段搬运；它必须由 preparation 阶段从 upload view 生成。

来源/去向约束：

| 数据 | 来源 | 去向 |
|---|---|---|
| `RenderPathNodeContext` | 当前 RenderPathGraph node + renderer/backend pass state | preparation、compiler、backend submission 的 node scope |
| `RenderDrawInput` | scene/renderable traversal 中的 object、mesh/submesh、bound material reference | 只给 preparation |
| `SceneResourceTableUploadView` | `REQ-073-b` 的 SceneResourceTable upload build | 只给 preparation 做 index/range 解析 |
| final shader reflection | `REQ-073-c` 对当前 node 内 input material/source 的 material-source variant resolution | material signature resolver 与 readiness validation |
| object data signature | 当前 bindless object/draw table ABI resolver | prepared candidate 与 backend pipeline lookup |
| material pipeline signature | 当前 node 内对 input material 的 material signature resolver | prepared candidate 与 backend pipeline lookup |
| typed object/draw/material/mesh index/range | preparation 通过 `SceneResourceTableUploadView` 解析 `RenderDrawInput` 生成 | prepared candidate validation 与 indirect command generation |
| `PreparedRenderDrawCandidate` | preparation output | sort policy 与 batch merge |
| `RenderBatchAnalysis` | batch compiler output | Vulkan indirect submission 与 diagnostics/tests |

任何字段如果不是当前阶段能产出的事实，就不能被放进该阶段的输入结构。

### R2: Prepared Draw Candidate Readiness

opaque geometry node 内的 prepared draw candidate 只有在以下事实由 preparation 阶段显式解析后才 indirect-ready：

- valid mesh / geometry table range。
- non-zero index count 和 instance count。
- object data signature 已解析；当前 bindless 阶段为单一稳定值，例如 `BindlessObjectData.v1`。
- material pipeline signature 已解析。
- shader 消费 `SceneDraws` / `SceneObjects` 时，preparation 已由 input object reference 解析出 typed draw/object index。
- shader 消费 source-local material data 时，preparation 已由 input material reference 解析出 typed material ref 和 source-local material index。
- material storage 存在且 index/range 合法。
- final shader reflection 来自 material-source variant。

缺失数据 SHALL 形成 preparation error 或 batch diagnostic，不得回退 direct/per-item draw。

### R3: Batch Compatibility

batch compatibility SHALL 在一个 `RenderPathNodeContext` 内判断。两个 prepared draw candidate 可合批，当且仅当：

```text
object data signature == object data signature
material pipeline signature == material pipeline signature
```

不得作为 batch split key：

- `PipelineKey` 独立比较结果。
- `RenderPathNodeSignature` per-draw 拷贝。
- old mesh-derived `objectSignature`。
- material URI、material name、material 参数值、texture presence、texture id。
- per-material descriptor object identity 或 `DescriptorResourceList` equality。
- vertex layout、object topology、target/attachment、geometry buffer identity。
- old `MaterialUBO` / `SceneGpuMaterialRecord` PBR payload identity。
- `techniques/...` shader URI。

不同材质参数值或不同 texture slot 在 object data signature 和 material pipeline signature 相同时 SHALL 进入同一 batch。

### R4: Generic Batch Pipeline Flow

`RenderWorkQueue` 的 batch pipeline SHALL 使用同一流程支持 opaque 和未来 transparent node：

```text
RenderPathNodeData
  -> resolve RenderDrawInput through SceneResourceTableUploadView
  -> emit PreparedRenderDrawCandidate or preparation diagnostic
  -> validate prepared candidate readiness
  -> apply RenderPathNodeContext sort policy
  -> merge contiguous prepared candidates with the same batch signature
  -> return RenderBatchAnalysis
```

073e 只实现 opaque policy：opaque 可以为了 batch locality 排序/聚合，因为深度顺序不是语义约束。073f 在同一模型上补 transparent depth sort 和 adjacent-compatible merge。

### R5: Diagnostics And Stats

`RenderBatchAnalysis` SHALL 保证：

- 每个 input draw 要么被 preparation 拒绝并带 diagnostic，要么生成 prepared candidate；每个 prepared candidate 要么被一个 batch 覆盖，要么有一个 diagnostic。
- 没有 draw 被静默跳过。
- diagnostics 至少包含 input draw index、pass/node context、object data signature、material pipeline signature、material/source identity、prepared mesh/draw/material index/range when available、derived PipelineKey when available、split/rejection reason。
- stats 至少包含 input draw count、prepared candidate count、batch count、draw count、indirect-capable draw count、unsupported draw count、legacy-rejected draw count、fallback-observed count。
- positive validation 中 `fallback-observed == 0`。

合法 split/rejection reason 词表：

- `object-data-signature-mismatch`
- `material-pipeline-signature-mismatch`
- `source-material-ref-unresolved`
- `object-draw-record-unresolved`
- `invalid-source-material-ref`
- `invalid-draw-record`
- `missing-mesh-range`
- `invalid-mesh-range`
- `zero-index-count`
- `zero-instance-count`
- `global-geometry-table-missing`
- `backend-indirect-unsupported`
- `legacy-input-rejected`

不得把 `descriptor-resource-mismatch`、`vertex-layout-mismatch`、`topology-mismatch`、`target-mismatch`、`geometry-buffer-mismatch` 作为 realtime opaque geometry 的永久 split reason。

### R6: Backend Indirect Submission Hard Cut

Vulkan realtime geometry 默认路径 SHALL 消费 `RenderBatchAnalysis` 并提交 indirect draw。

要求：

- empty queue 正常返回。
- rejected analysis 输出首个 diagnostic，并保留完整 stats。
- successful analysis 记录 indirect draw batches。
- old direct/per-item geometry submission 不得作为 material-source geometry 的 fallback success path。
- 如果低层 direct draw helper 因 debug/fullscreen/test-only 保留，必须命名为非默认路径，且 rg audit 中列出 allowlist。

### R7: Duplicate Concept Hard Cut

本 REQ 完成时 SHALL 不存在第二套可成功提交 realtime geometry 的 batch/submission 概念。

必须删除或命名隔离：

- old union-like `RenderWorkItem` geometry routing。
- `RenderWorkKind` / `.kind` / `RasterDraw` / `RasterBatch` 作为 geometry batch 概念。
- `OpaqueBatch*`、`OpaqueGeometry*`、`OpaqueIndirect*` 等 opaque-only 并行类。
- `DescriptorResourceList` equality 作为 batch compatibility。
- target/attachment/topology/vertex layout/geometry buffer identity 作为 per-draw split key。
- direct/per-item geometry submission fallback。

### R8: Helmet Opaque Smoke

Helmet realtime smoke SHALL 验证：

- converted Helmet scene loads through Material v3 source contract。
- opaque RenderPathGraph path is used。
- final source-variant shader reflection is used。
- prepared draw candidates enter `RenderBatchCompiler`。
- Vulkan backend records indirect draw submission。
- output non-black。
- `fallback-observed == 0`。
- no skipped draw is treated as success。

## 测试

### T1: Batch Compiler Characterization

新增 failing tests：

- current descriptor-resource equality split 被证明为旧行为。
- `RenderWorkKind` / `.kind` / `RasterDraw` / `RasterBatch` 不属于 geometry batch contract。
- current bindless object data signature 是单一稳定值，参与 batch signature。
- old mesh-derived `objectSignature` 不能拆 opaque bindless batch。
- 不同 vertex buffer、topology、target/attachment 或 geometry buffer identity 不能成为永久 split key。
- zero index/instance、unresolved source material ref、unresolved draw record、invalid prepared material/draw/mesh index/range 输出 diagnostic。
- 每个 input draw 要么 preparation diagnostic，要么生成 prepared candidate；每个 prepared candidate 都 covered or diagnosed。

### T2: Same Signature Batching

构造同 object data signature、同 material pipeline signature、不同材质参数值和 texture slot 的多个 draw，断言进入同一 batch。

### T3: Split Diagnostics

构造 object data signature mismatch、material pipeline signature mismatch 和 invalid table/range data，断言 exact reason 和 input/prepared identity。

### T4: Backend Indirect Submission

运行 Vulkan focused test 或 smoke harness，断言 realtime opaque geometry 使用 indirect batch submission，而不是 direct/per-item draw submission。

### T5: Helmet Smoke

运行低分辨率 Helmet realtime smoke，断言非黑图、batch/draw/pipeline stats、indirect-capable draw count、`fallback-observed == 0`。

### T6: rg Hard Cut Audit

实现完成报告必须包含以下 rg 审计。命令可以按文件范围拆分，但普通 production / positive test hit 必须为 0；任何剩余 hit 必须是 named negative audit 或非 geometry debug/compute allowlist。

```bash
rg -n "OpaqueBatch|OpaqueGeometry|OpaqueIndirect" src/core src/backend src/test
rg -n "RenderWorkKind|RasterDraw|RasterBatch|\\.kind\\b" src/core src/backend src/test
rg -n "DescriptorResourceList|sameDescriptorResources|descriptor-resource-mismatch" src/core src/backend src/test
rg -n "vertex-layout-mismatch|topology-mismatch|target-mismatch|geometry-buffer-mismatch" src/core src/backend src/test
rg -n "executeWorkItem|direct.*draw|per-item" src/backend src/core src/test
```

## 修改范围

- `src/core/frame_graph/render_queue.*`
- new or renamed queue-owned node data / batch analysis types
- `src/core/scene/scene_resource_table*`
- `src/core/scene/scene_gpu_records.*`
- Vulkan realtime submission / descriptor binding path consuming `REQ-073-b` tables
- validation diagnostics and tests
- Helmet realtime smoke harness
- legacy input rejection audit from `REQ-073-d` handoff

## 边界与约束

- 不写按 material source/type 的 shader runtime branch。
- 不因贴图存在性、材质值、material URI、material name 拆 pipeline 或 batch。
- 不用旧 `MaterialUBO`、`SceneGpuMaterialRecord` PBR truth 或 per-material descriptor 证明 indirect path 成功。
- 不使用 `techniques/...`、material-local technique/defaultTechnique、legacy resolver fallback 或旧 shader source tree 作为正向 batching 输入。
- 不保留两个可通过的 realtime geometry 默认入口。
- 不用 path/name substring 选择 strictness；strictness 来自 validation profile/property。

## 依赖

- `REQ-073-b`: bindless-ready material/object/draw/mesh tables and backend table/staging foundation。
- `REQ-073-c`: material source shader variant 和 final shader reflection。
- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。

## 后续工作

- `REQ-073-f`: Transparent sorting/batching, glass material support, BMW converter/shader coverage and BMW realtime smoke。
- `REQ-073-g`: OfflineRT RenderPathGraph compute path。

## 实施状态

未实施。
