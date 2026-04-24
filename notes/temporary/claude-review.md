# Claude Review — 项目现状与下一步规划（2026-04-24 复核版）

日期：2026-04-24
作者：Codex（基于 `HEAD=82267d3` 与当前 dirty worktree 复核；原稿作者：Claude）
适用范围：`src/core`、`src/infra`、`src/backend`、`src/test`、`src/demos`、`openspec/`、`notes/`

---

## 0. 一页摘要

项目已经是一个"结构完整、主路径可跑通"的 Vulkan 小引擎：

- 三层分层（core / infra / backend）边界清晰；
- Scene → FrameGraph → RenderQueue → PipelineBuildDesc → PipelineCache 的渲染管线身份链路基本打通；
- demo `scene_viewer` 把 window / renderer / engine loop / input / camera / UI overlay 串起来可以运行；
- `openspec/specs/` 下当前有 34 份 spec、`notes/subsystems/` 有 13 份设计文档，工程流程规范。

但是**仍存在若干高优先级缺陷**至今未修，其中一部分是 2026-04-17 的 `notes/ai-scanned/` 报告里点过名、当前代码里仍原样存在的硬错误：四元数乘法公式写反、浮点向量哈希走 UB、SDL 窗口 `updateSize` 空壳、GLFW Surface 句柄泄漏与契约漂移、`VulkanRenderer` 的 pImpl 仍是裸 `new/delete`、`isDeviceSuitable()` 仍把集显排除在外。

另外，测试框架仍然薄弱（没有 CTest 注册、没有单元测试框架、数学层零覆盖）。不过原稿里“REQ-034 的 R2–R10 长期未动”这句已经过时：`shader binding ownership` 已经进入当前主规格与代码，材质系统也正在通过活跃 change `openspec/changes/canonical-material-parameters/` 继续收敛，不能再按“完全没启动”来判断。

**下一步建议**分成 4 条线：（1）先把仍在的硬错误修掉并补最小测试闭环；（2）继续推进 REQ-034 里的 R2 与 R4–R7；（3）把正在进行中的 canonical material parameter change 收口并归档；（4）长期债继续聚焦 GPU 资源句柄契约、`VulkanRendererImpl` 封装、裸指针 pImpl 统一迁到 `std::unique_ptr`。

---

## 1. 架构总览

### 1.1 分层与代码规模

| 层 | 目录 | 非 external LOC | 职责 |
|---|---|---|---|
| core | `src/core/` | 1825 | 接口、数学、资源类型、场景图、FrameGraph / PipelineKey |
| infra | `src/infra/` | 2424 | window / gui / mesh loader / texture loader / shader compiler + reflector |
| backend | `src/backend/vulkan/` | 3235 | Vulkan 设备、资源、descriptor、pipeline、command buffer、swapchain |
| demos | `src/demos/` | 534 | `scene_viewer`：helmet + 地面 + orbit/freefly 相机 + ImGui overlay |
| tests | `src/test/` | 5496 | 当前有 29 个 `test_*.cpp`，其中大部分是 integration test，另有 app-style `test_render_triangle` |

### 1.2 主渲染路径

```
Scene
  └── SceneNode (mesh + material + skeleton?) + Camera + Light
FrameGraph
  └── FramePass { name, RenderTarget, RenderQueue }
        └── RenderQueue::buildFromScene(scene, pass, target)
              ├── filter renderables by supportsPass(pass)
              ├── merge scene.getSceneLevelResources(pass, target)
              └── emit RenderingItem[]  with PipelineKey
VulkanResourceManager
  ├── syncResource(...)   （vertex / index / UBO / combined sampler）
  └── PipelineCache::preload(collectAllPipelineBuildDescs)
VulkanRenderer::draw()
  └── for each pass, item: bindPipeline / bindResources / drawItem
```

身份层（`PipelineKey` / `PipelineBuildDesc`）和执行层（`VulkanPipeline` 缓存）是分离的，这一点做得干净。

