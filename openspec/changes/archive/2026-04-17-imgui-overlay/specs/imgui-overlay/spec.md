## ADDED Requirements

### Requirement: VulkanRenderer owns the Gui instance

`LX_core::backend::VulkanRenderer`（PImpl 内部）SHALL 持有一个 `infra::Gui` 成员，并在 `initialize()` 完成 Vulkan device / swapchain / swapchain render pass / swapchain image view 创建之后，对其调用 `init()`。`Gui::InitParams` 中的 `renderPass` 字段 SHALL 为 `VulkanRenderer` 自身的 swapchain render pass；`swapchainImageCount` SHALL 为当前 swapchain image 数量；`nativeWindowHandle` SHALL 来自 `WindowSharedPtr->getNativeHandle()`。

#### Scenario: Gui 在 initialize 末尾被 init

- **WHEN** 外部调用 `VulkanRenderer::initialize(window, "AppName")`
- **THEN** 在方法返回前，`Gui::init()` SHALL 已经被调用且 `isInitialized()` 为 `true`

#### Scenario: Gui 在 shutdown 对称释放

- **WHEN** 外部调用 `VulkanRenderer::shutdown()`
- **THEN** 在释放 Vulkan device 之前 `Gui::shutdown()` SHALL 先被调用，descriptor pool 与 ImGui context 在 device 销毁前全部释放

### Requirement: VulkanRenderer exposes setDrawUiCallback

`VulkanRenderer` SHALL 暴露：

```cpp
void setDrawUiCallback(std::function<void()> cb);
```

该回调 SHALL 在每帧 `draw()` 中被调用，调用点位于 `gui->beginFrame()` 之后、任何场景 draw call 之前。回调允许为空（`std::function` 判空）；为空时 SHALL 跳过调用。该方法 SHALL NOT 下沉到 `gpu::Renderer` 抽象基类。

#### Scenario: 空回调不崩溃

- **WHEN** 外部未调 `setDrawUiCallback` 或传入空 `std::function`
- **THEN** `draw()` SHALL 正常运行，ImGui 仍然完成 `beginFrame()` / `endFrame(cmd)` 的空帧录制

#### Scenario: 非空回调每帧执行一次

- **WHEN** 外部调用 `setDrawUiCallback([&]{ invoked++; })` 后连续触发 3 帧 `draw()`
- **THEN** `invoked` SHALL 等于 `3`

### Requirement: Overlay draw order inside swapchain render pass

在 `VulkanRenderer` 录制单帧 command buffer 时，SHALL 按以下顺序：

1. `vkBeginCommandBuffer`
2. `vkCmdBeginRenderPass`（swapchain render pass）
3. `gui->beginFrame()`
4. 调用 `drawUiCallback`（若非空）
5. 执行场景 `FrameGraph` 的所有 pass / draw call
6. `gui->endFrame(cmd)`
7. `vkCmdEndRenderPass`
8. `vkEndCommandBuffer`

ImGui SHALL NOT 进入 `FrameGraph::getPasses()`，亦不建立独立 `Pass_DebugUI` 常量或 `RenderQueue`。

#### Scenario: ImGui 不在 FrameGraph pass 列表中

- **WHEN** 遍历 `FrameGraph::getPasses()`
- **THEN** 返回列表 SHALL NOT 包含任何与 ImGui 相关的 pass ID

#### Scenario: endFrame 发生在 endRenderPass 之前

- **WHEN** 录制任意一帧
- **THEN** `ImGui_ImplVulkan_RenderDrawData()` SHALL 被传入的 `cmd` 尚处于 swapchain render pass 之内

### Requirement: Integration test for ImGui overlay

`src/test/integration/test_imgui_overlay.cpp` SHALL 被新增，并在 `src/test/CMakeLists.txt` 中注册。测试 SHALL 覆盖：

- `gui_init_succeeds_after_renderer_initialize`：构造 `Window` + `VulkanRenderer`，调用 `initialize()` 后断言 `Gui` 已 init
- `draw_with_ui_callback_does_not_crash`：设置一个最小 UI 回调（如 `ImGui::Text("hi")`），连续运行若干帧 `draw()` 不崩溃
- `ui_capture_flags_default_to_false_without_imgui`：构造 `DummyInputState` / `MockInputState`（无 SDL / 无 ImGui），`isUiCapturingMouse()` 与 `isUiCapturingKeyboard()` SHALL 返回 `false`

若 CI 环境无图形，相关 renderer 用例允许被跳过，但测试文件与注册条目 SHALL 存在。

#### Scenario: 所有 imgui overlay 集成测试通过

- **WHEN** 在本地具备 Vulkan 与显示环境下运行 `test_imgui_overlay`
- **THEN** 全部断言 SHALL 通过
