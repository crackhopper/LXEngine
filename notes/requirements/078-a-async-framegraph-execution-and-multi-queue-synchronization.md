# REQ-078-a: Async FrameGraph Execution And Multi-Queue Synchronization

> 2026-06-14 新增：本 REQ 明确后置到 realtime / offline / 3DGS 可用闭环之后，不插入 `REQ-073-*`。短期 `REQ-073-e` 到 `REQ-077-e` 优先把 realtime、OfflineRT 和 3DGS 渲染效果做出来；本 REQ 集中处理 FrameGraph resource synchronization、split barrier、timeline semaphore、multi-queue scheduling、async compute 和 secondary command buffer parallel recording，作为后续性能与调度架构升级。

## 背景

当前 FrameGraph 和 Vulkan 执行层已经有一条可用但偏线性的路径：

- `RenderPathGraph` asset 声明 pass、sources、targets、shader、attachment 和 render state。
- `buildFrameGraphFromRenderPathGraph()` 把 `RenderPassNode` 转成 `FramePass`。
- `FrameGraph::compile()` 会检查 resource read/write 合同，按 producer -> consumer 关系生成 DAG 顺序，并叠加 phase / stable order 排序。
- realtime Vulkan 执行层按 `CompiledFrameGraphPass` 顺序录制 pass。
- offscreen pass 开始前，backend 根据 writes 创建 frame graph attachment 并 transition 到 color/depth attachment layout。
- offscreen pass 结束后，backend 把 writes transition 到 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`。
- sampled read 通过 `FrameGraphSampledResource` 在 descriptor 绑定时解析为当前 frame 的 attachment。
- OfflineRT software compute 当前只在 compute output 到 host readback 前插入 shader-write -> host-read memory barrier。

这些现状足够支撑当前单 graphics queue、线性 pass 执行，但还不是完整的异步执行模型：

- 没有 formal resource access model。
- 没有 FrameGraph compile 输出的 barrier plan。
- 没有 split barrier release/acquire 语义。
- 没有 queue family ownership transfer。
- 没有 timeline semaphore 驱动的 graphics / compute / transfer queue 同步。
- 没有 async-aware FramePass queue affinity。
- 没有 secondary command buffer / worker thread parallel recording contract。
- 没有真实 workload 上的 async compute 性能验收。

如果继续把这些能力零散塞进 `REQ-073-e`、`REQ-073-j`、OfflineRT 或 3DGS 实现，会拖慢短期可用性目标，也会让同步 bug 难以归因。因此，本 REQ 作为后置集中升级：等 realtime / offline / 3DGS 都有真实 workload 后，再用统一 FrameGraph execution plan 一次性解决同步正确性和异步效率。

## 目标

1. 建立 FrameGraph resource access model 和 backend-neutral barrier plan。
2. 让 Vulkan backend 消费 barrier plan，而不是依赖 pass-local ad hoc transition。
3. 支持 split barrier、queue family ownership transfer 和 timeline semaphore。
4. 引入 async-aware FramePass queue affinity，支持 graphics / compute / transfer queue 调度。
5. 支持 fallback：无独立 compute queue 时仍可在 graphics queue 上正确执行。
6. 支持 secondary command buffer / worker thread parallel recording，让 independent pass 或 pass body 可并行录制。
7. 用 realtime、OfflineRT 和 3DGS 真实 workload 做 correctness + performance 验证。

## 非目标

- 不阻塞 `REQ-073-e` 的 opaque indirect batching。
- 不阻塞 `REQ-073-j` 的 transparent/BMW realtime smoke。
- 不阻塞 `REQ-074-h` / `REQ-074-i` 的 OfflineRT graph path 和 hard cut。
- 不阻塞 `REQ-077-a` 到 `REQ-077-e` 的 3DGS 可视化闭环。
- 不在本 REQ 之前强制替换所有现有 linear graphics queue 路径。
- 不实现完全自动的 workload profitability scheduler；本 REQ 的 async 调度以显式 pass queue affinity 和可测规则为准。
- 不承诺所有硬件都有 FPS 提升；无独立 queue 或 GPU-bound 场景必须正确 fallback。

## 需求

### R1: FrameGraph Resource Access Model

FrameGraph SHALL 建立 backend-neutral resource access model，用于描述每个 pass 对 logical resource 的访问。

最低访问类型：

- color attachment write。
- depth/stencil attachment read/write。
- sampled texture read。
- storage buffer/image read。
- storage buffer/image write。
- transfer read/write。
- present。
- host readback。

要求：

- access model 使用 engine 层 enum / struct，不直接暴露 Vulkan `VkAccessFlags` 或 `VkPipelineStageFlags`。
- `FrameGraphRead` / `FrameGraphWrite` 或相邻 compile metadata 必须能表达 resource、access type、pass index、queue affinity 和 attachment kind。
- imported resources、feature resources、swapchain resources 和 frame graph attachments 都必须有清晰来源。
- 无法建模的访问必须 fail-fast 或输出 compile diagnostic，不能被 backend 默默补默认 barrier。

### R2: Barrier Plan Compile Output

FrameGraph compile SHALL 输出 barrier plan，描述 pass 之间和 pass 内必要的 resource state transition。

要求：

- barrier plan 保持 backend-neutral。
- barrier plan 能表达 image layout transition、buffer memory dependency、queue ownership transfer、timeline wait/signal 和 host readback dependency。
- compile diagnostics 必须报告 unknown resource、missing producer、illegal read/write cycle、unsupported access transition 和 ambiguous queue ownership。
- existing DAG pass order 仍然由 producer/consumer 依赖、phase 和 stable order 决定。
- barrier plan 不改变 `RenderPathGraph` 作为 pass/source/target source of truth 的地位。

### R3: Vulkan Barrier Plan Consumer

Vulkan backend SHALL 消费 barrier plan 来录制 barrier，而不是把同步逻辑散落在 pass helper 中。

要求：

- 当前 `transitionPassWritesToShaderRead()`、offscreen write layout transition、swapchain present transition、dump/readback transition 等逻辑逐步迁移到 plan consumer。
- Vulkan consumer 负责把 backend-neutral access 映射到 stage/access/layout。
- plan consumer 必须支持 `VK_KHR_synchronization2` 可用路径；若暂时保留 sync 1.0，也必须在代码中有明确兼容边界和后续删除点。
- validation layer 测试必须覆盖 read-after-write、write-after-read、write-after-write、transfer/readback 和 present。

### R4: Split Barrier And Queue Ownership Transfer

跨 queue 或可重排工作 SHALL 使用 split barrier 表达 release/acquire。

要求：

- graphics -> compute、compute -> graphics、transfer -> graphics、graphics -> transfer 都有明确 release/acquire plan。
- exclusive sharing mode resource 必须有 queue family ownership transfer。
- same-family / same-queue fallback 不生成无意义 ownership transfer。
- split barrier 中间允许录制和提交 independent work，但不得越过真实 producer/consumer dependency。
- diagnostics 必须能指出 barrier plan 中的 producer pass、consumer pass、resource name、source queue 和 destination queue。

### R5: Timeline Semaphore And Queue Submission Model

backend SHALL 使用 timeline semaphore 或等价 timeline abstraction 表达多 queue submit dependency。

要求：

- graphics、compute、transfer queue 都有可查询 timeline state。
- pass execution plan 输出 submit batch，包含 command buffers、wait timeline、signal timeline 和 queue target。
- resource retirement / garbage collection 依赖 timeline completion，而不是仅依赖 frame index。
- 无 timeline semaphore 支持时，必须 fail-fast 或降级到明确定义的 binary semaphore / single queue fallback。
- frame overlap 不得让 frame graph attachment 或 transient buffer 被过早回收。

### R6: Async-Aware FramePass Queue Affinity

FramePass SHALL 支持 queue affinity。

最低 queue kind：

- Graphics。
- Compute。
- Transfer。

要求：

- raster / present pass 默认 Graphics。
- compute pass 默认 Compute，但无独立 compute queue 时 fallback 到 Graphics。
- upload / copy / readback 可选择 Transfer，若无 transfer queue fallback 到 Graphics。
- queue affinity 来自 RenderPathGraph pass contract、offline job contract 或 explicit backend policy，不得通过 pass name substring 推断。
- unsupported pass / queue combination 输出 diagnostic。

### R7: Parallel Command Recording

FrameGraph execution SHALL 支持 command recording 并行化的稳定边界。

要求：

- 独立 pass 或 pass body 可以录入 secondary command buffer。
- worker thread 不直接修改 shared Vulkan resource manager mutable state；需要预分配 descriptor/pipeline/framebuffer/attachment handles 或使用受控 command-recording context。
- primary command buffer 负责按 barrier plan 拼接 secondary command buffers。
- GUI overlay、swapchain final pass、readback 和 debug dump 等不可并行部分必须显式标记。
- profiling 输出 CPU recording time、submit time、GPU queue time 和 worker utilization。

### R8: Async Compute Workload Integration

本 REQ SHALL 至少选择一个真实 workload 验证 async compute。

候选优先级：

1. 3DGS culling / binning / tile preparation。
2. compute-based post-process，例如 bloom downsample/upsample 或 tone mapping。
3. GPU culling for indirect draw。
4. OfflineRT progressive compute pass。

要求：

- workload 必须有 sync graphics-only baseline。
- async path 必须有 pixel / output correctness comparison。
- performance report 至少包含 CPU frame time、GPU frame time、queue overlap、submitted command buffer count、barrier count 和 wait/signal count。
- 如果目标硬件没有独立 compute queue，测试必须证明 fallback 正确，并标记 no-overlap expected。

### R9: Hardware Fallback And Capability Reporting

renderer startup SHALL 报告 async capability。

要求：

- 是否存在独立 compute queue。
- 是否存在独立 transfer queue。
- 是否支持 timeline semaphore。
- 是否启用 synchronization2。
- 当前 FrameGraph execution mode：single-queue、multi-queue-fallback、multi-queue-async。
- fallback 状态必须进入 debug UI / log / test probe，便于解释性能结果。

### R10: Migration And Hard Cut

本 REQ 完成时，默认 FrameGraph execution 不得再依赖未登记的 ad hoc synchronization。

要求：

- production realtime/offline/3DGS path 的 resource synchronization 都可追溯到 barrier plan。
- 保留的 ad hoc barrier 必须属于 named debug/readback/bootstrap allowlist，并在文档中说明为什么不进入 barrier plan。
- rg audit 覆盖旧 transition helper、manual `vkCmdPipelineBarrier`、pass-name queue inference 和 unsynchronized attachment layout state。
- 若发现当前 path 依赖 implicit ordering 才能工作，必须加负向测试或 validation-layer repro。

## 测试

### T1: FrameGraph Barrier Plan Unit Tests

构造最小 graph：

- color attachment write -> sampled read。
- depth write -> sampled read。
- compute storage write -> graphics sampled/storage read。
- transfer write -> graphics read。
- graphics write -> host readback。

断言 compile 输出 expected pass order、barrier count、resource name、access transition 和 queue dependency。

### T2: Cycle And Unsupported Transition Diagnostics

构造非法 graph：

- 同 resource 无合法 write mode 的重复写。
- graphics / compute 双向 cycle。
- unknown queue kind。
- storage write 后缺少 modeled consumer access。
- imported resource 被写。

断言 diagnostic 精确包含 pass、resource、access 和 queue。

### T3: Vulkan Validation Layer Smoke

在 validation layer 下运行 small realtime graph：

- Forward -> PostProcess。
- Deferred -> DeferredLighting -> PostProcess。
- Shadow -> Forward。
- readback dump。

要求无 Vulkan synchronization validation error。

### T4: Cross-Queue Ownership Transfer Test

compute queue 写 buffer/image，graphics queue 读取。

要求：

- 独立 queue 硬件上使用 release/acquire ownership transfer。
- single queue fallback 上不生成 ownership transfer。
- validation layer 通过。
- 输出结果正确。

### T5: Timeline Submit And Retirement Test

构造多帧 transient resources。

要求：

- resource retirement 等待对应 timeline completion。
- frame overlap 下不会提前释放 frame graph attachment、descriptor set、staging buffer。
- timeline wait/signal 数量符合 execution plan。

### T6: Parallel Recording Test

构造多个 independent pass 或 large pass body。

要求：

- secondary command buffer recording 可在 worker threads 并行执行。
- primary command buffer 拼接顺序与 barrier plan 一致。
- 关闭 parallel recording 后输出一致。
- CPU recording time probe 可用。

### T7: Real Workload Performance Gate

选择 3DGS、compute post-process、GPU culling 或 OfflineRT progressive compute 中至少一个。

要求：

- baseline 与 async 输出一致或在允许阈值内。
- 报告 CPU/GPU frame time 和 queue overlap。
- 无独立 compute queue 时明确记录 fallback，不把无提升视为失败。
- 有独立 compute queue 且 workload 适合 async 时，至少证明 barrier/wait 没有导致明显回归；如果没有 FPS 提升，报告瓶颈归因。

### T8: rg Audit

完成报告必须包含：

```bash
rg -n "vkCmdPipelineBarrier|pipelineBarrier\\(" src/backend/vulkan src/test
rg -n "transitionPassWritesToShaderRead|transitionFrameGraphAttachment|updateFrameGraphAttachmentLayout" src/backend/vulkan src/test
rg -n "Pass_.*Compute|queue.*substring|pass.*name.*queue" src/core src/backend src/test
rg -n "timeline|semaphore|ownership transfer|queue family" src/core src/backend src/test
```

剩余 hit 必须是 barrier plan consumer、named debug/readback/bootstrap allowlist、tests 或 diagnostics。

## 修改范围

- `src/core/frame_graph/*`
- `src/core/rhi/*`
- `src/core/pipeline/*`
- `src/backend/vulkan/details/device.*`
- `src/backend/vulkan/details/commands/*`
- `src/backend/vulkan/details/resource_manager.*`
- `src/backend/vulkan/vulkan_realtime_renderer.*`
- `src/backend/vulkan/offline/*`
- future 3DGS Vulkan pass files
- validation / integration tests under `src/test`
- notes under `notes/concepts-design/rendering-pipeline/` after implementation

## 边界和约束

- Use current repo facts only；不得把旧调研中的“LX 完全没有 compute 路径”当成当前事实，因为当前代码已经有 compute pipeline / OfflineRT software compute 基础。
- 不引入第二套 public graph contract；RenderPathGraph / RenderPassNode / FrameGraph 仍是 source of truth。
- 不通过 pass name substring 推断 queue / access / barrier。
- 不用 barrier plan 掩盖缺失 resource dependency；缺少 producer 或 missing resource 必须继续 fail-fast。
- 不为了 async 重开旧 per-item descriptor / direct draw fallback。
- 不要求所有 workload 都 async；不适合 async 的 pass 应明确留在 Graphics queue。
- fallback 是 first-class 行为，不是隐藏兼容路径。

## 依赖

- `REQ-073-e`: indirect batching / diagnostics，提供稳定 draw submission 和 batch stats baseline。
- `REQ-073-j`: transparent/BMW realtime path，提供更复杂 realtime graph 和 visual smoke。
- `REQ-074-h` / `REQ-074-i`: OfflineRT graph path 和 hard cut，提供 compute path baseline。
- `REQ-077-a` 到 `REQ-077-e`: 3DGS loader/runtime/render/editor/tutorial，提供 compute-suitable 或 high-throughput GPU workload。
- `notes/roadmaps/research/async-compute/`: async compute 调研路线，作为设计参考但不直接占用 active 编号。
- `notes/roadmaps/research/frame-graph/`: frame graph barrier / compile 研究，作为 barrier plan 的概念参考。

## 下游工作

- GPU-driven rendering：GPU culling、meshlet、draw indirect compaction。
- advanced async scheduler：自动判断 pass 是否适合 async。
- ray tracing / BLAS build async queue。
- multi compute queue workload balancing。
- full synchronization2 hard cut if initial implementation keeps sync 1.0 compatibility.

## 实施状态

- 状态：未开始，后置到短期渲染可用性目标之后。
- 当前不影响 `REQ-073-*`、`REQ-074-*`、`REQ-073-h`、`REQ-076-*` 和 `REQ-077-*` 的实施顺序。
- 触发条件：realtime / OfflineRT / 3DGS 至少形成一个可测性能基线，或者性能分析显示 CPU command recording、queue idle gap、compute-suitable workload 已经成为明确瓶颈。
