# REQ-017: ImGui overlay 接入 VulkanRenderer + 输入 forwarding

## 背景

`src/infra/gui/gui.hpp:7-33` 与 `src/infra/gui/imgui_gui.cpp` 已经把 ImGui Vulkan backend 的初始化 / `beginFrame` / `endFrame` / `shutdown` 写好了，但**当前没有任何代码调用它**：

- `VulkanRenderer::initialize` 没有创建 `Gui` 实例
- `VulkanRenderer::draw` 没有调 `gui->beginFrame()` / `gui->endFrame()`
- SDL3 的事件循环（`window_impl_sdl.cpp:39-46`）没有把事件 forward 给 `ImGui_ImplSDL3_ProcessEvent`
- `Gui::init` 当前没有调 `ImGui_ImplSDL3_InitForVulkan`，缺一半 backend 初始化

加上 `vulkan_renderer.cpp:276-283` 的 draw loop 已经在遍历 `m_frameGraph.getPasses()`，理想情况是把 ImGui 作为一个独立的 `Pass_DebugUI` 节点接进去 —— 但这需要：

1. 让 ImGui 走自己的 VkRenderPass（或继承 swapchain pass，但绑定不同的 pipeline）
2. 在 `FrameGraph` / `RenderQueue` 里加"非 RenderQueue 驱动的 pass 形态"

工作量太大，会把 REQ-017 拖成一个准 Phase。Phase 1 roadmap 风险段也明确写了 "post-process / fullscreen pass 不走 buildFromScene"。

**本 REQ 选择 overlay 路径**：ImGui 的 draw data 在 swapchain pass 末尾、`endRenderPass` 之前被 record 进同一个 command buffer。这让 ImGui 可以最快接入而**不破坏现有 FrameGraph 抽象**。代价是 ImGui pass 不出现在 `FrameGraph::getPasses()` 里，未来 REQ-101+ 引入 fullscreen pass 形态时再迁移。

## 目标

1. SDL3 backend 初始化 (`ImGui_ImplSDL3_InitForVulkan`) 补齐
2. SDL 事件 forward 给 ImGui，鼠标 / 键盘事件不会"穿透"UI
3. `VulkanRenderer` 持有 `Gui` 实例，draw loop 在合适位置调 begin/endFrame
4. 上层 demo 通过一个 `Gui::onDraw(callback)` 注入"画 panel 的回调"
5. ImGui 的 `WantCaptureMouse` / `WantCaptureKeyboard` 状态可以被相机控制器查询，避免抢鼠标

## 需求

### R1: `Gui::init` 补齐 SDL backend

修改 `src/infra/gui/imgui_gui.cpp:30-64` 的 `Gui::init`：

- 当前只调了 `ImGui_ImplVulkan_Init`
- 需要在前面加 `ImGui::CreateContext()` + `ImGui_ImplSDL3_InitForVulkan(sdlWindow, instance)`
- 这意味着 `Gui::InitParams` 需要新增 `SDL_Window* sdlWindow` 字段（PR 影响：caller 必须能拿到 SDL window 句柄）

修改后的 `InitParams`：

```cpp
struct InitParams {
  // 现有字段保持不变
  VkInstance instance;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  uint32_t graphicsQueueFamilyIndex;
  uint32_t presentQueueFamilyIndex;
  VkQueue graphicsQueue;
  VkQueue presentQueue;
  VkSurfaceKHR surface;
  // 新增
  void* sdlWindowHandle;     // SDL_Window* （void* 避免 gui.hpp 暴露 SDL3）
  VkRenderPass renderPass;   // ImGui_ImplVulkan_Init 需要绑定到 swapchain 的 render pass
  uint32_t swapchainImageCount;
};
```

`init` 内部：

```cpp
IMGUI_CHECKVERSION();
ImGui::CreateContext();
ImGui::StyleColorsDark();

ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window*>(params.sdlWindowHandle));

ImGui_ImplVulkan_InitInfo initInfo = {};
initInfo.Instance = params.instance;
initInfo.PhysicalDevice = params.physicalDevice;
initInfo.Device = params.device;
initInfo.QueueFamily = params.graphicsQueueFamilyIndex;
initInfo.Queue = params.graphicsQueue;
initInfo.PipelineCache = VK_NULL_HANDLE;
initInfo.DescriptorPool = pImpl->descriptorPool;  // 见 R2
initInfo.MinImageCount = 2;
initInfo.ImageCount = params.swapchainImageCount;
initInfo.RenderPass = params.renderPass;          // 关键
initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
initInfo.Allocator = nullptr;
initInfo.CheckVkResultFn = nullptr;

if (!ImGui_ImplVulkan_Init(&initInfo)) {
  throw std::runtime_error("Failed to init ImGui Vulkan backend");
}
```

