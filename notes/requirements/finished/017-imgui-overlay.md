# REQ-017: ImGui overlay 接入 VulkanRenderer

## 背景

本仓库已经引入了 ImGui 及其 SDL3 / Vulkan backend，但 2026-04-16 按当前代码核查，接线仍停留在半成品状态：

- [src/infra/gui/gui.hpp](../../src/infra/gui/gui.hpp) 与 [src/infra/gui/imgui_gui.cpp](../../src/infra/gui/imgui_gui.cpp) 已存在 `Gui` 包装，但它还没有被 `VulkanRenderer` 使用
- `Gui::init()` 目前只调用了 `ImGui_ImplVulkan_Init()`，没有 `ImGui::CreateContext()`、没有 `ImGui_ImplSDL3_InitForVulkan()`，也没有有效的 descriptor pool / render pass / image count
- `Gui::beginFrame()` 已经调用 `ImGui_ImplSDL3_NewFrame()`，但 SDL backend 实际上尚未初始化
- `Gui::endFrame()` 现在用 `VK_NULL_HANDLE` 调 `ImGui_ImplVulkan_RenderDrawData()`，不能工作
- [src/backend/vulkan/vulkan_renderer.cpp](../../src/backend/vulkan/vulkan_renderer.cpp) 还没有 `Gui` 成员、没有 UI 回调、也没有在 draw loop 里记录 ImGui draw data
- [src/core/platform/window.hpp](../../src/core/platform/window.hpp) 只有 `getInputState()`，还没有 `getNativeHandle()`，`Gui` 也拿不到 `SDL_Window*`
- SDL window 事件循环当前只处理 close，没有把事件 forward 给 ImGui
- `REQ-013` 约定的 SDL 真实输入实现尚未落地，因此 `REQ-017` 必须把它作为依赖，而不是默认已完成

把 ImGui 做成独立 FrameGraph pass 在设计上更整洁，但当前 `FrameGraph` / `RenderQueue` 还没有“非场景驱动 overlay pass”形态。为了尽快形成可用闭环，本 REQ 选择更小的落地方式：

`ImGui` 作为 swapchain render pass 内的 overlay，在场景绘制完成后、`endRenderPass` 前录入同一个 command buffer。

## 目标

1. 补齐 ImGui SDL3 + Vulkan backend 初始化
2. 让 SDL 事件能 forward 到 ImGui
3. 让 `VulkanRenderer` 持有并驱动 `Gui`
4. 允许 demo 注入一个简单的“画 UI 回调”
5. 为后续相机控制器提供最小的 UI capture 协调点

## 需求

### R1: 修正 `Gui` 初始化契约

修改 [src/infra/gui/gui.hpp](../../src/infra/gui/gui.hpp) 与 [src/infra/gui/imgui_gui.cpp](../../src/infra/gui/imgui_gui.cpp)。

`Gui::InitParams` 需要扩展为当前 ImGui Vulkan backend 真正所需的初始化参数：

```cpp
struct InitParams {
  VkInstance instance;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  uint32_t graphicsQueueFamilyIndex;
  uint32_t presentQueueFamilyIndex;
  VkQueue graphicsQueue;
  VkQueue presentQueue;
  VkSurfaceKHR surface;
  void* nativeWindowHandle;   // SDL 路径下实际为 SDL_Window*
  VkRenderPass renderPass;
  uint32_t swapchainImageCount;
};
```

`Gui::init()` 至少应按以下顺序完成：

1. `IMGUI_CHECKVERSION()`
2. `ImGui::CreateContext()`
3. `ImGui::StyleColorsDark()` 或项目选定的默认 style
4. `ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window*>(params.nativeWindowHandle))`
5. 创建 ImGui 专用 descriptor pool
6. 调用 `ImGui_ImplVulkan_Init()`，并传入有效的 `renderPass`、`swapchainImageCount`、descriptor pool

`Gui::shutdown()` 必须对称执行：

1. `ImGui_ImplVulkan_Shutdown()`
2. `ImGui_ImplSDL3_Shutdown()`
3. 销毁 ImGui descriptor pool
4. `ImGui::DestroyContext()`

### R2: `Gui::endFrame` 必须接收真实 command buffer

当前 `Gui::endFrame()` 用 `VK_NULL_HANDLE` 调 `ImGui_ImplVulkan_RenderDrawData()`，这只是占位写法。

接口改为：

