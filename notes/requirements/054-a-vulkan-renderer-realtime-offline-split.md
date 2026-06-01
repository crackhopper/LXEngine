# REQ-054-a: Vulkan Renderer Realtime / Offline 拆分

> 2026-06-01 新增：本 REQ 在实现 Vulkan compute 离线渲染器前，先重构当前 `VulkanRenderer` 的职责边界。当前仍在讨论中，未开始。

## 背景

当前 `src/backend/vulkan/vulkan_renderer.cpp` 已超过 2200 行，`VulkanRendererImpl`
同时承担 device/resource 初始化、swapchain、FrameGraph、scene resource sync、
IBL bake、post process、debug dump、shadow cascade、GUI overlay 和 present loop。

Offline Rendering Lab 需要 headless Vulkan compute renderer。如果直接在当前
`VulkanRendererImpl` 里继续加 offline 分支，会让大类继续膨胀，并把离线 renderer
绑到 swapchain/frame loop/editor 语义上。

因此在 `REQ-054-b` 的 compute MVP 前，需要先把 Vulkan renderer 重新组织成：

- 可共享的 renderer foundation。
- 实时 renderer。
- 离线 renderer。
- 若干更小的功能模块。

架构强约束：

- `VulkanRendererFoundation` 或等价 base/foundation 持有共享 Vulkan device/resource/command/shader/pipeline helper。
- `VulkanRealtimeRenderer` 负责 swapchain、FrameGraph、skybox、post、shadow、GUI/editor realtime。
- `VulkanOfflineRenderer` 负责 headless compute、GpuScene、BVH、readback。
- public `VulkanRenderer` facade 可以暂时保留给 editor，但内部只委托 realtime renderer。
- offline renderer 不能继承或依赖 realtime renderer，不能依赖 swapchain、window、GUI 或 editor path。
- 不允许通过在 realtime renderer 中增加 `offline mode` 来实现离线 renderer。

## 目标

1. 提取 Vulkan renderer 的公共基础能力。
2. 把实时 swapchain/frame loop 与离线 headless compute 分开。
3. 拆分当前 2200+ 行 `vulkan_renderer.cpp` 的主要功能块。
4. 保持当前 editor/realtime public API 尽量稳定。
5. 为 `REQ-054-b` 提供干净的 offline renderer 落点。

## 需求

### R1: 分步重构策略

本 REQ 必须分步执行，避免把大类拆分、public facade 调整和 offline renderer
接入混成一次不可控的大改。

推荐实施阶段：

| 阶段 | 交付物 | 可并行性 |
|---|---|---|
| S1 | `VulkanRendererFoundation`，抽出 device/resource/command/shared helper | 前置，先做 |
| S2 | `VulkanFrameGraphExecutor`，抽出 compiled pass 执行与 attachment transition | 依赖 S1，可独立 agent |
| S3 | `VulkanRenderTargetDump`，抽出 attachment/debug render target dump | 依赖 S1，可独立 agent |
| S4 | `VulkanPostProcessBuilder` / skybox/bloom/fullscreen item builder | 依赖 S1，可独立 agent |
| S5 | `VulkanShadowRuntime`，抽出 shadow cascade UBO snapshot/binding | 依赖 S1，可独立 agent |
| S6 | `VulkanRealtimeRenderer`，保留 `VulkanRenderer` public facade | 依赖 S1-S5 的稳定接口 |
| S7 | `VulkanOfflineRenderer` 空壳与 headless context 验证 | 依赖 S1，可与 S2-S5 部分并行 |

每个阶段都必须保持 `lxe_editor` 可构建，并且不改变当前实时渲染行为。

### R2: Renderer foundation

新增或提取一个 Vulkan renderer foundation 模块。

建议职责：

| 职责 | 说明 |
|---|---|
| device | `VulkanDevice` 初始化与生命周期 |
| command | `VulkanCommandBufferManager` |
| resource | `VulkanResourceManager` |
| shared helpers | shader loading、readback、layout transition 辅助 |
| IBL bake access | 可被 realtime/offline 复用的 IBL bake 入口 |

要求：

- foundation 不依赖 swapchain。
- foundation 不依赖 GUI。
- foundation 不依赖 realtime FrameGraph 主循环。
- foundation 使用 RAII 和 constructor injection。

S1 验收：

- 能初始化 `VulkanDevice`、`VulkanCommandBufferManager`、`VulkanResourceManager`。
- 可在无 swapchain 的测试中创建 foundation。
- 现有 realtime renderer 改为通过 foundation 访问这些资源。

### R3: FrameGraph executor 拆分

抽出 compiled FrameGraph 执行逻辑。

职责：

- pass write attachment 创建。
- attachment layout transition。
- offscreen framebuffer 获取/创建。
- draw item pipeline binding / descriptor binding / draw call。
- pass writes -> shader read transition。

边界：

- 不负责 scene build。
- 不负责 swapchain acquire/present。
- 不负责 GUI frame。
- swapchain render pass 的最后 present 语义仍由 realtime renderer 管理。

S2 验收：

- frame graph pass count 与重构前一致。
- Forward/Post/DebugOverlay 仍按原顺序执行。
- attachment dump 仍能读取 `scene.hdrColor`。

### R4: Render target dump 拆分

把 `dumpFrameGraphAttachment()` / `dumpDebugRenderTarget()` 相关逻辑移入独立模块。

职责：

- frame graph attachment readback。
- color/HDR tone mapped dump。
- depth dump。
- one-off debug render target render + readback。

边界：

- dump 模块依赖 foundation 和必要的 scene/frame graph 输入。
- 不持有 swapchain 生命周期。
- 不参与主 draw loop。

S3 验收：

