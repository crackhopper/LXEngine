# REQ-073-d: Indirect Material Batching And Diagnostics

> 2026-06-13 拆分：原 `REQ-073-b` 把 bindless data、shader variant、indirect batching 和 hard cut 混在一起。本 REQ 只负责让 realtime geometry pass 使用 source-reflected material records 和 bindless tables 进入 indirect / batched submission，并输出可解释的拆分诊断。

## 背景

`REQ-073-b` 会提供 bindless-ready texture/material/object/draw/mesh table；`REQ-073-c` 会让 pass shader 和 pipeline identity 使用 material source variant。此时 renderer 已经有足够结构事实把 draw submission 从“每材质 descriptor / 每 item fallback”推进到按兼容签名分批的 indirect path。

本 REQ 的核心是 batching 和 diagnostics，不负责最终删除旧 fallback。最终硬切和视觉 smoke 由 `REQ-073-e` 完成。

## 目标

1. Forward / Deferred geometry pass 默认生成 indirect-capable render work。
2. RenderWorkQueue 按真实结构签名分组，不按 material instance 字符串或 texture 存在性分组。
3. RenderWorkItem 只携带 source-local material index、object/draw/mesh offsets 等 table index。
4. 诊断每个不能合批或不能 indirect 的原因。
5. Helmet/BMW validation 可以看到 material source、batch、pipeline 和 draw count 统计。

## 非目标

- 不实现 material storage foundation；由 `REQ-073-b` 处理。
- 不实现 shader source variant；由 `REQ-073-c` 处理。
- 不删除 realtime 旧 fallback；由 `REQ-073-e` 处理。
- 不要求 Helmet/BMW 最终视觉验收；由 `REQ-073-e` 处理。
- 不处理 OfflineRT compute work item；由 `REQ-073-f` 处理。

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

- texture、sampler、material storage、object、draw、mesh/geometry tables 可从 work item 追踪。
- per-material descriptor 不能作为 indirect path 的成功条件。
- 如果 backend 暂时无法消费某 table，必须输出 unsupported diagnostic，而不是静默回退。

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

这些统计供 `REQ-073-e` 的视觉 smoke 判断问题归因。

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
- Vulkan realtime submission / descriptor binding path
- validation diagnostics and tests

## 边界与约束

- 不写按 material source/type 的 shader runtime branch。
- 不因贴图存在性拆 pipeline 或 batch。
- 不用旧 `MaterialUBO` 或 per-material descriptor 证明 indirect path 成功。
- 暂时保留 fallback 时必须有 named diagnostic，不能把 fallback 伪装成 indirect。

## 依赖

- `REQ-073-b`: bindless-ready material/object/draw/mesh tables。
- `REQ-073-c`: material source shader variant 和 final shader reflection。

## 后续工作

- `REQ-073-e`: Realtime material path hard cut and smoke。
- `REQ-073-f`: OfflineRT RenderPathGraph compute path。

## 实施状态

未实施。
