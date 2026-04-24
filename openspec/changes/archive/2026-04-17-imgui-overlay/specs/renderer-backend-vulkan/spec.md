## ADDED Requirements

### Requirement: VulkanRenderer drives an ImGui overlay

`LX_core::backend::VulkanRenderer` SHALL 持有一个 `infra::Gui` 成员（可通过 PImpl 间接持有）。`initialize(WindowSharedPtr window, const char* appName)` SHALL 在 Vulkan device / swapchain / swapchain render pass 均建立之后构造 `Gui::InitParams` 并调用 `gui.init(params)`；`InitParams::nativeWindowHandle` SHALL 为 `window->getNativeHandle()`；`InitParams::renderPass` 与 `InitParams::swapchainImageCount` SHALL 与 renderer 自身 swapchain 一致。

`shutdown()` SHALL 在释放 Vulkan device 之前对称调用 `gui.shutdown()`。

`draw()` 单帧命令录制 SHALL 按以下顺序：

1. `vkBeginCommandBuffer`
2. `vkCmdBeginRenderPass`（swapchain render pass）
3. `gui.beginFrame()`
4. 调用 `drawUiCallback`（若已注册且非空）
5. 执行 `FrameGraph` 的所有 pass / draw call
6. `gui.endFrame(cmd)`
7. `vkCmdEndRenderPass`
8. `vkEndCommandBuffer`

ImGui SHALL NOT 出现在 `FrameGraph::getPasses()` 中。

#### Scenario: Gui 随 VulkanRenderer 生命周期

- **WHEN** `VulkanRenderer::initialize()` 成功返回
- **THEN** 内部 `Gui` 实例的 `isInitialized()` SHALL 为 `true`

#### Scenario: draw 空 UI 回调不崩

- **WHEN** `setDrawUiCallback` 未被调用或传入空 `std::function` 的情况下连续运行若干帧 `draw()`
- **THEN** 每帧 SHALL 仍执行 `beginFrame` / `endFrame(cmd)`，不得崩溃

### Requirement: VulkanRenderer exposes draw UI callback

`VulkanRenderer` SHALL 暴露：

```cpp
void setDrawUiCallback(std::function<void()> cb);
```

该方法 SHALL NOT 下沉到 `gpu::Renderer` 抽象基类；其他 backend 不要求实现。回调 SHALL 在 `draw()` 中、`gui.beginFrame()` 之后、任何场景 draw call 之前被调用。存在多次 `setDrawUiCallback` 调用时 SHALL 以最后一次为准（替换语义，非追加）。

#### Scenario: 回调替换语义

- **WHEN** 依次调用 `setDrawUiCallback(cb1)` 与 `setDrawUiCallback(cb2)` 后触发一次 `draw()`
- **THEN** 仅 `cb2` SHALL 被调用，`cb1` SHALL NOT 被调用

#### Scenario: 回调执行于 beginFrame 之后、场景绘制之前

- **WHEN** 回调内部调用 `ImGui::Text("x")` 并且场景内有至少一个 draw call
- **THEN** 回调中的 ImGui 调用 SHALL 被 `gui.endFrame(cmd)` 正确聚合进 ImDrawData；场景 draw call 的 command recording SHALL 发生在回调之后
