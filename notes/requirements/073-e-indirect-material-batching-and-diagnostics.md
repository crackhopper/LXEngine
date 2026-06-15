# REQ-073-e: Realtime RenderInput Batching And Diagnostics

> 2026-06-15 Task 8/9 校准：`REQ-073-e2` hard cut 后已实现。旧 queue/item/batch public work 模型已经从 `src` / `assets` 删除；本文只保留 realtime opaque batching、diagnostics 和 Helmet smoke 的需求背景。当前正向实现必须按 `RenderWorkCompiler`、typed `RenderInput` 和 `RenderInputDesc` 说明，不再把旧模型写成 active owner 或 backend input。

## 背景

`REQ-073-b` 已提供 bindless-ready texture/material/object/draw/mesh table；`REQ-073-c` 已建立 material source variant 和 RenderPathNode pipeline identity；`REQ-073-d` 已把 shader URI 与术语硬切到 RenderPathGraph。073e 的原始目标是让 realtime geometry 从 scene renderables 进入统一 preparation/diagnostics，再由 Vulkan 提交 indirect draw。

当前完成态不再经过旧 public queue/batch 类型。我们现在按这一条主线理解代码：

```text
RenderPathGraph input
  -> FramePass input contract
  -> RenderWorkCompiler
  -> RenderInput[] payloads
  -> RenderInputDesc[] validation/pipeline/binding facts
  -> Vulkan pipeline/upload/execute
```

旧设计词表，例如旧 work owner、旧 work item、旧 batch analysis/result、旧 node-local context/data DTO，只能作为 073e 立项期历史或 073e2 删除对象出现；它们不是当前目标，也不是后续需求的扩展点。

## 当前完成态

| 环节 | 当前事实 |
|---|---|
| 图输入 | RenderPathGraph pass 的 `input` block 描述 scene renderables、fullscreen triangle、material requirement、geometry contract 和可选 object render class filter |
| pass contract | `FramePass` 持有 `RenderPassInputContract input`，同时保存 shader URI、stage、dispatch、sources/targets、render state 等 pass metadata |
| compiler | `RenderWorkCompiler::buildInputs()` 从 compiled FrameGraph 和 scene/resource facts 生成 `RenderDrawInput` / `RenderComputeInput` |
| prepared desc | `RenderWorkCompiler::prepare()` 生成 `RenderInputDesc[]`，包含 accepted/rejected 状态、pipeline build desc、binding plan、resource dependencies、diagnostics 和 stats |
| validation | `validatePreparedRenderInputs()` 校验 desc，而不是校验旧 batch/result 类型 |
| Vulkan | realtime submission 消费 accepted desc 的 pipeline/upload/execute facts；metadata 暴露 `renderInputStats` |

典型 realtime opaque input：

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

fullscreen/debug pass 使用单独输入：

```yaml
input:
  kind: fullscreen-triangle
```

当 pass 只接收特定对象类型时，graph 可以通过 `input.object.renderClass` 过滤。debug mesh 这类不要求材质的路径可以显式写 `material.required: false`，但仍要进入 typed `RenderInput` / `RenderInputDesc`，不能恢复旧 per-item fallback。

## 目标

1. RenderPathGraph input 是 realtime geometry 工作的结构来源，旧 top-level `filters` / `geometry` 不再作为新的正向 schema。
2. `FramePass` 是 per-pass pass/input contract record，内部持有 `RenderPassInputContract input`，不持有旧 queue。
3. `RenderWorkCompiler::buildInputs()` 为 `scene-renderables` 产生 `RenderDrawInput[]`，为 fullscreen/debug/compute 路径产生对应 typed input。
4. `RenderWorkCompiler::prepare()` 输出 `RenderInputDesc[]`，每个 desc 携带 validation、pipeline、binding 和 resource dependency facts。
5. Vulkan backend 默认从 accepted desc 提交 indirect-capable realtime draw；不能旁路 desc 回到旧 per-item geometry path。
6. diagnostics 和 stats 覆盖 input、accepted/rejected desc、submitted draw/dispatch、fallback observation，不能静默跳过。
7. Helmet realtime smoke 验证 opaque RenderPathGraph input、compiler-produced desc、Vulkan submission、非黑输出和 `fallbackObservedCount == 0`。

## 非目标

- 不实现 material storage / backend table upload foundation；由 `REQ-073-b` 处理。
- 不实现 shader source variant；由 `REQ-073-c` 处理。
- 不迁移 shader URI / RenderPath 术语；由 `REQ-073-d` 处理。
- 不处理非 opaque material/pass 扩展。
- 不实现 OfflineRT graph asset；由 `REQ-076-c` 继续处理。
- 不实现 package、BC7、pipeline cache serialization 或 offline/realtime equivalence。

