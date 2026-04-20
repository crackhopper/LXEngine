# Codex 项目评审与后续规划

评审时间：2026-04-19

这份文档不是逐文件抄录，而是站在“项目现状盘点”的角度，回答四个问题：

1. 这个仓库现在的结构是什么。
2. 哪些实现已经连成主路径，说明项目不再是空壳。
3. 哪些风险已经能从代码中直接证实，而且值得尽快处理。
4. 接下来更合理的推进顺序是什么。

先说结论：我们现在面对的是一个“主渲染路径已经成型，但边界契约、数学正确性和工程收口还没完成”的 Vulkan 渲染器仓库。`core / infra / backend` 的分层主线是清楚的，`scene_viewer` demo、材质/Shader 编译反射、场景验证、FrameGraph/RenderQueue 到 Vulkan draw 的路径也已经打通；但在窗口句柄契约、基础数学实现、资源与 PImpl 生命周期、测试运行环境这些地方，还存在会直接拖慢后续演进的硬问题。

## 结构速览

| 区域 | 路径 | 现状判断 |
| --- | --- | --- |
| Core | `src/core/` | 负责抽象、场景、材质、资源、FrameGraph、数学与时间系统，已经形成主干 |
| Infra | `src/infra/` | 负责窗口、Shader 编译/反射、材质加载、GUI、mesh/texture loader，能力较多，但风格和命名还没完全收口 |
| Backend | `src/backend/` | Vulkan backend 已覆盖 device / swapchain / resource / pipeline / command buffer / renderer |
| Tests | `src/test/` | 以集成测试为主，CPU 侧覆盖还可以，图形侧更依赖本机环境 |
| Demo | `src/demos/scene_viewer/` | 已有默认 demo，可作为主路径样例 |
| 设计与说明 | `notes/`、`openspec/specs/` | 规范和设计文档比较完整，但部分扫描结论已经过期，需要继续同步 |

## 当前实现状态

### 我们已经具备的主路径

| 能力 | 代码依据 | 现状 |
| --- | --- | --- |
| 场景驱动主循环 | `src/core/gpu/engine_loop.cpp` | `initialize -> startScene -> tickFrame/run` 语义明确，测试可跑通 |
| Scene 到绘制队列 | `src/core/frame_graph/frame_graph.cpp`、`src/core/frame_graph/render_queue.cpp` | `Scene -> FrameGraph -> RenderQueue -> RenderingItem` 已经落地 |
| SceneNode 结构校验 | `src/core/scene/object.cpp` | 会基于 shader 反射、vertex layout、skeleton/descriptor 资源做验证缓存 |
| Shader 运行时编译与反射 | `src/infra/shader_compiler/` | 编译和反射链路能跑，`test_shader_compiler` 在给定 shader 目录后通过 |
| 材质与资源绑定 | `src/core/asset/`、`src/infra/material_loader/` | 模板、实例、binding、variant 规则已经形成体系 |
| Vulkan 渲染主路径 | `src/backend/vulkan/vulkan_renderer.cpp` | 初始化、资源同步、pipeline preload、draw loop 已贯通 |
| GUI 叠加层 | `src/backend/vulkan/vulkan_renderer.cpp`、`src/infra/gui/imgui_gui.cpp` | 已经接入主 renderer，不再是孤立代码 |

### 当前项目更像什么

从仓库现实看，它已经不是“从零开始的 renderer skeleton”，而是一个正在从“能跑 demo”走向“可持续演进”的中期工程：

- 架构分层是清楚的。
- 核心抽象不是纸面设计，很多已经有真实消费者。
- 测试覆盖偏“模块集成验证”，而不是纯单元测试。
- 最大问题不在“有没有功能”，而在“功能边界是否稳定、正确、可维护”。

## 已确认的明显风险

下面这些问题不是猜测，而是当前代码里已经能直接定位到的风险点。

### P0：窗口后端的 Vulkan surface 契约不一致

| 项 | 说明 |
| --- | --- |
| 位置 | [src/infra/window/glfw_window.cpp](../../src/infra/window/glfw_window.cpp)、[src/backend/vulkan/details/device.cpp](../../src/backend/vulkan/details/device.cpp) |
| 证据 | `glfw_window.cpp` 第 109-113 行把 `VkSurfaceKHR` 包成 `new VkSurfaceKHR(...)` 返回；`device.cpp` 第 168-170 行又把返回值直接强转回 `VkSurfaceKHR` |
| 影响 | GLFW 路径下拿到的不是 Vulkan surface 句柄值，而是“指向句柄的地址”；后续 surface 查询和销毁都可能吃到错误句柄 |
| 判断 | 这是 ABI/句柄语义错误，不是代码风格问题 |

SDL 路径在 [src/infra/window/sdl_window.cpp](../../src/infra/window/sdl_window.cpp) 第 160-165 行返回的是句柄值，GLFW 路径却返回了堆对象地址。一个接口两套语义，会把窗口后端切换变成高风险操作。

### P0：四元数乘法公式写坏了

