# 渲染管线：从场景到多 Pass 提交的路线图

渲染管线负责把 `Scene` 里的 camera、light、renderable 和 material pass，组织成一组有顺序的 `FramePass`，再交给 Vulkan backend 录制和提交。

我们可以把它想成一条工厂生产线：`FrameGraph` 是排程表，`RenderTargetDesc` 是每个工位的托盘规格，Shadow pass 先生产深度底片，CSM 把底片按距离分成四段，`RenderQueue` 则是每个工位里真正要执行的任务箱。

| 对象 | 当前角色 | 生产线类比 |
|---|---|---|
| `FrameGraph` | 保存一帧的 pass 顺序、read/write 声明和 compile 校验 | 工序排程表 |
| `RenderTargetDesc` | 描述 pass 输出 attachment 的形状 | 工位托盘规格 |
| `FrameGraphResourceRef` | 给 pass 之间传递的 attachment 起稳定名字 | 半成品标签 |
| `Pass_Shadow` | 从 directional light 视角写 depth-only target | 深度底片工序 |
| `DirectionalLightData::cascadeViewProj` | 保存四级 cascade 的 light view-projection | 分段底片参数 |
| `RenderQueue` | 把 scene 过滤成某个 pass 内的 `RenderingItem` | 工位任务箱 |

## 阅读顺序

1. [FrameGraph：一帧的 Pass 排程表](framegraph.md)：先理解 pass 为什么按显式顺序执行，以及 resource read/write 如何约束顺序。
2. [Render Target：Pass 的输出形状](render-target.md)：再看 target description、camera target matching 和 Vulkan attachment 之间的分层。
3. [Shadow Pass：只写深度的光源视角](shadow-pass.md)：理解 shadow depth-only pass 如何成为第一条真实多 pass 链路。
4. [CSM：把方向光阴影分成四段](cascaded-shadow-maps.md)：理解四级 cascade 数据如何从 light 进入 shadow pass 和 forward shader。
5. [RenderQueue：把 Scene 收敛成 Draw 列表](render-queue.md)：最后回到每个 pass 内，理解 renderable、material pass、pipeline key 怎样变成可提交的 item。

## 当前范围

| 已经实现 | 不是当前事实 |
|---|---|
| 显式 pass 顺序执行 | 自动拓扑排序 |
| `FrameGraphRead` / `FrameGraphWrite` 资源声明 | attachment aliasing |
| offscreen depth attachment 写入后作为 sampled image 读取 | HDR/Post 的 scene color 链路 |
| 4 cascade directional shadow | point / spot shadow atlas |
| target-aware pipeline identity | task-based command recording |

## 继续阅读

- [架构总览](../architecture.md)
- [Shadow 阶段教程](../../tutorial/shadow-era/index.md)
- [多 Pass 如何变成 Draw](../../concepts/material/pass-rendering-flow.md)
- [Vulkan Backend](../../subsystems/vulkan-backend.md)
