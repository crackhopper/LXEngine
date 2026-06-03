# Render Target：Pass 的输出形状

`RenderTargetDesc` 可以想成工位上的托盘规格：它说明这道 pass 要写 color、depth、swapchain 还是 offscreen attachment，但它不是 Vulkan image，也不拥有 framebuffer。

当前代码刻意把“描述输出形状”和“持有 GPU 对象”分开。这样 core 层可以用稳定、可比较的 target description 参与 pipeline identity，backend 层再把 description 翻译成实际 Vulkan 资源。

## desc 描述形状，backend 持有资源

| 对象 | 当前层级 | 保存什么 | 不保存什么 |
|---|---|---|---|
| `RenderTargetDesc` | `src/core/frame_graph/` | role、color/depth format、sample count、layer count | Vulkan image / image view / framebuffer |
| `RenderTarget` | `src/core/frame_graph/` | 与 camera target matching 兼容的 target 值 | pass 之间的 resource 依赖 |
| `VulkanFrameGraphAttachment` | `src/backend/vulkan/` | 当前 frame 的 image、view、layout | scene 语义 |
| `VulkanRenderPass` / framebuffer | `src/backend/vulkan/` | 与 target desc 匹配的 render pass / framebuffer | material pass 语义 |

例如 shadow pass 使用 depth-only offscreen target：

```cpp
const auto shadowTarget =
    LX_core::RenderTargetDesc::offscreenDepth(swapchainTarget.depthFormat);
```

这句话只说明“这道工序需要一个离屏 depth 托盘”。托盘什么时候创建、当前 frame 使用哪一个 image、写完后如何 transition 到 shader-read layout，是 Vulkan backend 的职责。

## target 也是 pipeline identity 的一部分

同一个 shader 和同一个 mesh，写入 swapchain color/depth 与写入 offscreen depth-only target 时，Vulkan pipeline 的 attachment 形状不同。LXEngine 因此把 `RenderTargetDesc` 放进 pipeline identity。

| Pipeline 维度 | 为什么影响 pipeline |
|---|---|
| pass name | Forward 和 Shadow 使用不同 shader / render state |
| shader | vertex / fragment 输入输出不同 |
| vertex layout | mesh 顶点格式影响 pipeline input |
| render state | cull、depth test、blend 等影响 pipeline |
| `RenderTargetDesc` | attachment format、depth/color 组合影响 render pass compatibility |

这也是 shadow pass 能和 forward pass 并存的关键。Depth-only shadow target 不会误用 forward swapchain pipeline，forward target 也不会误用 shadow depth-only pipeline。

## camera target matching 仍走 core 类型

`Scene::getSceneLevelResources(pass, target)` 和 `RenderWorkQueue::build(context, pass, target)` 会把 target 传给 scene/camera/light 过滤逻辑。当前 target 的职责不是持有 GPU 资源，而是让 scene-level resources 能按输出形状匹配。

| 调用点 | target 的作用 |
|---|---|
| `Camera::matchesTarget` | 判断 camera 是否服务于当前 pass target |
| `Scene::getSceneLevelResources` | 按 pass 和 target 收集 camera/light UBO |
| `RenderWorkQueue::build` | 生成带 target identity 的 `RenderWorkItem` |
| `PipelineBuildDesc::target` | 让 pipeline cache 区分 attachment 形状 |

这个分层让 FrameGraph 可以描述“要写什么”，Scene 可以回答“哪些资源适合这道 pass”，Backend 再负责“怎样在 GPU 上实现”。

## 继续阅读

- [FrameGraph：一帧的 Pass 排程表](framegraph.md)
- [RenderWorkQueue：把 Scene 收敛成 Work 列表](render-queue.md)
- [RenderTarget 源码分析](../../source_analysis/src/core/frame_graph/render_target.md)
- [Pipeline identity 源码分析](../../source_analysis/src/core/pipeline/pipeline_identity.md)