## 需求

### R1: RenderPathGraph Input Contract

Realtime geometry SHALL 从 RenderPathGraph pass 的 `input` block 进入当前工作模型。`scene-renderables` 输入至少表达：

| 字段 | 说明 |
|---|---|
| `kind` | `scene-renderables`、`fullscreen-triangle` 等输入族 |
| `material.type` | 当前 pass 接收的 material type 列表 |
| `material.required` | 是否要求 scene renderable 绑定材质；debug mesh 可为 `false` |
| `geometry.vertex` | pass 需要的 vertex contract，例如 `position-only` |
| `geometry.topology` | pass 需要的 primitive topology，例如 `triangle-list` |
| `object.renderClass` | 可选对象 render class 过滤，用于只接收特定 renderable class |

parser 接受的字段必须进入 `RenderPassInputContract` 并被 compiler 消费。未知字段必须 fail-fast；不得接受后忽略。

### R2: FramePass Owns The Input Contract

`FramePass` SHALL 保存 RenderPathGraph pass metadata 和 `RenderPassInputContract input`。FrameGraph compile 负责 pass ordering/resource vocabulary；work generation 发生在 `RenderWorkCompiler` 中。

要求：

- pass identity、stage、dispatch、shader URI、sources/targets、render state 和 input contract 保持在 `FramePass`。
- graph builder 不创建旧 queue owner，不按 pass name 或 shader path 注入 work。
- `FramePass` 只描述 contract；typed GPU table index 由 compiler/preparation 从 scene/resource facts 派生。

### R3: Typed RenderInput Payloads

`RenderWorkCompiler::buildInputs()` SHALL 从 compiled FrameGraph、scene renderables、resource table upload view 和 pass input contract 生成 typed payload：

- `RenderDrawInput` 表示 object/mesh/material/submesh/render class 等 handle/ref-level draw facts。
- `RenderComputeInput` 表示 compute pass 的 shader/dispatch/domain payload facts。
- fullscreen/debug 输入也必须通过 typed `RenderInput` family 表达。

上游不得手写 typed GPU table index，也不得把旧 work item 字段搬运成新输入。

### R4: RenderInputDesc Preparation

`RenderWorkCompiler::prepare()` SHALL 把 typed inputs 解析为 `RenderInputDesc[]`：

```text
RenderPathGraph input
  -> FramePass input contract
  -> RenderWorkCompiler::buildInputs()
  -> RenderInput[] payloads
  -> RenderWorkCompiler::prepare()
  -> RenderInputDesc[] validation/pipeline/binding facts
```

每个 desc 至少表达：

- accepted / rejected status。
- `inputIndex`、input identity 和 pass identity。
- pipeline build desc 或 rejection diagnostic。
- binding plan 和 resource dependencies。
- validation / pipeline / binding / dependency / diagnostic / stats facts。
- `RenderInputStats` 聚合字段，包括 `compilerInputCount`、`acceptedInputCount`、`rejectedInputCount`、`submittedDrawCount`、`submittedDispatchCount`、`fallbackObservedCount`。

draw / dispatch execution data stays on the typed `RenderInput` (`RenderDrawInput` or `RenderComputeInput`); `RenderInputDesc` points back to it by `inputIndex`.

### R5: Diagnostics And Stats

`RenderInputDesc` SHALL 保证每个 input 有明确结果：要么 accepted 并进入 backend coverage，要么 rejected 并有 diagnostic。不得把缺 mesh、缺 material、zero count、unsupported resource 或 legacy input 当作成功。

diagnostics 至少包含 pass id、input index、input kind、material/source/object identity、可用的 table/range facts、pipeline key when available、split/rejection reason。

positive validation 中 `fallbackObservedCount == 0`。

### R6: Backend Consumption Hard Cut

Vulkan realtime geometry 默认路径 SHALL 消费 accepted `RenderInputDesc` 中的 pipeline/upload/execute facts。

要求：

- empty input 正常返回。
- rejected desc 输出 diagnostic，并保留 stats。
- successful desc 记录 submitted draw/dispatch coverage。
- backend command recording 不从 raw draw inputs 重新做一套 compatibility grouping。
- backend command recording 不恢复旧 per-item geometry fallback。
- submission observability 至少暴露 compiler input count、accepted/rejected count、submitted draw/dispatch count 和 fallback observation。

### R7: Duplicate Concept Hard Cut

