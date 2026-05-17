# REQ-042-a: v0.1.1 — FrameGraph v1 resource / target / pass execution

## 背景

当前 `FrameGraph` 已经能按 `FramePass{name, target, queue}` 从 `Scene` 构建多条 `RenderQueue`，但它还只是加载期的 queue 组织器。`RenderTarget` 只有 format / depth / sample 的形状描述，没有真实 GPU attachment 生命周期；`FramePass` 也没有声明读写资源，Vulkan renderer 仍主要围绕 swapchain forward pass 执行。

如果要实现 shadow，第一步不是先写阴影公式，而是让一帧能表达：

```text
Shadow pass writes shadow.depth
Forward pass reads shadow.depth and writes swapchain color/depth
```

本需求把 FrameGraph 从“多 pass queue 容器”推进到 v0.1.1 所需的最小资源图。

## 目标

1. 支持 offscreen attachment 的创建、复用和销毁。
2. 让 `FramePass` 能声明 pass 输入输出资源。
3. 让 Vulkan renderer 按 compiled frame graph 执行多 pass。
4. 支持同 graphics queue 内的 image layout transition / barrier。
5. 为 shadow pass、CSM 和后续 HDR / G-Buffer 保留清晰扩展点。

## 需求

### R1: RenderTargetDesc 与 RenderTarget binding 分层

引入 target 描述与实际绑定的分层：

| 对象 | 角色 | 不负责什么 |
|---|---|---|
| `RenderTargetDesc` | 描述 format、extent policy、sample count、attachment kind、layer count | 不持有 Vulkan image |
| `RenderTarget` 或 backend binding | 绑定实际 attachment 资源、image view、framebuffer 等后端对象 | 不参与 scene 语义 |

`FramePass` 应优先持有描述信息，而不是直接持有 Vulkan 资源。

### R2: FrameGraphResource

新增 frame graph resource 概念，用稳定名字表达 pass 之间的资源流：

| 示例名 | 用途 |
|---|---|
| `shadow.depth` | shadow pass 输出的 depth texture |
| `swapchain.color` | 最终呈现 color |
| `swapchain.depth` | forward pass 使用的 depth |

首版只要求支持 color / depth attachment 与 sampled image 读，不要求 aliasing。

### R3: FramePass reads / writes

`FramePass` 需要声明：

| 字段 | 含义 |
|---|---|
| `name` | `Pass_Forward` / `Pass_Shadow` 等 pass identity |
| `reads` | 本 pass 采样或读取的 frame graph resource |
| `writes` | 本 pass 写入的 color/depth attachment |
| `queue` | 本 pass 的 `RenderQueue` |

声明顺序就是 v1 执行顺序。首版不做复杂自动重排。

### R4: compile() v1

`FrameGraph::compile()` 或等价入口需要完成：

- 校验每个 read 都有此前 pass 或外部资源提供。
- 校验每个 write 的 target desc 可被 backend 创建。
- 生成顺序执行计划。
- 发现明显的重复资源名、缺失资源和非法读写组合。

错误信息必须包含 pass name、resource name 和失败原因。

### R5: Vulkan pass execution

Vulkan renderer 应按 compiled execution plan 执行 pass：

- 为 offscreen resource 创建 image / view / framebuffer 或 dynamic rendering 等价封装。
- 每个 pass 使用自己的 color/depth attachment。
- `Pass_Forward` 可以继续写 swapchain，直到后续 HDR/Post 需求接管。
- 每个 pass 内复用已有 `RenderQueue` / `RenderingItem` / pipeline cache 路径。

### R6: Same-queue barrier v1

FrameGraph v1 需要覆盖 shadow 需求所需的最小同步：

- depth attachment write → shader sampled read。
- color attachment write → shader sampled read。
- undefined / clear → attachment write。
- attachment write → present read（swapchain）。

首版只支持 graphics queue 内 barrier，不做跨 queue ownership transfer。

### R7: Pipeline target identity

pipeline identity 需要能区分 depth-only、swapchain color/depth、未来 MRT/HDR 等 target 差异。首版可以通过 target signature 进入 `PipelineKey` 或等价 pipeline build desc 合同实现。

### R8: Debug inspection

至少提供可测试的内部状态输出：

- compiled pass 列表。
- 每个 pass 的 read/write resource。
- resource format / extent / layout 状态。

可以先作为测试辅助或日志，不要求 editor debug view。

### R9: 测试覆盖

覆盖：

- 两个 pass：A 写 offscreen color，B 采样 A 并写 swapchain 或测试 target。
- 一个 depth-only pass 写 depth，后续 pass 以 sampled image 读取。
- 缺失 resource read 报错包含 pass/resource。
- pipeline build desc 能区分 depth-only target 与 forward target。

## 修改范围

- `src/core/frame_graph/*`
- `src/core/pipeline/*`
- `src/core/rhi/*`
- `src/backend/vulkan/*`
- `openspec/specs/frame-graph/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`
- 相关 tests

## 边界与约束

- 本 REQ 不实现 shadow 采样公式。
- 本 REQ 不实现 HDR scene color、tone map、Bloom。
- 本 REQ 不实现 G-Buffer / deferred。
- 本 REQ 不实现 task-based 并行、async compute、multi-queue。
- 本 REQ 不要求 resource aliasing 或 pass reorder 优化。

## 依赖

- 当前 `FrameGraph` / `RenderQueue` / `RenderingItem`
- 当前 `PipelineKey` / `PipelineBuildDesc`
- 当前 Vulkan renderer、pipeline cache、descriptor binding by name

## 后续工作

- `REQ-042-b` Directional shadow map。
- `REQ-042-c` Cascaded Shadow Maps。
- HDR/Post、G-Buffer/Deferred、Task-based pass build 已移入 pending，不属于 v0.1.1 active 范围。

## 实施状态

未开始。v0.1.1 的第一项 active requirement。
