# REQ-073-e: Indirect Material Batching And Diagnostics

> 2026-06-13 顺延：原 `REQ-073-d` 因 `REQ-073-c` 进一步拆出 URI migration 而顺延为本 REQ。本 REQ 只负责让 realtime geometry pass 使用 source-reflected material records 和 bindless tables 进入 indirect / batched submission，并输出可解释的拆分诊断。

## 背景

`REQ-073-b` 已提供 bindless-ready texture/material/object/draw/mesh table，并证明 backend/GPU resource table 能建立对应 slot/staging；`REQ-073-c` 会让 pass shader 和 pipeline identity 使用 material source variant；`REQ-073-d` 会把默认 shader URI 和术语硬切到 `render_paths/...`。此时 renderer 才有足够结构事实把 draw submission 从“每材质 descriptor / 每 item fallback”推进到按兼容签名分批的 indirect path。

本 REQ 的核心是 batching 和 diagnostics，不负责最终删除旧 fallback。最终硬切和视觉 smoke 由 `REQ-073-f` 完成。

## 承接自 073-a / 073-b 的未完成项

| 来源 | 本 REQ 承接内容 | 为什么属于 073-e |
|---|---|---|
| `REQ-073-a` T7 / T10 | 同 source 不因材质参数值或贴图存在性拆 batch 的 renderer 级验证 | source signature 已在合同层成立，但 batch 是否错误拆分只能在 RenderWorkQueue 消费最终 shader variant 和 bindless table 后验证 |
| `REQ-073-b` 未完成项 | RenderWorkQueue / geometry pass 默认消费 bindless table 并生成 indirect-capable work item | 073-b 已证明 table/staging 可上传；本 REQ 负责让实时提交路径真正消费这些 table |
| `REQ-073-b` 未完成项 | material source、batch、pipeline、draw 的集中 diagnostics profile | upload/backend diagnostics 只能说明数据表存在；batch/pipeline/draw 归因必须由 RenderWorkQueue 和 geometry pass 输出 |
| `REQ-073-a` / `REQ-073-b` Helmet/BMW 验证前置 | Helmet/BMW batching stats | 最终视觉 smoke 属于 073-f；本 REQ 先提供 material source、batch、pipeline、draw count 和 split reason，便于 073-f 判断失败原因 |

## 目标

1. Forward / Deferred geometry pass 默认生成 indirect-capable render work。
2. RenderWorkQueue 按真实结构签名分组，不按 material instance 字符串或 texture 存在性分组。
3. RenderWorkItem 只携带 source-local material index、object/draw/mesh offsets 等 table index。
4. 诊断每个不能合批或不能 indirect 的原因。
5. Helmet/BMW validation 可以看到 material source、batch、pipeline 和 draw count 统计。

## 非目标

- 不实现 material storage / backend table upload foundation；由 `REQ-073-b` 处理。
- 不实现 shader source variant；由 `REQ-073-c` 处理。
- 不迁移 shader URI / RenderPath 术语；由 `REQ-073-d` 处理。
- 不删除 realtime 旧 fallback；由 `REQ-073-f` 处理。
- 不要求 Helmet/BMW 最终视觉验收；由 `REQ-073-f` 处理。
- 不处理 OfflineRT compute work item；由 `REQ-073-g` 处理。

## 需求

### R1: Indirect-capable Geometry Work Items

Forward / Deferred geometry pass SHALL 从 SceneResourceTable upload view 创建 indirect-capable work item。

work item 至少引用：

| 字段 | 说明 |
|---|---|
| pass id / RenderPath | pass identity |
| material source signature | source-reflected material storage 选择 |
| source-local material index | material record index |
| object table index | transform / visibility / mesh reference |
| draw table range | draw command range |
| mesh/geometry table range | vertex/index/attribute stream range |
| pipeline key | variant shader + render state + target + layout |

规则：

- work item 不保存 backend descriptor object pointer。
- work item 不保存旧 `MaterialUBO` bytes。
- material URI / material name / texture id 不作为 batch key。

### R2: Batch Compatibility Signature

RenderWorkQueue SHALL 使用兼容签名分 batch。

最低签名：

