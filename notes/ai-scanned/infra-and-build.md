# Infra 与构建层扫描：半接线功能和资源生命周期缺口

这一层的问题更像“接口看起来齐了，但真正连到运行路径时会掉链子”。它们会让上层误以为某个能力已经存在，结果在 resize、图片热加载或 GUI 接入时才暴露出断层。

## 高风险问题

| 严重度 | 位置 | 现象 | 影响 |
| --- | --- | --- | --- |
| 高 | [src/infra/window/sdl_window.cpp](../../src/infra/window/sdl_window.cpp) | `Impl::updateSize()` 写了完整逻辑，但公开的 `Window::updateSize()` 是空函数 | SDL 窗口尺寸更新接口对外失效，调用方拿不到 resize/minimize 结果 |
| 高 | [src/infra/texture_loader/texture_loader.cpp](../../src/infra/texture_loader/texture_loader.cpp) | `load()` 在覆盖 `pImpl->data` 前没有释放旧图像 | 同一个 `TextureLoader` 实例重复加载会持续泄漏像素内存 |
| 高 | [src/infra/gui/imgui_gui.cpp](../../src/infra/gui/imgui_gui.cpp)、[src/infra/external/imgui/backends/imgui_impl_vulkan.cpp](../../src/infra/external/imgui/backends/imgui_impl_vulkan.cpp) | GUI 封装初始化时同时把 `DescriptorPool` 设为 `VK_NULL_HANDLE`、`DescriptorPoolSize` 设为 `0` | 一旦真的调用 `Gui::init()`，ImGui Vulkan backend 会在断言里直接停掉 |

## 证据

### 1. SDL resize 逻辑被实现体和公开接口拆裂了

- [src/infra/window/sdl_window.cpp](../../src/infra/window/sdl_window.cpp) 第 76-96 行：`Impl::updateSize()` 会等待窗口恢复、处理 `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`
- 同文件第 146 行：公开的 `Window::updateSize()` 直接空实现

这意味着当前接口名义上支持 resize，实际没有把内部逻辑暴露给任何调用方。

### 2. `TextureLoader` 只有析构释放，没有“覆盖旧数据前释放”

- [src/infra/texture_loader/texture_loader.cpp](../../src/infra/texture_loader/texture_loader.cpp) 第 14-18 行只在析构里 `stbi_image_free(data)`
- 同文件第 27-39 行每次 `load()` 都把 `pImpl->data` 直接改成新返回值

如果材质加载、工具链预览或编辑器反复复用同一个 loader，这里会形成稳定泄漏。

### 3. GUI 子系统既没有接到主路径，也没有初始化完整

- [src/infra/CMakeLists.txt](../../src/infra/CMakeLists.txt) 第 291-321 行：ImGui 代码始终进 `LX_Infra`
- `rg` 扫描项目内调用点时，`Gui` 只出现在 [src/infra/gui/gui.hpp](../../src/infra/gui/gui.hpp) 和 [src/infra/gui/imgui_gui.cpp](../../src/infra/gui/imgui_gui.cpp)
- [src/infra/gui/imgui_gui.cpp](../../src/infra/gui/imgui_gui.cpp) 第 52-53 行：`DescriptorPool = VK_NULL_HANDLE` 且 `DescriptorPoolSize = 0`
- [src/infra/external/imgui/backends/imgui_impl_vulkan.cpp](../../src/infra/external/imgui/backends/imgui_impl_vulkan.cpp) 第 1289-1292 行：明确要求“两者必须二选一，且不能都空”

这说明当前 GUI 代码不是“已集成但未使用”，而是“未接线且启用即坏”。

## 设计问题

| 类型 | 位置 | 说明 |
| --- | --- | --- |
| 规范违背 | [src/infra/window/window.hpp](../../src/infra/window/window.hpp) 第 33-34 行、[src/infra/gui/gui.hpp](../../src/infra/gui/gui.hpp) 第 30-32 行 | 两套 PImpl 都在手写裸指针生命周期，没有用 `std::unique_ptr` |
| 重复/漂移 | [src/infra/window/glfw_window.cpp](../../src/infra/window/glfw_window.cpp)、[src/infra/window/sdl_window.cpp](../../src/infra/window/sdl_window.cpp) | 两个窗口后端共享同一抽象接口，但 `createGraphicsHandle()`、`destroyGraphicsHandle()`、`updateSize()` 的语义已经漂移成不同版本 |
| 过早集成 | [src/infra/CMakeLists.txt](../../src/infra/CMakeLists.txt) 第 291-321 行 | 没有主路径调用、也没有通过构建选项隔离的 GUI 代码仍然强绑定到 `LX_Infra`，会放大编译成本和未来排障范围 |

## 建议的修正方向

| 优先级 | 建议 |
| --- | --- |
| P0 | 让 `Window::updateSize()` 真正转发到 `Impl::updateSize()` |
| P0 | 在 `TextureLoader::load()` 里先释放旧 `data`，再接收新图像 |
| P0 | 要么补全 `Gui::init()` 所需的 descriptor pool / render path，要么在构建层先把这套代码隔离出主库 |
| P1 | 统一窗口后端的句柄与销毁契约 |
| P1 | 把 PImpl 全部改成 `std::unique_ptr`，避免继续手写 `new/delete` |

## 继续阅读

- [Backend Vulkan 扫描](backend-vulkan.md)
- [Window System Spec](../../openspec/specs/window-system/spec.md)
- [Shader 反射源码分析](../source_analysis/src/core/asset/shader.md)