本 REQ 完成后，production code 不得保留第二套可成功提交 realtime geometry 的旧 work 模型。旧 queue/item/batch public 类型、旧 direct/per-item material-source geometry fallback、queue-derived pipeline preload 和旧 batch stats 都已由 `REQ-073-e2` 删除；后续扩展必须直接扩展 `RenderWorkCompiler` / `RenderInputDesc`。

### R8: Helmet Opaque Smoke

Helmet realtime smoke SHALL 验证：

- converted Helmet scene loads through Material v3 source contract。
- opaque RenderPathGraph input path is used。
- final source-variant shader reflection is used。
- `RenderWorkCompiler` produces accepted desc for the scene-renderable path。
- Vulkan backend records submission from compiler-produced `RenderInputDesc`。
- output non-black。
- `fallbackObservedCount == 0`。
- no skipped input is treated as success。

## 测试

### T1: Input Contract Tests

测试覆盖：

- `scene-renderables` input schema 被解析进 `RenderPassInputContract`。
- `fullscreen-triangle` input 不要求 scene material/geometry。
- `input.object.renderClass` 过滤只接收匹配对象。
- `material.required: false` debug mesh path 不恢复旧 fallback。
- legacy top-level `filters` / `geometry` 在 strict profile 下失败。

### T2: RenderWorkCompiler Tests

构造同 object/material type contract 的多个 draw，断言进入 accepted `RenderInputDesc`，并记录 pipeline/build/binding facts。

### T3: Rejection Diagnostics

构造 unresolved material、unresolved draw record、invalid mesh/range、zero count 和 unsupported input，断言 exact reason、input identity 和 rejected count。

### T4: Backend Submission Tests

运行 Vulkan focused test 或 smoke harness，断言 realtime geometry 使用 compiler-produced `RenderInputDesc` 提交，而不是从 raw draw inputs 或旧 per-item submission path 重新提交。

### T5: Helmet Smoke

运行低分辨率 Helmet realtime smoke，断言非黑图、`renderInputStats`、submitted draw/pipeline stats、`fallbackObservedCount == 0`。

### T6: Hard Cut Audit

完成报告必须包含旧 production token zero-hit 审计。旧 token 只能在明确历史文档中出现，不能在 `src` / `assets` 中作为 active path 出现。

## 修改范围

- RenderPathGraph input parser / pass schema tests。
- `src/core/frame_graph/render_work_compiler.*`。
- `src/core/frame_graph/render_validation_contract.*`。
- `src/core/pipeline/pipeline_build_desc.*`。
- scene resource table upload view consumption。
- Vulkan realtime submission / descriptor binding path。
- validation diagnostics and tests。
- Helmet realtime smoke harness。

## 边界与约束

- 不写按 material source/type 的 runtime shader branch。
- 不直接比较 material instance identity 或 per-instance material data 来拆 pipeline 或 batch；draw/material table index 负责选择具体参数和资源。
- 不用旧 `MaterialUBO`、旧 PBR payload 或 per-material descriptor 证明 RenderInput path 成功。
- 不使用 `techniques/...`、material-local technique/defaultTechnique、legacy resolver fallback 或旧 shader source tree 作为正向输入。
- 不保留两个可通过的 realtime geometry 默认入口。
- 不用 path/name substring 选择 strictness；strictness 来自 validation profile/property。

## 依赖

- `REQ-073-b`: bindless-ready material/object/draw/mesh tables and backend table/staging foundation。
- `REQ-073-c`: material source shader variant 和 final shader reflection。
- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。
- `REQ-073-e2`: `FramePass` / `RenderWorkCompiler` / `RenderInput` / `RenderInputDesc` 单轨模型。

## 后续工作

- `REQ-076-c`: OfflineRT RenderPathGraph compute path。
- Additional non-opaque material/pass policies on the same compiler model。

## 实施状态

已由 `REQ-073-e2` 单轨 hard cut 承接底层实现；Task 9 文档复审修正后，本文只作为 realtime opaque batching 和 diagnostics 的需求背景保留。

当前代码事实：

- realtime geometry、fullscreen pass 和 offline compute 都通过 `RenderWorkCompiler::buildInputs()` / `prepare()` 产出 typed input 与 `RenderInputDesc`。
- Vulkan realtime metadata 使用 `renderInputStats`，Helmet smoke 验证 `fallbackObservedCount == 0`。
- 旧 public queue/item/batch 类型、旧 batch stats 和 queue-derived pipeline preload 不再是当前代码路径。

若后续继续扩展非 opaque policy，应直接扩展 `RenderWorkCompiler` / `RenderInputDesc` 字段与 diagnostics，而不是恢复旧 public work 或 batch/result 层。
