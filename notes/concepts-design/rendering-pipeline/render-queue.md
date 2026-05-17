# RenderQueue：把 Scene 收敛成 Draw 列表

`RenderQueue` 可以想成每个工位上的任务箱。FrameGraph 决定这一帧有哪些工位，RenderQueue 则决定某个工位里具体要加工哪些 renderable，以及它们用什么 shader、resource 和 pipeline。

FrameGraph 不直接检查 mesh 和 material 是否匹配。这个工作在 scene 和 material pass validation 中完成。到 `RenderQueue::buildFromScene(scene, pass, target)` 时，它消费的是已经具备 pass 语义的 scene 数据。

## buildFromScene 只处理单个 pass

`RenderQueue::buildFromScene` 的输入是一个 pass name 和一个 target description。它不会关心其他 pass 的 read/write，也不会管理 attachment 生命周期。

| 步骤 | 当前行为 |
|---|---|
| 取 scene-level resources | camera 按 target 匹配，light 按 pass 匹配 |
| 筛 renderable | `supportsPass(pass)`、visibility mask、validated pass data |
| 生成 item | 把 shader、descriptor resources、target、pipeline key 放进 `RenderingItem` |
| 排序 | 按 `PipelineKey` 稳定聚合，减少 pipeline 切换 |
| 预构建 | `collectUniquePipelineBuildDescs()` 产出 pipeline build desc |

这个分工让 FrameGraph 只处理“跨 pass 顺序与资源合同”，RenderQueue 只处理“单个 pass 里有哪些 draw”。

## scene-level resources 跟 pass 和 target 一起匹配

一个 pass 的 draw item 不只需要 mesh 和 material，还需要 camera/light 等 scene-level resource。RenderQueue 会从 scene 取这些 resource，并和 material instance 自己的 resource 合并。

| Resource 来源 | 例子 | 进入 item 的方式 |
|---|---|---|
| Scene camera | view/projection UBO | `Scene::getSceneLevelResources(pass, target)` |
| Scene light | directional light UBO / shadow params | `Scene::getSceneLevelResources(pass, target)` |
| Material instance | texture、parameter buffer | material pass validation 输出 |
| FrameGraph sampled resource | `ShadowMap0..3` | backend attach 后进入 descriptor |

在 shadow pass 中，camera/light resource 会让 shader 拿到 light-space transform；在 forward pass 中，scene light resource 和 sampled shadow resource 一起参与光照。

## pipeline key 把相同 draw 条件聚合起来

RenderQueue 的排序单位不是 mesh 名字，而是 `PipelineKey`。同样的 shader、vertex layout、render state 和 target desc 会被聚在一起，方便 backend 减少 pipeline 切换。

| 进入 `PipelineKey` 的信息 | 为什么重要 |
|---|---|
| pass name | 区分 Forward、Shadow 等 pass |
| shader identity | 决定 shader module 和 binding layout |
| vertex layout | 决定 vertex input |
| render state | 决定 cull、depth、blend 等状态 |
| target signature | 区分 swapchain color/depth 与 offscreen depth-only |

这也是 RenderQueue 和 RenderTarget 的连接点：target 不只是输出位置，也参与 pipeline identity。

## FrameGraph 和 RenderQueue 的边界

| 责任 | `FrameGraph` | `RenderQueue` |
|---|---|---|
| 多 pass 顺序 | 负责 | 不负责 |
| resource read/write 合同 | 负责 | 不负责 |
| 单 pass renderable 过滤 | 不负责 | 负责 |
| item 排序和 pipeline desc 收集 | 汇总所有 pass | 负责单个 pass |
| Vulkan attachment 生命周期 | 不负责 | 不负责 |

这个边界能帮助我们阅读代码：看到跨 pass 的资源名和顺序时找 FrameGraph；看到“为什么这个 mesh 在 Shadow pass 里画或不画”时找 RenderQueue、Scene 和 Material。

## 继续阅读

- [FrameGraph：一帧的 Pass 排程表](framegraph.md)
- [Render Target：Pass 的输出形状](render-target.md)
- [RenderQueue 源码分析](../../source_analysis/src/core/frame_graph/render_queue.md)
- [多 Pass 如何变成 Draw](../../concepts/material/pass-rendering-flow.md)