### 1.3 已经做好的部分

- `GlobalStringTable` + `StringID` + `compose/decompose` 稳定，覆盖了 pass / 材质 / pipeline 身份。
- SPIR-V 反射（含 UBO 成员）与反射驱动的 `MaterialInstance::setVec3` 等写入路径已打通。
- `PipelineCache::preload` 与 runtime miss 的 warn 分离，preload 抑制警告策略干净。
- `EngineLoop` 把 window / renderer / scene / clock / updateHook 粘合起来，demo 里用得很自然。
- 相机体系（`OrbitCameraController` + `FreeFlyCameraController` + `F2` 切换）+ `IInputState` + `Sdl3InputState` 闭环。
- `ImageFormat` / `RenderTarget` 使用"后端 format ↔ 抽象 format"双向映射，初始化时一次性回填。

---

## 2. 高优先级问题（block 发布或正确性）

这一节里的问题大部分已在 2026-04-17 的 `notes/ai-scanned/` 三份报告里点名，**截至 2026-04-24 复核仍未修复**。复述它们，不是因为新发现，而是因为它们应该在下一轮就被清掉。

### P0-1 · 四元数乘法公式错误 · `src/core/math/quat.hpp:117-129`

```cpp
QuatT &multiply_inplace(const QuatT &o) {
  auto oldW = w;
  auto oldV = v;
  w = oldW * o.w - oldV.dot(o.v);
  v = oldV.cross(o.v) + o.v * w + oldV * o.w;  // ← 这里 w 是新 w
  return *this;
}
```

正确公式应当使用 `oldW`：`v = oldV.cross(o.v) + o.v * oldW + oldV * o.w`。`left_multiply_inplace` 同病。`operator*`、`operator*=` 都走这两个函数，任何涉及旋转组合 / 插值的路径都会被污染。

**测试现状**：`src/test/integration/` 中**没有任何 `quat` / `math` 单元测试**。即使修好也没有测试兜底。

**建议**：修公式 + 新建 `test_math.cpp`（或起码 `test_quaternion.cpp`），覆盖 `q1 * q2`、共轭、单位四元数旋转向量三个性质。

### P0-2 · 浮点向量哈希依赖 UB · `src/core/math/vec.hpp:95-110`

```cpp
if constexpr (std::is_floating_point<T>::value) {
  hi = std::hash<long long>()(
      *reinterpret_cast<const long long *>(&v[i]));
}
```

`T = float` 时这里用 8 字节视图读 4 字节对象 —— 违反严格别名、越界访问。换对齐 / 优化级别就可能出问题。

**建议**：用 `std::bit_cast<uint32_t>(v[i])`（float）/ `std::bit_cast<uint64_t>(v[i])`（double）替换。

### P0-3 · SDL `Window::updateSize()` 空实现 · `src/infra/window/sdl_window.cpp:177`

```cpp
void Window::updateSize(bool *closed, int *width, int *height) {}
```

`Impl::updateSize` 有完整逻辑（等待最小化恢复、处理 `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`），但对外公开的接口是空的。调用方（包括 `EngineLoop::tickFrame`）拿不到 resize / minimize 结果。GLFW 路径正常转发，两个后端行为已漂移。

**建议**：一行 `pImpl->updateSize(closed, width, height);`。

### P0-4 · GLFW Surface 句柄契约错误 · `src/infra/window/glfw_window.cpp:112`

```cpp
return new VkSurfaceKHR(getVulkanSurface(*(VkInstance *)graphicsInstance));
```

GLFW 后端返回的是"指向 VkSurfaceKHR 的堆指针"，设备层 `device.cpp` 把这个指针值直接当 `VkSurfaceKHR` 用，而 SDL 后端返回的是句柄值。两个后端同名接口语义已经不一致，加上 `destroyGraphicsHandle()` 是空实现，这块堆内存也漏了。