`shutdown` 同步加 `ImGui_ImplSDL3_Shutdown` + `ImGui::DestroyContext`。

### R2: ImGui 专用 descriptor pool

ImGui Vulkan backend 要求一个独立 descriptor pool（容纳字体纹理 + per-window state）。`Gui::Impl` 新增：

```cpp
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
```

`init` 开头创建：标准的 1000 个 combined image sampler，单个 max set ≤ 16。`shutdown` 销毁。**不复用** `VulkanResourceManager` 的 descriptor pool，独立隔离 ImGui。

### R3: `Gui::endFrame` 接收外部 cmd buffer

当前 `endFrame()` 写死了 `VK_NULL_HANDLE` 调 `ImGui_ImplVulkan_RenderDrawData`，这是错的 —— 必须把渲染命令录进调用方的 command buffer。

修改签名：

```cpp
void endFrame(VkCommandBuffer cmd);
```

实现：

```cpp
ImGui::Render();
ImDrawData* drawData = ImGui::GetDrawData();
if (drawData && drawData->TotalVtxCount > 0) {
  ImGui_ImplVulkan_RenderDrawData(drawData, cmd, VK_NULL_HANDLE);
}
```

### R4: `VulkanRenderer` 接入 Gui

修改 `src/backend/vulkan/vulkan_renderer.cpp`：

- `Impl` 新增成员 `std::unique_ptr<infra::Gui> gui`
- `initialize` 末尾创建 `gui = std::make_unique<infra::Gui>()`，调 `gui->init(...)`
  - SDL window 句柄需要从 `Window` 接口暴露 —— 见 R5
- `draw` loop 改造（在 `vulkan_renderer.cpp:262-285` 范围）：

```cpp
cmdBufferMgr->beginFrame(currentFrameIndex);

// 给 ImGui 喂事件 + 推进帧
gui->beginFrame();

// 用户回调：填充 panel
if (drawUiCallback) {
  drawUiCallback();
}

auto cmd = cmdBufferMgr->allocateBuffer();
cmd->begin();
cmd->beginRenderPass(...);
cmd->setViewport(...);
cmd->setScissor(...);

// 现有的 frame graph draw loop
for (auto &pass : m_frameGraph.getPasses()) {
  for (auto &item : pass.queue.getItems()) {
    // ... 现有 draw item ...
  }
}

// ImGui 在所有 scene draw 之后录入同一个 render pass
gui->endFrame(cmd->getHandle());

cmd->endRenderPass();
cmd->end();
```

`drawUiCallback` 是 `std::function<void()>`，由上层注入：

```cpp
class VulkanRenderer : public Renderer {
public:
  // 新增
  void setDrawUiCallback(std::function<void()> cb) {
    drawUiCallback = std::move(cb);
  }
};
```

注：`setDrawUiCallback` 是 `VulkanRenderer` 特有的，**不**进 `Renderer` 基类（其他 backend 还没适配 ImGui）。

### R5: `Window::getNativeHandle()` 暴露 SDL window 指针

`Gui::init` 需要 `SDL_Window*`，但 `Window` 接口现在只暴露 Vulkan surface，没有 native window 句柄。

新增到 `src/core/platform/window.hpp`：

```cpp
class Window {
public:
  // ...

  /// 返回底层窗口的 native 句柄。
  /// SDL3 实现：返回 SDL_Window*
  /// GLFW 实现：返回 GLFWwindow*
  /// 调用方按 backend 类型 cast。本接口不进一步 type-erase。
  virtual void* getNativeHandle() const = 0;
};
```

SDL 实现：`return pImpl->window;`（指向 `SDL_Window*`）
GLFW 实现：`return pImpl->window;`（GLFWwindow*）

### R6: SDL 事件 forward

修改 `src/infra/window/sdl_window.cpp` 的事件循环（与 REQ-013 R3 协调）：

```cpp
bool shouldClose() {
  bool quit = false;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    // 给 ImGui 喂事件
    if (imguiEnabled) {
      ImGui_ImplSDL3_ProcessEvent(&event);
    }
    if (inputState->handleSdlEvent(event)) {
      quit = true;
    }
  }
  return quit;
}
```

`imguiEnabled` 是一个 bool，由 `setImguiEnabled(true)` 在 `Gui::init` 后被 `VulkanRenderer` 调用打开。**不**让 SDL 模块直接 include ImGui header —— 通过函数指针注入：

```cpp
// 新增到 window.hpp
void setEventForwarder(std::function<void(const SDL_Event&)> fn);
```

但这又把 SDL 类型暴露到 hpp。妥协方案：**`ImGui_ImplSDL3_ProcessEvent` 是一个全局 C 函数**，直接在 `window_impl_sdl.cpp` 内 `#ifdef LX_HAVE_IMGUI` 调用。`LX_HAVE_IMGUI` 由 CMake 在编译 SDL window 时定义。

