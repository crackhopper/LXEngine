# REQ-069-a: Vulkan Realtime Renderer 单文件拆分

> 2026-06-14 归档：本文件已并入 `REQ-076-j: Large File Decomposition Backlog`，不再作为单独 active 需求实施。以下内容保留为历史拆分草稿。

> 2026-06-02 调整：`REQ-069` 系列按“一份大文件一个需求”推进。本需求只拆分 `src/backend/vulkan/vulkan_realtime_renderer.cpp`，不夹带其它大文件治理。

## 背景

`src/backend/vulkan/vulkan_realtime_renderer.cpp` 当前约 1987 行。仓库已经有一部分模块化基础：

| 已有模块 | 当前职责 |
|---|---|
| `VulkanRendererFoundation` | 持有 realtime/headless 可共享的 device、command buffer manager、resource manager |
| `VulkanResourceManager` | 管理 GPU resource cache、FrameGraph attachment、render pass、pipeline cache |
| `VulkanPostProcessBuilder` | 创建 skybox、bloom、post process 相关 fullscreen material |
| `IblBakeRenderer` | 执行 equirectangular HDR 到 IBL 资源的 GPU bake |
| `core/frame_graph/FrameGraph` | 声明 pass 顺序、read/write resource、per-pass queue build |

但 `vulkan_realtime_renderer.cpp` 仍把实时 renderer 总控、FrameGraph Vulkan 执行、offscreen attachment layout transition、fullscreen item 注入、shadow cascade runtime、debug dump/readback、BMP 写出辅助都放在一个文件里。

这不是简单的“已有模块重复实现”，而是缺少更细的执行层模块。拆分时必须先判断某段代码是否应该复用已有模块，还是应该成为已有模块之上的协调层。

## 目标

1. 把 `vulkan_realtime_renderer.cpp` 降到 1000 行以下，保留 realtime renderer 总控语义。
2. 把 FrameGraph Vulkan 执行、render target dump、fullscreen item 构建、shadow runtime 拆成语义明确的小文件。
3. 拆分前逐块检查已有模块能力，优先复用而不是写第二套相似代码。
4. 保持 realtime renderer public API、渲染行为、debug dump 行为不变。
5. 形成 Vulkan realtime 端的整体架构图和文件职责表，让我们能从文件名判断职责边界。

## 需求

### R1: 先做职责审计和复用判断

实现前 SHALL 对 `vulkan_realtime_renderer.cpp` 中的职责块逐项审计：

| 职责块 | 必须对照的已有模块 | 决策要求 |
|---|---|---|
| device/resource/command 访问 | `VulkanRendererFoundation` | 不新增第二套 foundation/context |
| pipeline/resource/attachment cache | `VulkanResourceManager` | 不复制 resource cache 或 pipeline cache |
| post/bloom/skybox material | `VulkanPostProcessBuilder` | 复用 material 创建，只抽 item 构建 |
| IBL bake | `IblBakeRenderer` | realtime renderer 只保留触发条件和 scene 写回 |
| FrameGraph 声明/编译 | `core/frame_graph/FrameGraph` | backend executor 不替代 core FrameGraph |
| render target readback | 现有 dump API | 抽独立模块，保持 API 行为 |

审计结果 SHALL 写入实现说明或 source analysis，明确每个新文件为什么存在、复用了哪些已有模块、没有复用的原因是什么。

### R2: 拆出 VulkanRealtimeFrameGraphExecutor

新增 `src/backend/vulkan/vulkan_realtime_frame_graph_executor.hpp/.cpp`。

职责：

- 根据 `CompiledFrameGraphPass` 创建/复用 offscreen FrameGraph attachment。
- 执行 attachment layout transition。
- 创建/复用 offscreen framebuffer。
- 绘制 pass queue：绑定 pipeline、绑定 descriptor、发出 draw。
- 把 offscreen pass writes 过渡到 shader-read layout。

不得负责：

- scene build。
- pass 顺序决策。
- swapchain acquire/present。
- GUI frame。
- post/bloom/skybox item 创建。

### R3: 拆出 VulkanRenderTargetDumper

新增 `src/backend/vulkan/vulkan_render_target_dumper.hpp/.cpp`。

职责：

- `dumpFrameGraphAttachment()` 的 readback 和 BMP 写出。
- `dumpDebugRenderTarget()` 的 one-off offscreen render + readback。
- BMP 24-bit header/write helper。
- HDR/half-float/depth dump 到 debug byte 的转换 helper。
- pass debug name 到 `StringID` 的解析。

要求：

- dump 模块依赖 `VulkanRendererFoundation`、`VulkanResourceManager`、`VulkanCommandBufferManager` 和必要的 scene/frame graph 输入。
- dump 模块不拥有 swapchain 生命周期。
- 错误信息保持可诊断。

### R4: 拆出 VulkanFullscreenItemBuilder

新增 `src/backend/vulkan/vulkan_fullscreen_item_builder.hpp/.cpp`。