**建议**：统一 `createGraphicsHandle()` 契约，两个后端都返回"句柄值"；一并补齐对应的 destroy 逻辑。

### P0-5 · `VulkanRenderer` pImpl 仍用裸 `new/delete` · `src/backend/vulkan/vulkan_renderer.{hpp,cpp}`

```cpp
class VulkanRenderer : public gpu::Renderer {
  ...
  VulkanRendererImpl* p_impl = nullptr;   // hpp:32
};
VulkanRenderer::VulkanRenderer(Token) : p_impl(nullptr) {
  p_impl = new VulkanRendererImpl();      // cpp:421-422
}
VulkanRenderer::~VulkanRenderer() { delete p_impl; }  // cpp:425
```

和 `openspec/specs/cpp-style-guide/spec.md` 的"禁止裸指针 + 禁止手工 `new/delete`"规则正面冲突。同类问题还在 `infra/window/{sdl,glfw}_window.cpp`、`infra/gui/imgui_gui.cpp`、`infra/{mesh,texture}_loader/*.cpp`、`infra/mesh_loader/{obj,gltf}_mesh_loader.cpp`。

**建议**：统一改成 `std::unique_ptr<Impl> pImpl`。PImpl 的前向声明 + `unique_ptr` 需要在 .cpp 里定义析构函数，参考 Herb Sutter 的 GotW #100/101。

### P0-6 · `isDeviceSuitable()` 排除集显

`src/backend/vulkan/details/device.cpp` 里 `isDeviceSuitable()` 仍要求 `VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU`，注释却说"优先独显、兜底任意可用 GPU"。集显笔记本、Linux VM、CI runner 全部会在初始化阶段失败。

**建议**：把"是否可用"（队列族 / 扩展 / swapchain 可用性）和"偏好排序"（独显优先）拆成两个步骤；`isDeviceSuitable` 只负责前者。

### 已从旧清单移除 · ImGui descriptor pool 初始化错误已修

原稿的 P0-7 已经过时。当前 `src/infra/gui/imgui_gui.cpp` 会在 `Gui::init()` 中显式创建 ImGui 自己的 `VkDescriptorPool`，并把 `initInfo.DescriptorPool` 设为该 pool；`DescriptorPoolSize = 0` 在这一模式下是合法的。这个问题不应再继续算作未修 P0。

---

## 3. 中优先级问题（影响可维护性 / 可测试性）

### M-1 · `std::terminate()` 被大量用作"编程错误"的处理

项目代码（非 third-party）里 7 处调用 `std::terminate()`：
`src/core/scene/scene.hpp:77`、
`src/core/scene/object.cpp:65,196,201`、
`src/core/asset/material_instance.cpp:49,57`、
`src/infra/material_loader/generic_material_loader.cpp:28`。

语义上都是"触发了不可能条件（重复节点名 / undefined pass / missing template）"。`terminate` 比 `assert` 更硬，又不可被测试框架捕获。集成测试里想断言"这种输入应当 fail fast" 做不到。

**建议**：统一改用 `throw std::logic_error(ctx)`（`[[noreturn]]` helper），测试用 `try/catch` 或专门的死亡断言验证。保留 `terminate` 仅给真正不可恢复的 Vulkan 致命错误。

### M-2 · 测试基础设施太薄

- 没有 `enable_testing()` / `add_test()`，`ctest` 无信号。
- 没有单元测试框架（没 GoogleTest、没 Catch2），用的是手写 `EXPECT` 宏 + `failures` 计数器 + `return failures != 0 ? 1 : 0`。
- 测试只靠 `ninja BuildTest` 编译通过，没有"跑完 28 个 exe 汇总结果"的入口。
- 数学层（`quat` / `vec` / `mat`）零覆盖。
- 没有 ASan / UBSan 构建开关，P0-2 的浮点 UB 在 sanitizer 下一跑就暴。

