## Context

`src/infra/gui/{gui.hpp, imgui_gui.cpp}` 目前只有一个残缺的 ImGui 包装：`init()` 直接调 `ImGui_ImplVulkan_Init()` 但缺 `CreateContext`、缺 SDL backend、没有 descriptor pool、没有 render pass / swapchain image count；`endFrame()` 用 `VK_NULL_HANDLE` 调 `RenderDrawData`。`VulkanRenderer` 的 PImpl 尚未持有 `Gui`，`draw()` 里没有任何 ImGui 调用。`src/core/platform/window.hpp` 只有 `getInputState()`，没有把 native handle 暴露出来，`Gui` 即便接好了也拿不到 `SDL_Window*`。`Sdl3InputState::handleSdlEvent()` 在 `sdl_window.cpp` 的单次 poll 循环里消费事件，当前只负责输入和 close，ImGui 没有接进这条路径。

相机控制器（`REQ-015/016`）和调试面板（`REQ-018`）都依赖一个能工作的 UI overlay 与 UI capture 信号。`FrameGraph` 目前没有"非场景 overlay pass"形态，把 ImGui 做成独立 pass 需要扩接口；为了快速闭环，本 REQ 选择 overlay 路线：ImGui 在 swapchain render pass 内、所有场景 draw 之后、`endRenderPass` 之前录入同一个 command buffer。

## Goals / Non-Goals

**Goals:**
- `Gui::init()/shutdown()` 顺序完整、descriptor pool 归 `Gui` 所有
- `Gui::endFrame(cmd)` 接收真实 command buffer，空 drawData 时安全跳过
- `VulkanRenderer` 持有 `Gui`，每帧按"beginFrame → UI 回调 → 场景 → endFrame(cmd) → endRenderPass"顺序录制
- `Window::getNativeHandle()` 以 `void*` type-erase SDL/GLFW native 指针，不污染 `core` 层
- SDL 事件循环单次 poll 同时服务 close、`Sdl3InputState`、ImGui
- `IInputState` 增加默认 `isUiCapturingMouse/Keyboard`，后续 Sdl3 实现可从 `ImGui::GetIO().WantCapture*` 读取
- 集成测试覆盖 init 可运行、UI 回调不崩、默认 capture = false 三组路径

**Non-Goals:**
- 不把 ImGui 做成独立 `FrameGraph` pass / `RenderQueue`
- 不实现 dockspace / multi-viewport / 自定义字体
- 不在本 REQ 实现相机控制器对 UI capture 的消费（由下游 REQ-015/016/019 接入）
- 不重构 `Renderer` 抽象基类，`setDrawUiCallback` 只存在于 `VulkanRenderer`
- 不要求 GLFW 路径完整接 ImGui；GLFW 只补 `getNativeHandle()`

## Decisions

### D1: Overlay 路径（swapchain 内）而非独立 FrameGraph pass

**选择**：`Gui::endFrame(cmd)` 在 `VulkanRenderer` 的 draw loop 中、`vkCmdEndRenderPass` 之前被调用，ImGui 不进入 `FrameGraph::getPasses()`，也不建立 `Pass_DebugUI` 常量。

**替代方案**：新建独立 ImGui pass → 需要扩 `FrameGraph` 以支持"非场景 overlay pass"、扩 `RenderQueue`、扩 `PipelineBuildDesc`；短期成本大且与 REQ-017 范围冲突。

**理由**：overlay 是主流 ImGui 接线方式；`RenderDrawData(drawData, cmd, VK_NULL_HANDLE)` 已允许复用外层 render pass，成本最小；后续若真的需要独立 pass，可在 FrameGraph 就绪后平滑迁移。

### D2: `Gui` 自己拥有 descriptor pool

**选择**：`Gui::init()` 内部创建 ImGui 专用 descriptor pool（1000 × `COMBINED_IMAGE_SAMPLER` 即可），`shutdown()` 销毁。

**替代方案**：复用 `VulkanDescriptorManager` 的统一池 → ImGui backend 要求可以 free 单个 set，与统一池策略冲突。

**理由**：ImGui backend 官方示例即此做法；隔离后 `Gui` 的生命周期对场景 descriptor 无副作用。

### D3: `Gui::endFrame` 签名改为 `endFrame(VkCommandBuffer cmd)`

**选择**：显式把 command buffer 传进来。drawData 为空或 `TotalVtxCount == 0` 时直接 return，不调 `RenderDrawData`。

**替代方案**：`Gui` 内部保存当前 cmd → 需要额外 `setCurrentCommandBuffer()`；增加状态机没有好处。

**理由**：符合 ImGui 官方 backend 签名；调用方（`VulkanRenderer`）本来就持有 cmd，传参最直接。

### D4: `Window::getNativeHandle()` 用 `void*` type-erase

**选择**：`core/platform/window.hpp` 增加：

```cpp
virtual void* getNativeHandle() const = 0;
```

SDL 实现 `return m_sdlWindow;`，GLFW 实现 `return m_glfwWindow;`。`core` 层依旧不 include SDL/GLFW 头。

**替代方案**：返回 `std::variant<SDL_Window*, GLFWwindow*>` → 需要在 `core` 暴露两个 backend 类型；或把 `Gui` 构造搬到 `infra` 某个桥接层并静态 cast → 增加耦合且没必要。