- RenderPath / pass id。
- render target signature。
- vertex / mesh input layout signature。
- material source signature。
- shader source variant identity。
- pipeline key。
- global geometry buffer compatibility。
- draw command layout。

要求：

- 同 source / 不同 material 参数值的对象可以进入同一 batch。
- 常量-only 材质和 texture-backed 材质可以进入同一 batch，只要 source signature 相同。
- mesh/geometry table 暂时不能合并时必须输出拆分原因。

### R3: Bindless Descriptor Consumption

geometry pass descriptor resources SHALL 来自 global bindless-ready tables。

要求：

- texture、sampler、material storage、object、draw、mesh/geometry tables 可从 work item 追踪；这些 table/slot/staging 由 `REQ-073-b` 建立，本 REQ 负责让 RenderWorkQueue 和 geometry pass 默认消费它们。
- per-material descriptor 不能作为 indirect path 的成功条件。
- 如果 geometry pass 所需 table/staging 缺失或与 work item 不匹配，必须输出 unsupported diagnostic，而不是静默回退。

### R4: Split Diagnostics

RenderWorkQueue SHALL 对每个 batch 和每个 split 输出 diagnostics。

最低内容：

- pass id。
- material source signature。
- material count / object count / draw count。
- pipeline key。
- batch compatibility signature。
- split reason：target、pipeline、vertex layout、geometry buffer、unsupported source、missing table、backend capability 等。

### R5: Fail-fast Invalid Indexes

source-local material index、object index、draw range、mesh range 无效时 SHALL fail-fast。

禁止：

- clamp 到 0。
- 使用默认材质继续渲染。
- 跳过 draw 后把结果当作通过。

### R6: Validation Stats

validation profile SHALL 暴露 realtime batching stats：

- material source count。
- pipeline count。
- batch count。
- draw count。
- indirect-capable draw count。
- fallback / unsupported draw count 和原因。

这些统计供 `REQ-073-f` 的视觉 smoke 判断问题归因。

## 测试

### T1: Same Source Batch

构造同 source、不同参数值、不同 texture 的多个对象，断言它们共享 material signature，并进入同一 compatible batch。

### T2: Different Source Split

构造不同 material source 的对象，断言它们产生不同 batch / pipeline key，并输出 source signature split diagnostic。

### T3: Texture Presence Does Not Split

常量 `Kd` 和贴图 `Kd` 的材质在同 pass / target / vertex layout 下不因 texture presence 拆 batch。

### T4: Index-only Work Item

断言 RenderWorkItem 保存 table index / range，不保存旧 `MaterialUBO` bytes 或 per-material descriptor pointer。

### T5: Invalid Index Negative

覆盖无效 source-local material index、object index、draw range、mesh range，断言 fail-fast 且输出明确 diagnostic。

### T6: Batch Diagnostics

构造 target、pipeline、vertex layout、geometry buffer 等拆分场景，断言 diagnostics 能说明每个 split reason。

### T7: Helmet/BMW Stats

运行 Helmet/BMW 低分辨率或 headless validation stats，断言输出 material source、batch、pipeline、draw 和 unsupported reason 统计。视觉正确性不在本 REQ 判定。

## 修改范围

- `src/core/frame_graph/render_queue.*`
- `src/core/frame_graph/render_work_item*`
- `src/core/scene/scene_resource_table*`
- `src/core/scene/scene_gpu_records.*`
- Vulkan realtime submission / descriptor binding path consuming `REQ-073-b` tables
- validation diagnostics and tests

## 边界与约束

- 不写按 material source/type 的 shader runtime branch。
- 不因贴图存在性拆 pipeline 或 batch。
- 不用旧 `MaterialUBO` 或 per-material descriptor 证明 indirect path 成功。
- 暂时保留 fallback 时必须有 named diagnostic，不能把 fallback 伪装成 indirect。

## 依赖

- `REQ-073-b`: bindless-ready material/object/draw/mesh tables and backend table/staging foundation。
- `REQ-073-c`: material source shader variant 和 final shader reflection。
- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。

## 后续工作

- `REQ-073-f`: Realtime material path hard cut and smoke。
- `REQ-073-g`: OfflineRT RenderPathGraph compute path。

## 实施状态

未实施。