| 项 | 说明 |
| --- | --- |
| 位置 | [src/core/math/quat.hpp](../../src/core/math/quat.hpp) |
| 证据 | 第 117-128 行先写回 `w`，再用新 `w` 参与 `v` 计算 |
| 影响 | 旋转组合、插值、骨骼局部变换等只要依赖这个乘法，理论上都会被污染 |
| 判断 | 这是数学正确性问题，优先级应视为 P0 |

`multiply_inplace()` 和 `left_multiply_inplace()` 都有同类问题。当前仓库里还没有看到专门覆盖 quaternion 乘法正确性的测试，这意味着这个问题可以长期潜伏。

### P0：向量哈希存在未定义行为

| 项 | 说明 |
| --- | --- |
| 位置 | [src/core/math/vec.hpp](../../src/core/math/vec.hpp) |
| 证据 | 第 101-103 行把 `float` 地址 `reinterpret_cast` 成 `const long long*` 再读 |
| 影响 | 违反严格别名和对象大小假设；在不同编译器/优化级别/平台上可能出现错 hash，甚至越界读取 |
| 判断 | 这是底层工具实现的硬缺陷，应尽快替换为 `std::bit_cast` 或按元素 hash |

这类问题看起来小，但它位于顶点去重、容器 key 等基础层，属于“出错时很难排查”的问题。

### P1：SDL 窗口 `updateSize()` 对外是空实现

| 项 | 说明 |
| --- | --- |
| 位置 | [src/infra/window/sdl_window.cpp](../../src/infra/window/sdl_window.cpp) |
| 证据 | 第 177 行公开接口 `Window::updateSize(...)` 是空函数；内部 `Impl::updateSize(...)` 在前面却实现了完整逻辑 |
| 影响 | 任何依赖抽象接口拉取 resize/minimize 结果的调用方都会得到假能力 |
| 判断 | 这是典型的“内部实现写了，但没接出去”问题 |

### P1：`TextureLoader` 重复加载会泄漏旧图像

| 项 | 说明 |
| --- | --- |
| 位置 | [src/infra/texture_loader/texture_loader.cpp](../../src/infra/texture_loader/texture_loader.cpp) |
| 证据 | 第 14-18 行只在析构释放 `data`；第 27-39 行的 `load()` 覆盖 `pImpl->data` 前不释放旧值 |
| 影响 | 编辑器预览、材质热切换或复用 loader 的流程里会稳定泄漏 |
| 判断 | 修复成本低，收益高，适合尽快处理 |

### P1：设备筛选逻辑把“可用”与“独显优先”混在一起

| 项 | 说明 |
| --- | --- |
| 位置 | [src/backend/vulkan/details/device.cpp](../../src/backend/vulkan/details/device.cpp) |
| 证据 | 第 353-367 行 `isDeviceSuitable()` 直接要求 `VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU`；第 381-396 行的注释却写成“优先独显，找不到再兜底” |
| 影响 | 没有独显的机器、虚拟机和部分 CI 环境会直接初始化失败 |
| 判断 | 这是选择策略写错，不是单纯偏好问题 |

### P1：项目自己的 C++ 规范还没有真正收口

| 项 | 说明 |
| --- | --- |
| 位置 | [src/backend/vulkan/vulkan_renderer.hpp](../../src/backend/vulkan/vulkan_renderer.hpp)、[src/core/scene/object.hpp](../../src/core/scene/object.hpp)、[src/infra/window/window.hpp](../../src/infra/window/window.hpp)、[src/infra/gui/gui.hpp](../../src/infra/gui/gui.hpp) |
| 证据 | `VulkanRendererImpl* p_impl`、`Scene *m_scene`、多个 `Impl *pImpl` 仍在手工 `new/delete` |
| 影响 | 和 `openspec/specs/cpp-style-guide/spec.md` 的所有权表达要求不一致；后续 review 和重构成本会上升 |
| 判断 | 这类问题短期未必立刻崩，但会持续拉低可维护性 |

这里要区分两类情况：

- 和 C API 交互时使用裸指针，本身可以接受。
- 把对象所有权或长期依赖关系保存在裸指针成员里，就已经偏离了项目自己定义的规范。

### P1：测试与运行环境仍有明显收口缺口

| 项 | 说明 |
| --- | --- |
| 位置 | [src/test/integration/test_shader_compiler.cpp](../../src/test/integration/test_shader_compiler.cpp)、`build/src/test/test_imgui_overlay` 的运行时依赖 |
| 证据 | `test_shader_compiler` 默认假设 shader 目录在当前工作目录下；从 `build/` 直接运行会失败，需要手动传 `../shaders/glsl`。`test_imgui_overlay` 在当前环境里无法加载 `libSDL3.so.0` |
| 影响 | 测试虽然存在，但可移植运行体验不稳定；CI 和新开发机更容易踩环境坑 |
| 判断 | 这是工程收口问题，不全是代码逻辑 bug，但会直接影响团队协作效率 |

## 一个需要特别说明的点

`notes/ai-scanned/` 下已经有一轮 2026-04-17 的扫描结果，其中有些结论今天仍然成立，但也有部分已经过期。最典型的是 GUI：

