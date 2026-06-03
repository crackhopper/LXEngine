# 渲染管线：从场景到多 Pass 提交的路线图

渲染管线负责把实时 `Scene` 或离线 `OfflineRenderJob` 组织成一组有顺序的 `FramePass`，再把每个 pass 内的任务收敛成 `RenderWorkItem`，交给 Vulkan backend 录制和提交。

我们可以把它想成一条工厂生产线：`FrameGraph` 是排程表，`RenderTargetDesc` 是每个工位的托盘规格，Shadow pass 先生产深度底片，CSM 把底片按距离分成四段，`RenderWorkQueue` 则是每个工位里真正要执行的任务箱。Realtime 和 offline 的入口不同，但它们使用同一张工单格式。

| 对象 | 当前角色 | 生产线类比 |
|---|---|---|
| `FrameGraph` | 保存一帧的 pass 顺序、read/write 声明和 compile 校验 | 工序排程表 |
| `RenderTargetDesc` | 描述 pass 输出 attachment 的形状 | 工位托盘规格 |
| `FrameGraphResourceRef` | 给 pass 之间传递的 attachment 起稳定名字 | 半成品标签 |
| `Pass_Shadow` | 从 directional light 视角写 depth-only target | 深度底片工序 |
| `DirectionalLightData::cascadeViewProj` | 保存四级 cascade 的 light view-projection | 分段底片参数 |
| `RenderWorkQueue` | 把 realtime scene 或 offline job 过滤成某个 pass 内的 `RenderWorkItem` | 工位任务箱 |
| `RenderWorkItem` | 一次 pipeline work 的最小稳定记录 | 可执行工单 |

## 阅读顺序

1. [FrameGraph：一帧的 Pass 排程表](framegraph.md)：先理解 pass 为什么按显式顺序执行，以及 resource read/write 如何约束顺序。
2. [Realtime 与 Offline：同一条 RenderWork 流水线](realtime-offline-shared-flow.md)：再看两条渲染入口怎样共享 FrameGraph、RenderWorkQueue、PipelineCache 和 command buffer 流程。
3. [Render Target：Pass 的输出形状](render-target.md)：再看 target description、camera target matching 和 Vulkan attachment 之间的分层。
4. [Shadow Pass：只写深度的光源视角](shadow-pass.md)：理解 shadow depth-only pass 如何成为第一条真实多 pass 链路。
5. [CSM：把方向光阴影分成四段](cascaded-shadow-maps.md)：理解四级 cascade 数据如何从 light 进入 shadow pass 和 forward shader。
6. [RenderWorkQueue：把 Scene 收敛成 Work 列表](render-queue.md)：最后回到每个 pass 内，理解 renderable、material pass、pipeline key 怎样变成可提交的 item。

## 当前范围

| 已经实现 | 不是当前事实 |
|---|---|
| 显式 pass 顺序执行 | 自动拓扑排序 |
| `FrameGraphRead` / `FrameGraphWrite` 资源声明 | attachment aliasing |
| HDR scene color、post process、bloom 和 swapchain 输出链路 | hardware ray tracing pipeline |
| 4 cascade directional shadow | point / spot shadow atlas |
| target-aware pipeline identity | render graph 自动 barrier / semaphore 推导 |
| realtime/offline 共享 `RenderWorkItem`、pipeline cache 和 command buffer 执行入口 | 自动把 offline pass 拆成完整 path tracing pass graph |

## 继续阅读

- [架构总览](../architecture.md)
- [Realtime 与 Offline：同一条 RenderWork 流水线](realtime-offline-shared-flow.md)
- [Shadow 阶段教程](../../tutorial/shadow-era/index.md)
- [多 Pass 材质怎样变成 RenderWork](../../concepts/material/pass-rendering-flow.md)
- [Vulkan Backend](../../subsystems/vulkan-backend.md)