- 现有 dump API 行为不变。
- dump 缺失 attachment 时错误信息保持可诊断。

### R5: Post process / skybox builder 拆分

抽出 fullscreen item 构建逻辑。

职责：

- standard post process item。
- bloom threshold / blur item。
- skybox background item。
- fullscreen shader binding helper。

边界：

- 只构建 material/rendering item，不执行 pass。
- 不直接访问 swapchain present。

S4 验收：

- HDR/Post 相关测试通过。
- skybox 背景仍能消费 baked `SkyboxMap`。

### R6: Shadow runtime 拆分

抽出 shadow cascade 运行时辅助。

职责：

- directional light cascade 更新。
- shadow cascade UBO snapshot。
- shadow map descriptor 绑定辅助。

边界：

- 不构建整个 FrameGraph。
- 不持有 swapchain。

S5 验收：

- shadow / CSM 测试保持通过。
- 没有 directional light 时行为不变。

### R7: Realtime renderer 子类/派生实现

实时 renderer 负责当前 editor 需要的行为：

- window/swapchain 初始化
- GUI overlay
- FrameGraph build/compile/execute
- present loop
- resize/rebuild swapchain
- debug overlay
- realtime post stack

外部 public facade 可以继续保留 `VulkanRenderer` 名称，避免一次性修改所有 editor 调用点。内部必须落到 `VulkanRealtimeRenderer` 或等价结构；facade 只委托 realtime renderer，不承载 offline 分支。

S6 验收：

- `VulkanRenderer::create()` 仍返回 editor 可用的 realtime renderer。
- `gpu::Renderer` 接口仍由 realtime renderer 实现。
- editor 不需要知道 offline renderer。

### R8: Offline renderer 子类/派生实现

离线 renderer 负责：

- headless Vulkan context
- compute pipeline
- storage buffer / storage image
- GPU readback
- 不创建 swapchain
- 不进入 realtime draw/present loop

`REQ-054-b` 的 `VulkanOfflineRenderer` 必须落在这一侧，而不是塞回 realtime renderer。

要求：

- offline renderer 可以复用 foundation/base，但不能继承 realtime renderer。
- offline renderer 不依赖 swapchain/window/GUI。
- offline renderer 不实现 `gpu::Renderer::draw()` / realtime frame loop 语义。
- offline renderer 的入口是 offline job API。

S7 验收：

- 可在无 window/swapchain 情况下创建 `VulkanOfflineRenderer` 或等价空壳。
- 可创建 compute-capable command/resource context。
- 不调用 `draw()` / `uploadData()` / present loop。

### R9: 大类功能拆分

当前 `vulkan_renderer.cpp` 中的功能需要拆成更合理的模块。建议拆分方向：

| 模块 | 当前职责来源 |
|---|---|
| `vulkan_renderer_foundation.*` | device/resource/command 初始化与共享 helper |
| `vulkan_realtime_renderer.*` | 原 `VulkanRendererImpl` 的 realtime 主体 |
| `vulkan_frame_graph_executor.*` | compiled FrameGraph pass 执行、attachment transition |
| `vulkan_scene_resource_binder.*` | scene resource sync、IBL resource 注入相关辅助 |
| `vulkan_render_target_dump.*` | frame graph attachment / debug render target dump |
| `vulkan_post_process_builder.*` | bloom/post/skybox fullscreen item 构建 |
| `vulkan_shadow_runtime.*` | shadow cascade UBO snapshot 与绑定 |
| `vulkan_offline_renderer.*` | `REQ-054-b` 的 offline compute renderer |

实际文件名可按代码现状调整，但必须避免继续把新增离线能力堆进单个 2000 行大文件。

### R10: Public API 兼容边界

要求：

- `gpu::Renderer` 的实时接口继续由 realtime renderer 实现。
- editor 现有调用点不需要理解 offline renderer。
- offline renderer 有独立 job API，不继承 `gpu::Renderer` 的 `draw()/uploadData()` 语义。
- dump/debug API 若需要共享，应下沉到可复用 helper，而不是只能通过 realtime renderer 使用。

### R11: 测试与验证

覆盖：

- 现有 `lxe_editor` 仍能启动并渲染当前测试场景。
- FrameGraph pass count / attachment dump 等现有测试保持通过。
- headless foundation 可在无 swapchain 情况下创建 device/resource/command manager。
- 重构后 IBL bake 测试仍通过。
- 新模块没有引入循环依赖。

建议每个阶段至少运行：

```bash
cmake --build build --target lxe_editor test_vulkan_frame_graph test_vulkan_ibl_bake -j2
xvfb-run -a ./build/src/test/test_vulkan_frame_graph
xvfb-run -a ./build/src/test/test_vulkan_ibl_bake
```

## 修改范围

- `src/backend/vulkan/vulkan_renderer.*`
- `src/backend/vulkan/details/`
- 新增 Vulkan renderer foundation / realtime / offline 相关模块
- CMake
- Vulkan integration tests

## 边界与约束

- 本 REQ 不实现 compute offline path tracer；那是 `REQ-054-b`。
- 本 REQ 不要求 public facade 一次性改名。
- 本 REQ 不改变 editor 功能表现。
- 本 REQ 不引入 Vulkan hardware ray tracing。
- 本 REQ 不做无关渲染效果重写。

## 依赖

- `REQ-052-a`
- 当前 Vulkan backend
- `openspec/specs/renderer-backend-vulkan/spec.md`
- `openspec/specs/cpp-style-guide/spec.md`

## 后续工作

- `REQ-054-b` 基于拆分后的 offline renderer 落点实现 Vulkan compute MVP。
- 后续 Vulkan RT backend 可作为另一个 offline acceleration backend 接入。

## 实施状态

讨论中，未开始。