- 旧扫描认为 GUI “未接线且启用即坏”。
- 当前代码里，GUI 已经在 [src/backend/vulkan/vulkan_renderer.cpp](../../src/backend/vulkan/vulkan_renderer.cpp) 第 131-144 行初始化，并在第 299-319 行进入 draw loop。
- [src/infra/gui/imgui_gui.cpp](../../src/infra/gui/imgui_gui.cpp) 第 35-63 行也已经补上 descriptor pool 创建。

也就是说，旧扫描文档现在更适合作为“历史问题记录”，不适合作为当前状态的最终结论。

## 当前优点

问题不少，但也有几件事值得明确记录，因为这决定了后续规划不应该推倒重来。

| 优点 | 说明 |
| --- | --- |
| 主路径真实存在 | 不是只有 spec，没有实现；很多接口已经有上下游 |
| 文档基础较好 | `openspec/specs/`、`notes/subsystems/`、`notes/concepts/` 提供了足够多的上下文 |
| SceneNode 校验思路是对的 | 把 mesh/material/shader/skeleton 的约束尽量前移，有利于后端简化 |
| Pipeline identity 已经成形 | `PipelineKey`、`PipelineBuildDesc`、预热机制都已进入真实路径 |
| demo 与测试都能说明演进方向 | `scene_viewer` 和一批 integration tests 让架构不是纯纸面设计 |

## 验证记录

我在 2026-04-19 本地工作区做了代表性构建与测试，结果如下：

| 项目 | 结果 | 备注 |
| --- | --- | --- |
| `cmake --build build --target test_engine_loop test_scene_node_validation test_shader_compiler test_gltf_loader test_imgui_overlay -j4` | 通过 | 目标可以成功构建 |
| `build/src/test/test_engine_loop` | 通过 | 主循环 CPU 逻辑正常 |
| `build/src/test/test_scene_node_validation` | 通过 | SceneNode 校验链路正常 |
| `build/src/test/test_gltf_loader` | 通过 | glTF 资产读取链路正常 |
| `build/src/test/test_shader_compiler` | 直接运行失败 | 默认 shader 路径假设不对 |
| `build/src/test/test_shader_compiler ../shaders/glsl` | 通过 | 说明功能本身正常，问题在测试入口假设 |
| `build/src/test/test_imgui_overlay` | 失败 | 当前环境缺少 `libSDL3.so.0`，属于运行时依赖未收口 |

## 后续规划

我建议把后续工作拆成三个阶段，而不是同时大修全部模块。

### 第一阶段：先清掉会误导后续开发的硬问题

目标：让“切后端、跑测试、做数学/资源扩展”不再踩基础坑。

1. 统一 `Window::createGraphicsHandle()` 契约，修正 GLFW surface 句柄问题。
2. 修正 `QuatT` 乘法实现，并补 quaternion 正确性测试。
3. 替换 `Vec` 浮点 hash 的未定义行为实现。
4. 修正 SDL `updateSize()` 空实现。
5. 在 `TextureLoader::load()` 前释放旧图像。

### 第二阶段：把规范真正落实到现有代码

目标：减少“架构写的是一套，代码又是另一套”的维护成本。

1. 把 `VulkanRenderer`、`Window`、`Gui` 等 PImpl 改成 `std::unique_ptr`。
2. 重新整理 `SceneNode -> Scene` 的回连模型，至少去掉裸指针持有。
3. 清理 `infra` 内部 `namespace infra` 与 `namespace LX_infra` 的混用，统一命名边界。
4. 复盘 `RenderableSubMesh` 是否仍是长期 API；若保留，应补齐与 `SceneNode` 对齐的约束语义。

### 第三阶段：把“能跑”提升到“可持续交付”

目标：让 demo、测试、文档和运行环境形成闭环。

1. 让测试可执行文件不再依赖隐式工作目录。
2. 收口 SDL/GLFW 运行时依赖策略，至少让本地和 CI 能稳定启动图形测试。
3. 同步更新 `notes/ai-scanned/` 与 `notes/subsystems/`，避免旧结论继续误导。
4. 在 roadmap 里把“功能开发”与“工程收口”分两条线跟踪，不要继续混写。

## 建议的近期优先级

如果只做一轮短冲刺，我建议顺序是：

1. 窗口句柄契约修正。
2. quaternion 与 vector hash 修正。
3. SDL `updateSize()` 与 `TextureLoader` 泄漏修正。
4. 测试入口与运行时依赖收口。
5. PImpl / 裸指针规范整改。

这样做的好处是，先消掉“会把结果做错”的问题，再处理“会让团队慢下来”的问题。

## 总结

当前仓库最有价值的地方，不是“功能多”，而是很多关键概念已经开始形成真实主路径；当前仓库最大的风险，也不是“功能缺”，而是几处基础边界没有完全做对。换句话说，我们现在不需要推倒重做，更需要的是一轮扎实的收口：把句柄契约、数学正确性、资源生命周期、测试可运行性这些底层问题先做实，然后再继续往更复杂的渲染能力推进。