**理由**：`Gui` 用到该指针的地方只有 `ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window*>(handle))`，一次 `static_cast` 开销可忽略；type erasure 的成本最低。

### D5: SDL 事件循环按 `ProcessEvent → handleSdlEvent → quit 检查` 顺序共用单次 poll

**选择**：在 `sdl_window.cpp` 的 `shouldClose()` 事件 poll 中，先调 `ImGui_ImplSDL3_ProcessEvent(&event)`，再调 `inputState->handleSdlEvent(event)`，最后检查 `SDL_EVENT_QUIT`。`ImGui_ImplSDL3_ProcessEvent` 仅在 SDL backend 已初始化后有意义——`Gui::init()` 成功后 ImGui 内部会记录初始化标志，未 init 时该函数为 no-op，所以无需在 `sdl_window` 这一侧加额外开关。

**替代方案**：在 `Gui` 内部 poll 事件 → 会造成 SDL 事件被消费两次，破坏 REQ-013 既有约定。

**理由**：REQ-013 的"单次 poll 多消费者"模式已被确立，REQ-017 遵循相同模式，不引入第二套主循环。

### D6: UI capture 作为 `IInputState` 默认虚方法

**选择**：

```cpp
virtual bool isUiCapturingMouse() const { return false; }
virtual bool isUiCapturingKeyboard() const { return false; }
```

默认实现放在 `IInputState` 本身。`DummyInputState` 不覆写；`MockInputState` 暴露 setter 以供控制器测试；`Sdl3InputState` 后续可覆写为 `ImGui::GetIO().WantCapture*`（本 REQ 只定义接口与写入点，不要求 SDL 实现立刻接通 ImGui）。

**替代方案**：新增独立接口 `IUiCapture` → 控制器需要同时持有两个指针，接口分裂。

**理由**：相机控制器只拿一个 `InputStateSharedPtr`，把 UI capture 放在同一接口上最简单；默认 false 不打扰既有调用方。

### D7: `VulkanRenderer::setDrawUiCallback` 不下沉到 `Renderer` 基类

**选择**：`setDrawUiCallback(std::function<void()>)` 只出现在 `VulkanRenderer`（以及其 PImpl）上。

**替代方案**：下沉到 `gpu::Renderer` → 非 Vulkan backend 无意义，`Renderer` 抽象应保持最小。

**理由**：REQ-017 明确划定 UI 回调入口"仅限 `VulkanRenderer`"。调用点（demo/scene viewer）一定知道自己拿的是 `VulkanRenderer`。

### D8: `Gui::init()` 所需的 render pass 与 swapchain image count 由 `VulkanRenderer` 传入

**选择**：`VulkanRenderer::initialize()` 先完成 device / swapchain / render pass 创建，再构造 `Gui::InitParams`（填 `renderPass = m_swapchainRenderPass`，`swapchainImageCount = m_swapchainImages.size()`），然后 `gui->init(params)`。

**理由**：ImGui backend 要求这两项在 init 时就知道；放进 InitParams 比内部再 query backend 更清晰，也避免 `Gui` 反向依赖 VulkanRenderer 的内部结构。

## Risks / Trade-offs

- **[Swapchain 重建后 ImGui 需重新 init]** → 本 REQ 不处理窗口 resize 导致的 swapchain 重建；若后续引入 resize，需在 `VulkanRenderer::recreateSwapchain()` 里对称调 `gui->shutdown()` + `gui->init()`。作为已知限制记录在 gui-system spec 的注释中即可。
- **[`ImGui_ImplSDL3_ProcessEvent` 在未 init 时被调用]** → ImGui 源码里该函数会读取内部 `g_BackendData`，若未 init 为 nullptr 会 early-return；但依赖实现细节不稳妥。缓解：`sdl_window.cpp` 可加一个 `bool m_imguiReady` 开关，由 `Gui::init()` 通过回调或 `Window` setter 设置；实现细节在任务阶段选定。
- **[`void*` native handle 类型不可见性]** → 误用导致的 `static_cast` UB 风险；缓解：`Gui::init()` 注释里明确"SDL 路径下必须传 `SDL_Window*`"，并通过集成测试覆盖。
- **[测试无图形环境]** → CI 无 display 时 `VulkanRenderer::initialize()` 会失败；`test_imgui_overlay` 按既有约定标记为本地集成测试，不阻塞 CI。
- **[`MockInputState` 新增 capture setter 可能破坏既有测试]** → 新方法有默认实现且不影响现有接口，现有 mock 测试无需改动。

## Migration Plan

1. 先落 `gui-system` 接口（D2/D3/D8 → 代码可编译但未被驱动）
2. 落 `window-system` 的 `getNativeHandle()`（D4） + SDL 事件 forward（D5）
3. 落 `input-abstraction` 的 UI capture 默认虚方法（D6），同步更新 `MockInputState`
4. `VulkanRenderer` 接入 `Gui`（D1/D7/D8）
5. 写集成测试
6. 更新 REQ-017 实施状态

每一步单独可 build，不需要 flag 门控。

## Open Questions

- `m_imguiReady` 开关的放置点（`sdl_window::Impl` 字段 vs 让 `Gui::isInitialized()` 作为真值源）→ 由实现阶段选最小改动路径。
