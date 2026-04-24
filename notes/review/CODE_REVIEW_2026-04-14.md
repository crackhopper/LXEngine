# Renderer Demo Code Review (2026-04-14)

## 1. Review 范围与方法

- 范围：`src/core`、`src/infra`、`src/backend`、`src/test`（不含 `src/infra/external` 第三方代码）。
- 方法：静态代码审查 + 构建配置与测试策略检查。
- 重点：生命周期管理、跨层接口契约、Vulkan 资源正确性、测试有效性、与项目规范一致性。

## 2. 主要问题（按严重级别）

### [High] SDL 窗口 `updateSize` 被空实现覆盖，窗口尺寸更新逻辑失效

- 证据：
  - `src/infra/window/sdl_window.cpp:72` 存在 `Impl::updateSize(...)` 的完整实现。
  - `src/infra/window/sdl_window.cpp:138` `Window::updateSize(...)` 却是空函数。
- 风险：
  - 调用方认为可以通过 `Window::updateSize` 获取 resize/minimize 后的有效尺寸，但当前行为是“静默不更新”。
  - 与 swapchain 重建、最小化窗口处理相关的逻辑容易出现错误或卡死。
- 建议：
  - 让 `Window::updateSize(...)` 转发到 `pImpl->updateSize(...)`。
  - 增加一个集成测试覆盖“最小化->恢复->重建 swapchain”的流程。

### [High] GLFW 路径存在接口与句柄语义错误，当前基本不可用

- 证据：
  - `src/infra/window/glfw_window.cpp:68` 定义为 `bool Window::shouldClose() const`，但接口是非 `const`：`src/infra/window/window.hpp:26`。
  - `src/infra/window/glfw_window.cpp:83` 返回 `new VkSurfaceKHR(...)`（堆上指针）。
  - `src/backend/vulkan/details/device.cpp:169` 将 `createGraphicsHandle(...)` 返回值直接强转为 `VkSurfaceKHR`。
  - `src/infra/window/glfw_window.cpp:88`-`92` `destroyGraphicsHandle(...)` 空实现，没有释放 `new` 的对象。
- 风险：
  - `USE_GLFW=ON` 下可能编译失败（签名不匹配）。
  - 即使绕过编译问题，surface 句柄也可能被错误解释，导致 Vulkan 初始化失败或未定义行为。
  - 明确的内存泄漏。
- 建议：
  - `createGraphicsHandle` 直接返回真实的 `VkSurfaceKHR`（与 SDL 保持同一语义），不要 `new`。
  - 修正 `shouldClose` 签名与接口一致。
  - 为 `USE_SDL` 与 `USE_GLFW` 都建立 CI 编译任务，防止分支路径腐化。

### [High] `VulkanDevice::findQueueFamilies` 使用成员状态累积，设备筛选可能错误

- 证据：
  - `src/backend/vulkan/details/device.cpp:305`、`316` 直接写 `m_queueIndices`。
  - `src/backend/vulkan/details/device.cpp:359` 在筛选每个物理设备时都会调用 `findQueueFamilies(device)`。
  - 函数内部没有在入口重置 `m_queueIndices`。
- 风险：
  - 前一个设备的队列索引可能污染后一个设备判定。
  - 设备可用性判断出现“误判为可用”，最终在后续创建阶段失败。
- 建议：
  - `findQueueFamilies` 使用局部 `QueueFamilyIndices indices`，最后返回局部结果。
  - 仅在最终选定设备后赋值给 `m_queueIndices`。

### [Medium] `VulkanSwapchain::cleanup` 对同步对象数组做越界访问风险

- 证据：
  - `src/backend/vulkan/details/render_objects/swapchain.cpp:50`-`56` 按 `m_maxFramesInFlight` 直接索引三个 vector。
  - 若在 `initialize` 早期抛异常（例如 `createInternal` 失败），`createSyncObjects` 尚未执行，这些 vector 可能为空。
- 风险：
  - 析构路径或异常回滚路径触发越界访问，导致二次崩溃，掩盖原始错误。
