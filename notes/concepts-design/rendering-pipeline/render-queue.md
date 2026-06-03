# RenderWorkQueue：把 Scene 收敛成 Work 列表

`RenderWorkQueue` 可以想成每个工位上的任务箱。FrameGraph 决定这一帧有哪些工位，RenderWorkQueue 则决定某个工位里具体要执行哪些 pipeline work，以及它们用什么 shader、resource 和 pipeline。

FrameGraph 不直接检查 mesh 和 material 是否匹配，也不直接理解 offline SSBO schema。这个工作在 scene/material pass validation 或 offline job 组装阶段完成。到 `RenderWorkQueue::build(context, pass, target)` 时，它消费的是已经具备 pass 语义的输入数据。

## build 只处理单个 pass

`RenderWorkQueue::build` 的输入是一个 `RenderWorkBuildContext`、一个 pass name 和一个 target description。它不会关心其他 pass 的 read/write，也不会管理 attachment 生命周期。

| 步骤 | Realtime 当前行为 | Offline 当前行为 |
|---|---|
| 取输入资源 | camera 按 target 匹配，light 按 pass 匹配 | 从 `OfflineRenderJob` 构建 `SceneGpu*` SSBO 和 output buffer |
| 筛任务 | `supportsPass(pass)`、visibility mask、validated pass data | 当前只响应 `Pass_OfflineRayTrace` |
| 生成 item | 把 shader、descriptor resources、target、pipeline key 放进 `RenderWorkItem` | 生成一个 `ComputeDispatch` 类型的 `RenderWorkItem` |
| 排序 | 按 `PipelineKey` 稳定聚合，减少 pipeline 切换 | 当前单 item，不需要排序 |
| 预构建 | `collectUniquePipelineBuildDescs()` 产出 pipeline build desc | 同一个接口产出 compute pipeline build desc |

这个分工让 FrameGraph 只处理“跨 pass 顺序与资源合同”，RenderWorkQueue 只处理“单个 pass 里有哪些 work”。

## scene-level resources 跟 pass 和 target 一起匹配

一个 realtime raster work item 不只需要 mesh 和 material，还需要 camera/light 等 scene-level resource。RenderWorkQueue 会从 scene 取这些 resource，并和 material instance 自己的 resource 合并。

| Resource 来源 | 例子 | 进入 item 的方式 |
|---|---|---|
| Scene camera | view/projection UBO | `Scene::getSceneLevelResources(pass, target)` |
| Scene light | directional light UBO / shadow params | `Scene::getSceneLevelResources(pass, target)` |
| Material instance | texture、parameter buffer | material pass validation 输出 |
| FrameGraph sampled resource | `ShadowMap0..3` | backend attach 后进入 descriptor |

在 shadow pass 中，camera/light resource 会让 shader 拿到 light-space transform；在 forward pass 中，scene light resource 和 sampled shadow resource 一起参与光照。

## pipeline key 把相同 work 条件聚合起来

RenderWorkQueue 的排序单位不是 mesh 名字，而是 `PipelineKey`。同样的 shader、vertex layout、render state 和 target desc 会被聚在一起，方便 backend 减少 pipeline 切换。Offline compute item 也有自己的 `PipelineKey`，用于命中同一套 compute pipeline cache。

| 进入 `PipelineKey` 的信息 | 为什么重要 |
|---|---|
| pass name | 区分 Forward、Shadow 等 pass |
| shader identity | 决定 shader module 和 binding layout |
| vertex layout | 决定 vertex input |
| render state | 决定 cull、depth、blend 等状态 |
| target signature | 区分 swapchain color/depth 与 offscreen depth-only |

这也是 RenderWorkQueue 和 RenderTarget 的连接点：target 不只是输出位置，也参与 pipeline identity。

## FrameGraph 和 RenderWorkQueue 的边界

| 责任 | `FrameGraph` | `RenderWorkQueue` |
|---|---|---|
| 多 pass 顺序 | 负责 | 不负责 |
| resource read/write 合同 | 负责 | 不负责 |
| 单 pass realtime renderable 过滤 | 不负责 | 负责 |
| 单 pass offline compute item 生成 | 不负责 | 负责 |
| item 排序和 pipeline desc 收集 | 汇总所有 pass | 负责单个 pass |
| Vulkan attachment 生命周期 | 不负责 | 不负责 |

这个边界能帮助我们阅读代码：看到跨 pass 的资源名和顺序时找 FrameGraph；看到“为什么这个 mesh 在 Shadow pass 里画或不画”时找 RenderWorkQueue、Scene 和 Material；看到 offline 如何把整个任务变成 compute dispatch 时找 `RenderWorkBuildContext::offline(...)` 和 `makeOfflineComputeItem(...)`。

## 继续阅读

- [FrameGraph：一帧的 Pass 排程表](framegraph.md)
- [Realtime 与 Offline：同一条 RenderWork 流水线](realtime-offline-shared-flow.md)
- [Render Target：Pass 的输出形状](render-target.md)
- [RenderWorkQueue 源码分析](../../source_analysis/src/core/frame_graph/render_queue.md)
- [多 Pass 材质怎样变成 RenderWork](../../concepts/material/pass-rendering-flow.md)
