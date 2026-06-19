# 渲染管线：从场景到多 Pass 提交的路线图

渲染管线负责把实时 `Scene` 或离线 `OfflineRenderJob` 组织成一组有顺序的 `FramePass`，再把每个 pass 编译成可提交的 draw / dispatch input，交给 Vulkan backend 录制和提交。

我们可以把它想成一条工厂生产线：`RenderPathGraph` 是流程图，`FrameGraph` 是排程表，`FramePass` 是每道工序，`RenderWorkCompiler` 把这道工序编译成 typed `RenderInput` 和 `RenderInputDesc`。Realtime 和 offline 的入口不同，但它们应该使用同一条底层 work 编译线。

| 对象 | 当前角色 | 生产线类比 |
|---|---|---|
| `RenderPathGraph` | 从 asset 声明 pass、shader、source/target 和 pipeline 合同 | 工厂流程图 |
| `FrameGraph` | 保存一帧的 pass、read/write 声明和 compile 校验 | 工序排程表 |
| `FramePass` | 单个 pass 的 pass/input contract record | 单道工序 |
| `RenderWorkCompiler` | 把 `FramePass` 编译成 typed input / desc / diagnostics | 工单编译器 |
| `RenderInputDesc` | prepare/validate 后的 pipeline-facing 描述 | 可提交工单 |
| `RenderTargetDesc` | 描述 pass 输出 attachment 的形状 | 工位托盘规格 |
| `FrameGraphResourceRef` | 给 pass 之间传递的 attachment 起稳定名字 | 半成品标签 |
| `Pass_Shadow` | 从 directional light 视角写 depth-only target | 深度底片工序 |
| `DirectionalLightData::cascadeViewProj` | 保存四级 cascade 的 light view-projection | 分段底片参数 |

## 阅读顺序

1. [RenderPathGraph：渲染路线说明书](render-path-graph.md)：先理解 YAML 里每个 pass 字段如何定义 shader、source/target 和 pipeline 合同。
2. [FrameGraph：一帧的 Pass 排程表](framegraph.md)：再理解 graph pass 如何变成运行期 `FramePass`，以及 resource read/write 如何约束顺序。
3. [RenderWorkCompiler：FramePass 之后的唯一工单编译器](render-work-compiler.md)：理解当前 hard cut 后的 `RenderInput` / `RenderInputDesc` 单轨模型。
4. [Realtime 与 Offline：共享同一条 compiler 主线](realtime-offline-shared-flow.md)：再看两条渲染入口怎样共享 FrameGraph、PipelineCache 和 command buffer 流程。
5. [Render Target：Pass 的输出形状](render-target.md)：再看 target description、camera target matching 和 Vulkan attachment 之间的分层。
6. [Shadow Pass：只写深度的光源视角](shadow-pass.md)：理解 shadow depth-only pass 如何成为第一条真实多 pass 链路。
7. [CSM：把方向光阴影分成四段](cascaded-shadow-maps.md)：理解四级 cascade 数据如何从 light 进入 shadow pass 和 forward shader。

## 专题阅读

| 专题 | 什么时候读 |
|---|---|
| [Plane 与薄盒阴影](plane-vs-thin-box-shadow.md) | 调试 shadow acne、peter-panning、plane 是否应该 cast shadow 时读 |
| [3DGS PLY 渲染](3dgs-ply-rendering.md) | 需要理解 3D Gaussian Splatting 目标设计、PLY 字段和 `REQ-077-*` 后续路线时读；它不是当前已落地的 mesh 渲染路径 |

## 当前范围

| 已经实现 | 不是当前事实 |
|---|---|
| `FrameGraph::compile()` 已能做 resource DAG validation 和 ordered pass 输出 | 完整 async multi-queue scheduler |
| `FrameGraphRead` / `FrameGraphWrite` 资源声明 | attachment aliasing |
| HDR scene color、post process、bloom 和 swapchain 输出链路 | hardware ray tracing pipeline |
| 4 cascade directional shadow | point / spot shadow atlas |
| target-aware pipeline identity | render graph 自动 barrier / semaphore 推导 |
| `FramePass` / `RenderWorkCompiler` / `RenderInputDesc` 单轨模型 | 自动把 offline pass 拆成完整 path tracing pass graph |
| realtime metadata 使用 `renderInputStats` | batch 命名的正向运行时统计 |
| plane primitive 作为薄盒参与 shadow | 3DGS Vulkan splat pass 已经落地 |

## 继续阅读

- [架构总览](../architecture.md)
- [RenderPathGraph：渲染路线说明书](render-path-graph.md)
- [RenderWorkCompiler：FramePass 之后的唯一工单编译器](render-work-compiler.md)
- [Realtime 与 Offline：共享同一条 compiler 主线](realtime-offline-shared-flow.md)
- [Plane 与薄盒阴影](plane-vs-thin-box-shadow.md)
- [3DGS PLY 渲染](3dgs-ply-rendering.md)
- [Shadow 阶段教程](../../tutorial/shadow-era/index.md)
- [多 Pass 材质怎样变成 RenderWork](../../concepts/material/pass-rendering-flow.md)
- [Vulkan Backend](../../subsystems/vulkan-backend.md)