- 建议：
  - cleanup 时按各自 vector 的 `size()` 遍历，或先判空。
  - 将同步对象销毁逻辑封装成独立函数并做状态机式保护（Created/Destroyed）。

### [Medium] `TextureLoader::load` 重复加载会泄漏旧图像内存

- 证据：
  - `src/infra/texture_loader/texture_loader.cpp:30` 新分配 `imageData`。
  - `src/infra/texture_loader/texture_loader.cpp:39` 直接覆盖 `pImpl->data`，未释放旧指针。
- 风险：
  - 热重载或多次加载同一 `TextureLoader` 实例会持续泄漏。
- 建议：
  - 赋新值前 `stbi_image_free(pImpl->data)`。
  - 更推荐使用 `std::unique_ptr<unsigned char, custom_deleter>` 消除手工释放分支。

### [Medium] 多处 PImpl 使用裸指针 + 可拷贝，存在双重释放风险且违背项目规范

- 证据：
  - 裸指针成员：`src/infra/window/window.hpp:32`、`src/infra/gui/gui.hpp:32`、`src/infra/texture_loader/texture_loader.hpp:21`、`src/infra/mesh_loader/obj_mesh_loader.hpp:22`、`src/infra/mesh_loader/gltf_mesh_loader.hpp:22`、`src/backend/vulkan/vulkan_renderer.hpp:25`。
  - 配套 `new/delete`：如 `src/backend/vulkan/vulkan_renderer.cpp:363`、`366`。
  - 上述类普遍未显式 delete copy/move。
- 风险：
  - 默认拷贝构造会浅拷贝 `pImpl`，触发 double free。
  - 与 `openspec/specs/cpp-style-guide/spec.md` 的“对象引用禁用裸指针、RAII 全面管理”不一致。
- 建议：
  - 统一改为 `std::unique_ptr<Impl>`。
  - 明确 `=delete` copy，按需提供 move。
  - 对 `VulkanRenderer` 可直接持有 `std::unique_ptr<gpu::Renderer>` 或去掉包装层。

### [Medium] GLTF 头解析使用 `reinterpret_cast` 读取，存在未定义行为风险

- 证据：
  - `src/infra/mesh_loader/gltf_mesh_loader.cpp:33`：`uint32_t magic = *reinterpret_cast<uint32_t *>(header);`
- 风险：
  - 违反严格别名/对齐要求，平台相关 UB。
- 建议：
  - 使用 `std::memcpy(&magic, header, sizeof(magic))`。
  - 同时补充 endianness 注释与校验。

### [Medium] Vulkan 集成测试大量“异常即 SKIP 且返回 0”，会掩盖真实失败

- 证据：
  - `src/test/integration/test_vulkan_swapchain.cpp:44`-`45` 异常时 `return 0`。
  - `src/test/integration/test_vulkan_buffer.cpp:55`-`57` 异常时 `return 0`。
- 风险：
  - CI 绿色不代表功能可用，尤其在驱动/窗口系统变更后会漏报回归。
- 建议：
  - 将“环境不满足”与“测试失败”区分。
  - 只有明确识别到“无 Vulkan 运行环境”时标记 skip；其余异常返回非 0。

## 3. 优先级建议（修复顺序）

1. 修复窗口后端契约问题（SDL `updateSize`、GLFW surface 语义、签名一致性）。
2. 修复 `findQueueFamilies` 状态污染与 `swapchain.cleanup` 越界风险。
3. 清理裸指针 PImpl，完成 RAII/Rule-of-Five 收敛。
4. 提升测试可信度（双后端 CI + 真失败不吞）。
5. 补齐 `TextureLoader`、`GLTFLoader` 的内存/UB 细节。

## 4. 总结

项目整体架构（core/infra/backend 分层、FrameGraph + PipelineBuildDesc 路线）是清晰的，但当前在“后端分支路径可维护性”和“生命周期一致性”上有几处高风险缺口。建议先把窗口与设备初始化链路做成“强契约 + 双后端持续验证”，再推进功能扩展，这样能显著降低后续渲染问题定位成本。