### R7: 相机控制器避让 ImGui

REQ-015 / REQ-016 的 controller 直接读 `IInputState`，会和 ImGui 抢鼠标。本 REQ 增加一个轻量协调点：

修改 `IInputState`（在 REQ-012 接口上**追加**）：

```cpp
class IInputState {
public:
  // ...

  /// ImGui / debug UI 是否正在捕获鼠标输入。
  /// 为 true 时相机控制器应当跳过本帧的 mouse 处理。
  /// 默认实现返回 false（不依赖 UI）。
  virtual bool isUiCapturingMouse() const { return false; }
  virtual bool isUiCapturingKeyboard() const { return false; }
};
```

`Sdl3InputState` 实现：暴露两个 setter，`VulkanRenderer::draw` 在 `gui->beginFrame()` 之后写入：

```cpp
gui->beginFrame();
auto io = ImGui::GetIO();
sdlInputState->setUiCapturingMouse(io.WantCaptureMouse);
sdlInputState->setUiCapturingKeyboard(io.WantCaptureKeyboard);
```

REQ-015 / REQ-016 的 `update` 内做：

```cpp
if (input.isUiCapturingMouse()) return;  // 跳过本帧
```

注意：本 REQ 在 REQ-012 接口上**追加**两个虚方法，不破坏。`DummyInputState` / `MockInputState` 默认实现已经返回 false。

## 测试

**人工测试**：

- `demo_scene_viewer`（REQ-019）启动后，ImGui 窗口可见、可拖动、可点击
- 点击 ImGui 窗口时相机不会被旋转
- 鼠标移出 ImGui 窗口后右键拖拽相机正常

**集成测试**（headless 范围）：

- `src/test/integration/test_imgui_overlay.cpp`：
  - 构造 `Window` + `VulkanRenderer`，调 `initialize` 后断言 `gui->isInitialized() == true`
  - 调 `setDrawUiCallback` 后跑 5 帧 draw，断言无崩溃、`vkQueueWaitIdle` 正常
  - 不验证像素，CI 没头显示

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/infra/gui/gui.hpp` | `InitParams` 加 `sdlWindowHandle` / `renderPass` / `swapchainImageCount`；`endFrame(VkCommandBuffer)` 改签名 |
| `src/infra/gui/imgui_gui.cpp` | R1+R2+R3 实现 |
| `src/infra/gui/CMakeLists.txt` | 添加 `imgui_impl_sdl3.cpp` 到 sources（如未启用） |
| `src/backend/vulkan/vulkan_renderer.hpp` / `.cpp:119,237-322` | R4：持有 Gui、draw 接入、`setDrawUiCallback` |
| `src/core/platform/window.hpp` | R5：`getNativeHandle()` 纯虚 |
| `src/infra/window/window.hpp` / `window_impl_sdl.cpp` / `window_impl_glfw.cpp` | R5+R6：实现 `getNativeHandle`，SDL 事件 forward |
| `src/core/input/input_state.hpp` | R7：追加 `isUiCapturingMouse / Keyboard` 默认 false |
| `src/infra/window/sdl3_input_state.hpp` / `.cpp` | R7：实现两个 setter |
| `src/test/integration/test_imgui_overlay.cpp` | 新增 |
| `src/test/integration/CMakeLists.txt` | 注册 |
| `CMakeLists.txt` | 定义 `LX_HAVE_IMGUI` 编译期宏 |

## 边界与约束

- **不做** ImGui 作为独立 FrameGraph pass —— 当前是 overlay 模式，留作 REQ-101+ 的技术债
- **不做** ImGui dockspace / multi-viewport —— Phase 1 单窗口够用
- **不做** ImGui font 自定义 —— 默认 ProggyClean 字体
- **不做** GLFW backend 的 ImGui 集成 —— 项目主用 SDL3，GLFW 留 stub
- 不动 `Renderer` 抽象基类 —— `setDrawUiCallback` 仅 `VulkanRenderer` 暴露
- ImGui 渲染必须发生在 `endRenderPass` 之前，否则 validation layer 会抱怨

## 依赖

- **REQ-012**（必需）：`IInputState` 提供 `isUiCapturingMouse / Keyboard` 钩子
- **REQ-013**（必需）：SDL 真实事件循环（forward 给 ImGui 的入口在那里）

## 下游

- **REQ-018**：`DebugPanel` helper 包装 ImGui widget，在 `setDrawUiCallback` 里被调用
- **REQ-019**：demo_scene_viewer 注入 `setDrawUiCallback` 显示 panel
- **REQ-101+**：在引入独立 fullscreen post-process pass 时，回头把 ImGui 也变成正经的 FrameGraph pass

## 实施状态

未开始。