```cpp
void endFrame(VkCommandBuffer cmd);
```

行为要求：

- `beginFrame()` 负责 `ImGui_ImplSDL3_NewFrame()`、`ImGui_ImplVulkan_NewFrame()`、`ImGui::NewFrame()`
- `endFrame(cmd)` 负责 `ImGui::Render()` 与 `ImGui_ImplVulkan_RenderDrawData(drawData, cmd, VK_NULL_HANDLE)`
- 若 `drawData == nullptr` 或顶点数为 0，可直接跳过提交

### R3: `VulkanRenderer` 驱动 ImGui overlay

修改 [src/backend/vulkan/vulkan_renderer.hpp](../../src/backend/vulkan/vulkan_renderer.hpp) 与 [src/backend/vulkan/vulkan_renderer.cpp](../../src/backend/vulkan/vulkan_renderer.cpp)。

要求：

- `VulkanRenderer` 内部持有一个 `Gui`
- `initialize()` 完成 Vulkan device / render pass / swapchain 建立后，构造并初始化 `Gui`
- `draw()` 中在录制场景 draw call 之前执行 `gui->beginFrame()`
- 暴露一个仅限 `VulkanRenderer` 的 UI 回调入口，例如：

```cpp
void setDrawUiCallback(std::function<void()> cb);
```

- `draw()` 中在 `gui->beginFrame()` 后调用该回调，让上层填充 widget
- 场景绘制完成后、`endRenderPass()` 之前调用 `gui->endFrame(cmd)`

本 REQ 明确采用 overlay 路径：

- ImGui 不进入 `FrameGraph::getPasses()`
- ImGui 不建立独立 `Pass_DebugUI`
- ImGui 绘制发生在 swapchain render pass 内部尾部

### R4: `Window` 暴露 native handle

为了让 `Gui` 在 SDL 路径下拿到 `SDL_Window*`，扩展 [src/core/platform/window.hpp](../../src/core/platform/window.hpp)：

```cpp
virtual void* getNativeHandle() const = 0;
```

实现要求：

- SDL window 返回底层 `SDL_Window*`
- GLFW window 返回底层 `GLFWwindow*`
- 该接口只做 type erasure，不在 `core` 层暴露 SDL / GLFW 头文件

对应实现文件包括：

- [src/infra/window/window.hpp](../../src/infra/window/window.hpp)
- [src/infra/window/sdl_window.cpp](../../src/infra/window/sdl_window.cpp)
- [src/infra/window/glfw_window.cpp](../../src/infra/window/glfw_window.cpp)

### R5: SDL 事件转发给 ImGui

本 REQ 依赖 `REQ-013` 提供真实 SDL 输入状态实现；在此基础上，SDL 事件轮询需要同时服务于：

1. close 事件
2. `Sdl3InputState` 状态更新
3. ImGui backend 事件转发

行为要求：

- 在 SDL 事件循环中调用 `ImGui_ImplSDL3_ProcessEvent(&event)`
- 该调用必须发生在 SDL backend 已初始化之后
- 不能因为接入 ImGui 而破坏现有 close 行为

实现方式不要求固定为某一种注入手段，但有两个约束：

- 不引入不存在的 `window_impl_sdl.cpp` / `window_impl_glfw.cpp`
- 不新增一个与 `REQ-012/013` 冲突的第二套事件主循环

### R6: UI capture 协调点

当前 [src/core/input/input_state.hpp](../../src/core/input/input_state.hpp) 还没有 UI capture 相关接口。为了让后续 Orbit / FreeFly 控制器在点击 ImGui 时不抢鼠标，本 REQ 在 `REQ-012` 的基础上补两个默认虚方法：

```cpp
virtual bool isUiCapturingMouse() const { return false; }
virtual bool isUiCapturingKeyboard() const { return false; }
```

要求：

- `DummyInputState` 直接继承默认行为即可
- `MockInputState` 也应能覆盖这两个状态，供控制器测试使用
- SDL 真实输入实现完成后，应允许写入 `ImGui::GetIO().WantCaptureMouse / WantCaptureKeyboard`

本 REQ 只负责把接口和写入点定义清楚，不在这里实现完整的“相机控制器如何消费 UI capture”逻辑；那部分由 `REQ-015` / `REQ-016` / `REQ-019` 接入。

## 测试

新增 `src/test/integration/test_imgui_overlay.cpp`，至少覆盖：