**建议分两步**：
1. 最小代价：在 `src/test/CMakeLists.txt` 里对每个 exe 追加 `add_test(NAME ${TEST_EXE} COMMAND ${TEST_EXE})`，并在根 CMake `enable_testing()`；补 `cmake -DLX_ENABLE_SANITIZERS=ON` 选项。
2. 中期：切 GoogleTest（或 Catch2，header-only 更轻）；保留现有 exe 作为"app-style" 集成测试不变。

### M-3 · `VulkanResourceManager` 用 `this` 指针做 GPU 资源缓存键

`src/backend/vulkan/details/resource_manager.cpp:68` 里 `void *handle = cpuRes->getResourceHandle();` 返回的就是 `this`。`m_gpuResources` 是 `unordered_map<void*, …>`。

两个风险：
1. **地址复用**：如果 CPU 资源析构后，下次分配命中同地址，旧 GPU 条目若未被 `collectGarbage` 清掉会错误匹配。当前 `collectGarbage` 每次 sync 完都清理，这暂时保护了一致性，但这个安全带与 active-set 机制耦合很紧。
2. **跨帧颠簸**：`collectGarbage()` 只要 CPU 资源没被本轮 sync 引用就销毁 GPU 条目，条件 draw / 暂时切关的 item 每帧销毁-重建。

**建议**：把 cache key 改为 `std::weak_ptr<IGpuResource>` 或给 `IGpuResource` 分配一个稳定的 64-bit ID（interning 层已有，可以复用）。同时考虑"保留 N 帧"的 GC 窗口。

### M-4 · `VulkanRendererImpl` 设计怪

- `VulkanRendererImpl : public gpu::Renderer` —— 但外层 `VulkanRenderer` 也继承 `gpu::Renderer`，两层虚继承其实只是外层重复一次转发。
- `VulkanRendererImpl` 的成员 `device` / `swapchain` / `resourceManager` / `scene` / `m_gui` 全部是 `public:`。
- `setDrawUiCallback` 只挂在 `VulkanRenderer`（具体类）上，demo 要同时持有 `RendererSharedPtr` 和 `shared_ptr<VulkanRenderer>`。
- `maxFramesInFlight = 3` 在 `initialize()` 和 `draw()` 两处重复写。

**建议**：把 `VulkanRendererImpl` 改成纯实现类（不继承 `gpu::Renderer`），成员全部 private；`maxFramesInFlight` 抽成成员常量或 `constexpr`；`setDrawUiCallback` 考虑提到 `gpu::Renderer` 基类（现在规模支持得起）。

### M-5 · `src/main.cpp` 空文件、`src/core/resources/` 空目录

原稿这里也已部分过时。当前 `src/main.cpp` 不再是纯空壳，而是一个只做 `expSetEnvVK()` 的极薄入口；`src/core/resources/` 目录也已经不存在。真正的问题现在变成：根 `CMakeLists.txt` 仍把这个极薄入口构建成 `Renderer` 可执行，但它依然不是项目的正式 demo / app 入口，和 `scene_viewer` 的角色关系仍不清晰。

**建议**：要么明确把 `Renderer` 定义成一个 bootstrap / env-probe 可执行并补文档，要么让顶层入口直接对齐 `scene_viewer`；不要继续保持"能构建、但不是实际产品入口"的暧昧状态。

### M-6 · Dead / 半接线代码

- `vulkan_renderer.cpp` 顶部的 `vkResultToString` / `debugLog` / `chooseSwapSurfaceFormat` 无调用点。
- `envEnabled` 在 `vulkan_renderer.cpp` 与 `command_buffer.cpp` 各有一份。
- swapchain.cpp 里 `chooseSwapPresentMode` TODO 一直没兑现，`oldSwapchain = VK_NULL_HANDLE` 也写了"可以优化 resize"的 TODO。

**建议**：清理未接线 helper；把环境变量读取集中到一个 `core/utils/env.hpp` 入口，已经有 `env.cpp` 可以继续扩。