职责：

- 复用 `VulkanPostProcessBuilder` 创建 material。
- 把 fullscreen material 变成 `RenderingItem`。
- 构建并插入 bloom threshold、bloom blur、post process、skybox background item。
- 统一 fullscreen triangle 的 vertex/index buffer 和 object signature 约定。

要求：

- bloom disabled 时不创建 bloom pass item。
- skybox item 仍按当前逻辑消费 baked `SkyboxMap`，否则 fallback 到 `skyboxCubemap`。
- skybox item 仍注入 `EnvironmentUBO` 和 scene-level camera resource。

### R5: 拆出 VulkanShadowRuntime

新增 `src/backend/vulkan/vulkan_shadow_runtime.hpp/.cpp`。

职责：

- 找到当前主 directional light。
- 根据 active camera 更新 shadow cascades。
- 创建和刷新每个 cascade 的 UBO snapshot。
- 在 Shadow pass queue 中用 cascade snapshot 替换主 light UBO。
- 在每个 shadow pass 前同步对应 cascade snapshot。

要求：

- 没有 directional light 时行为不变。
- cascade 数量仍由 `LX_core::MaxShadowCascades` 控制。
- Forward pass 仍采样 `ShadowMap0..3`。

### R6: Realtime renderer 只保留总控职责

拆分后 `VulkanRealtimeRenderer::Impl` SHOULD 只保留：

- realtime 初始化和 shutdown。
- swapchain rebuild。
- scene init 总流程。
- FrameGraph pass 列表构建和 compile 调用。
- per-frame acquire / command buffer / submit / present。
- GUI frame 时机。
- 对新 helper 的组合调用。

如果仍保留其它细节，必须能说明它为什么属于 realtime 总控，而不是 executor/dumper/builder/shadow runtime。

### R7: 产出 Vulkan realtime 架构图和文件职责表

拆分完成后 SHALL 更新或新增 notes/source_analysis 页面，包含：

- Vulkan realtime 模块架构图。
- 每个新文件的单一职责说明。
- 每个文件复用哪些已有模块。
- 明确哪些职责不属于该文件。

建议图示内容：

```text
VulkanRealtimeRenderer
  -> VulkanRendererFoundation
  -> VulkanRealtimeFrameGraphExecutor
  -> VulkanFullscreenItemBuilder -> VulkanPostProcessBuilder
  -> VulkanShadowRuntime -> DirectionalLight / Scene
  -> VulkanRenderTargetDumper
  -> VulkanSwapchain / Gui
```

## 测试

### T1: Build

- `ninja lxe_editor`

### T2: Auto tests

- `ctest --output-on-failure -L auto -LE requires_video_device`

### T3: Vulkan-capable smoke

如环境可用：

- `xvfb-run -a ctest --output-on-failure -L requires_video_device`

### T4: Behavior-specific regression

至少验证：

- compiled FrameGraph pass names 和 pass count 不变。
- `scene.hdrColor` dump 仍可读取。
- shadow cascade pass 使用不同 cascade UBO snapshot。
- skybox 仍消费 IBL scene resources。
- bloom disabled/enabled 的 pass item 行为不变。

## 修改范围

- `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- `src/backend/vulkan/vulkan_realtime_renderer.hpp`
- `src/backend/vulkan/vulkan_realtime_frame_graph_executor.*`
- `src/backend/vulkan/vulkan_render_target_dumper.*`
- `src/backend/vulkan/vulkan_fullscreen_item_builder.*`
- `src/backend/vulkan/vulkan_shadow_runtime.*`
- `src/backend/vulkan/CMakeLists.txt` 或相关 build 文件
- 相关 tests
- 相关 notes/source_analysis

## 边界与约束

- 本 REQ 不拆其它超过 1000 行的 `.cpp`。
- 本 REQ 不新增 renderer 功能。
- 本 REQ 不重写 `VulkanResourceManager` 或 `FrameGraph`。
- 本 REQ 不改变 public `VulkanRealtimeRenderer` / `VulkanRenderer` facade 行为。
- 本 REQ 不把 offline renderer 逻辑引入 realtime renderer。

## 依赖

- `REQ-054-a: Vulkan Renderer 边界硬切与实时拆分`
- `openspec/specs/cpp-style-guide/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`
- `openspec/specs/test-build-execution/spec.md`

## 下游工作

- `REQ-069-b` 继续拆分 `src/core/editor/commands/builtin_commands.cpp`。
- `REQ-069-c` 继续拆分 `src/demos/lxe_editor/scene_runtime.cpp`。

## 实施状态

2026-06-14 复核：保留 active，基本未完成。

当前已有 `VulkanPostProcessBuilder`、`IblBakeRenderer` 等局部拆分，但 `src/backend/vulkan/vulkan_realtime_renderer.cpp` 仍是 3000+ 行级主文件，实时 renderer 的 frame graph 执行、render target dump、fullscreen/post/skybox/shadow 等大块职责仍未完成文档所述拆分。