- `gui_init_succeeds_after_renderer_initialize`
  - 初始化 `Window + VulkanRenderer` 后，ImGui 路径可被成功建立
- `draw_with_ui_callback_does_not_crash`
  - 设置一个简单 `drawUiCallback`，连续跑若干帧 `draw()`，断言无崩溃
- `ui_capture_flags_default_to_false_without_imgui`
  - `DummyInputState` / `MockInputState` 默认行为正确

测试约束：

- 测试注册点是 [src/test/CMakeLists.txt](../../src/test/CMakeLists.txt)
- 不做像素级截图验证
- 若 CI 环境不具备图形条件，可将 renderer 相关测试标记为本地集成测试；但需求文档仍应保留对应验证目标

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/infra/gui/gui.hpp` | 扩展 `InitParams`，调整 `endFrame` 签名 |
| `src/infra/gui/imgui_gui.cpp` | 补齐 SDL3/Vulkan backend 初始化与 shutdown；创建 descriptor pool |
| `src/backend/vulkan/vulkan_renderer.hpp` / `.cpp` | 持有 `Gui`、新增 UI 回调、在 draw loop 中接入 overlay |
| `src/core/platform/window.hpp` | 新增 `getNativeHandle()` |
| `src/infra/window/window.hpp` / `sdl_window.cpp` / `glfw_window.cpp` | 实现 `getNativeHandle()`；SDL 路径接入 ImGui event forwarding |
| `src/core/input/input_state.hpp` | 追加 UI capture 默认虚方法 |
| `src/test/integration/test_imgui_overlay.cpp` | 新增 |
| `src/test/CMakeLists.txt` | 注册测试 |

注：

- ImGui backend 源文件已经由现有 external imgui 构建逻辑编译；本 REQ 不应再引用不存在的 `src/infra/gui/CMakeLists.txt`
- 当前 `Gui` 位于 `namespace infra`；若后续统一到 `LX_infra`，属于额外整理项，不阻塞本 REQ

## 边界与约束

- 不做 ImGui 独立 FrameGraph pass
- 不做 dockspace / multi-viewport
- 不做自定义字体系统
- 不要求 GLFW 路径同步做完整 ImGui backend 集成；GLFW 至少补 `getNativeHandle()`，主线以 SDL 为准
- 不修改 `Renderer` 抽象基类；UI 回调入口仅是 `VulkanRenderer` 的扩展接口

## 依赖

- `REQ-012`：输入抽象与 `Window::getInputState()`
- `REQ-013`：SDL 真实输入状态与事件循环

## 下游

- `REQ-018`：基于 ImGui 构建调试面板
- `REQ-019`：demo scene viewer 注入 UI 回调，并与 Orbit / FreeFly 做输入协调

## 实施状态

2026-04-17 已落地（对应 OpenSpec change `imgui-overlay`）：

- `Gui::InitParams` 扩至包含 `nativeWindowHandle` / `renderPass` / `swapchainImageCount`；`init()` 完成 `CheckVersion → CreateContext → StyleColorsDark → ImplSDL3_InitForVulkan → 创建 descriptor pool → ImplVulkan_Init` 全链路，`shutdown()` 对称释放；`endFrame(VkCommandBuffer)` 接收真实 cmd 并对空 drawData 早退
- `VulkanRenderer` 持有 `infra::Gui`，`initialize()` 末尾装配 InitParams 并调 init；`draw()` 按 "begin pass → beginFrame → UI 回调 → 场景 draw → endFrame(cmd) → end pass" 顺序录制；新增 `setDrawUiCallback(std::function<void()>)`
- `Window` 新增 `getNativeHandle()`，SDL 返回 `SDL_Window*`，GLFW 返回 `GLFWwindow*`
- `sdl_window::Impl::shouldClose()` 在单次 SDL poll 中先调 `ImGui_ImplSDL3_ProcessEvent`，再调 `Sdl3InputState::handleSdlEvent`，最后检查 quit；未 init 时通过 `ImGui::GetCurrentContext() != nullptr` 判空跳过 forward
- `IInputState` 新增 `isUiCapturingMouse/Keyboard` 默认虚方法（默认返回 `false`）；`MockInputState` 暴露 setter 以供控制器测试
- 集成测试 `test_imgui_overlay` 覆盖三条路径；在无 display 环境下 renderer 用例自动跳过，UI capture 默认行为断言通过