---

## 4. 设计债务 / 演进方向

### D-1 · `SceneNode` 通过裸 `Scene*` 回连

`src/core/scene/object.hpp:121,141`。违反项目自己的 style guide（no raw pointers for object references），也没有 Scene 析构时反向清空的保险。对于 back-reference，一种常见做法是弱引用（`std::weak_ptr<Scene>`），前提是 Scene 本身走 `shared_ptr` 持有（当前正是这样）。

### D-2 · `RenderableSubMesh` legacy 路径未清理

REQ-034 R2 指的正是这一项。`SceneNode` 走完整的 `rebuildValidatedCache` 验证，`RenderableSubMesh` 走轻量 `getValidatedPassData`，两套强度不一的实现挂在同一个 `IRenderable` 抽象下。测试、示例、文档里混用。

### D-3 · Transform / 层级缺失

`00-gap-analysis.md` 的 A 项。`Scene` 只有扁平 `vector<IRenderableSharedPtr>`，PC_Draw.model 靠外部代码每帧塞。不能写"枪挂在手上"类逻辑。任何 gameplay 扩展都得先解这个。

### D-4 · 资产管线 & 发布

`00-gap-analysis.md` 的 D 项和 L 项。`cdToWhereAssetsExist()` 这种启动期 cwd 启发式显然不是发布版的路径。没有 asset GUID、没有 serialize、没有"把 shader SPIR-V 打包进可执行/pak"的步骤。

### D-5 · 材质系统的合同升级（REQ-034 R8–R10）

原稿这里已经过时。`global shader binding ownership` 已经进入当前规格与代码（`shader_binding_ownership.hpp`、`material_template.hpp` / `material_instance.cpp` 都已在用）；同时当前还有一个活跃 change `openspec/changes/canonical-material-parameters/`，其任务已经全部勾选，目标是把材质实例进一步收敛到单一 canonical 参数模型。

**更准确的判断**：R8 不再是“未开始”；R9 / R10 也不是“只有 spec 完全没落地”，而是正处于收口和归档前阶段。真正的风险在于这批改动仍停留在 dirty worktree，没有形成稳定提交与归档闭环。

---

## 5. 与需求 / roadmap 的对齐

### 5.1 REQ-034 的当前状态（来源：`notes/requirements/034-remaining-validated-backlog.md`）

| ID | 内容 | 状态 |
|---|---|---|
| R1 | REQ-019 `demo_scene_viewer` 人工验收 | 依赖显示环境，代码已就绪 |
| R2 | 删除 `RenderableSubMesh` legacy 路径 | 未做 |
| R3 | 真实的 non-blinnphong 自定义材质示例 | 未做 |
| R4 | Camera visibility layer / culling mask | 未开始 |
| R5 | SpotLight | 未开始 |
| R6 | IBL scene-level 资源 | 未开始 |
| R7 | Multi-light GPU 合同 | 未开始 |
| R8 | 全局 shader binding ownership 合同 | 已在主规格/代码中落地，不应再按“未开始”统计 |
| R9 | Pass-aware material binding interface | 已有前序实现，当前正被 canonical material parameter change 继续收敛 |
| R10 | 通用 material asset 格式 | 仍未形成稳定闭环，但前置材质合同已不再是零进展 |

### 5.2 roadmap 位置

`notes/roadmaps/main-roadmap/00-gap-analysis.md` 把项目距离"小型 AI-Native 游戏引擎"的差距切成 A–M（传统）和 N–U（AI 原生）。当前位置：**Phase 1 渲染深度 + Web 后端** 尚未启动，现在做的是 Phase 0 的收尾工作（demo / 材质 / 多光源合同）。

---

## 6. 下一步规划

**原则**：先正确、再好看、再扩功能。

### 6.1 Sprint A — 正确性与测试基建（1 周内）

目标：把 §2 的仍然成立的 P0 问题清零，补最小测试闭环。

1. 修四元数 `multiply_inplace` / `left_multiply_inplace` 公式（P0-1）
2. `std::bit_cast` 替换 `reinterpret_cast<const long long*>`（P0-2）
3. SDL `Window::updateSize` 转发到 `pImpl->updateSize`（P0-3）
4. 统一 GLFW / SDL 的 `createGraphicsHandle / destroyGraphicsHandle` 契约（P0-4）
5. `VulkanRenderer::p_impl` 改 `std::unique_ptr<VulkanRendererImpl>`，PImpl 头文件同时迁移（P0-5）
6. `isDeviceSuitable()` 拆"可用"与"偏好"两步（P0-6）
7. 在 `src/test/CMakeLists.txt` 里对每个 exe 追加 `add_test(...)`、根 CMake `enable_testing()`
8. 新建 `test_math.cpp`（quat + vec hash + mat 最小）

验证手段：`cmake --build build && ctest --output-on-failure`；可选 `-DLX_ENABLE_SANITIZERS=ON` 跑 ASan + UBSan。

### 6.2 Sprint B — REQ-034 收尾 + 小清理（1–2 周）

目标：把仍然真正活跃的需求数量继续往下收敛，并把材质系统当前 dirty worktree 收成可归档结果。

1. **R2** 删除 `RenderableSubMesh`，迁移测试与 demo 到 `SceneNode`
2. **R1** 在有显示环境的机器上跑 REQ-019 人工验收，归档
3. 明确顶层 `Renderer` 入口定位并清理未接线 helper（M-5、M-6）
4. `std::terminate` → `std::logic_error` 统一替换（M-1）
5. 把 7 份 pImpl 从裸指针改成 `std::unique_ptr`（P0-5 的后续 5 处）

### 6.3 Sprint C — 光照 / 材质系统升级（2–3 周）

依赖链现在应改写为：`R8` 已经基本具备，当前重点是把 `R9/R10` 对应的 canonical parameter change 正式收口；`R5 → R7` 仍成立；`R4` 与 `R6` 相对独立。

- 先把 `openspec/changes/canonical-material-parameters/` 对应实现整理成稳定提交，并更新相关 notes/spec
- 在 canonical 参数模型稳定后，再判断 `generic material asset` 还缺哪些真正未落地的部分
- **R4 visibility mask** + **R5 SpotLight**（可并行给一个开发者做）
- **R7 multi-light GPU 合同** 依赖 R5（至少两种光源）

### 6.4 Sprint D — 渲染深度启动（roadmap Phase 1）

前置：A/B/C 完成。

- Shadow mapping（一个方向光的 depth pass）
- Frustum culling（`RenderQueue::buildFromScene` 里加 AABB 测试）
- HDR + tone mapping 独立 pass（脱离 shader 手写）

### 6.5 长期

- §3 M-3：GPU 资源缓存 key 换 stable ID / weak_ptr
- §3 M-4：`VulkanRendererImpl` 封装整理
- §4 D-3：Scene transform 层级（roadmap Phase 2 的前提）
- §4 D-4：资产管线 + 发布流程（roadmap Phase 3 / Phase 12）

---

## 7. 作业建议

- §2 里仍未修的每个 P0 都适合开一个独立 openspec change（`2026-04-2x-fix-xxx`）跟踪，修完即归档。
- Sprint A 先；并行方向优先给 `R2` 和材质系统 canonical change 收口，而不是再新开更多材质合同分支。
- 在补上 `enable_testing()` / `add_test()` 之后，先跑一次 `ctest --output-on-failure`；若要抓 UB，优先给数学层与 `test_render_triangle` 加 sanitizer 构建。
- 建议把本文件和 `notes/ai-scanned/*.md` 一起作为下一轮计划的输入；两份视角互补，本文更偏向"做什么、按什么顺序"，扫描报告更偏向"哪里坏了"。
