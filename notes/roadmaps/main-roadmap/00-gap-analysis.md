# 00 · Gap Analysis

> 从当前代码库到 AI-Native 小型游戏引擎的能力差距盘点。整份 roadmap 的起点。

## 小型游戏引擎最低定义

- **开发者能脱离引擎源码写游戏**：稳定接口，游戏代码不改引擎内部
- **资产以外部文件形式存在**：贴图 / 网格 / 动画 / 场景 / shader 皆可被工具产出，引擎运行期加载
- **运行期可交互**：键鼠 / 手柄至少一种，能驱动相机与游戏逻辑
- **世界有物理与动画**：碰撞 / 射线 / 刚体响应；骨骼动画播放
- **能发布**：可打包成“没装 Vulkan SDK 的机器上双击能跑”的二进制

之上每一项（编辑器 / 脚本 / 多线程 / 网络）都是锦上添花。

## 当前已落地能力（按子系统）

### 渲染 / GPU

| 能力 | 位置 |
|------|------|
| Vulkan 设备 / 队列 / 命令缓冲 | `src/backend/vulkan/details/device.*` / `commands/*` |
| Swapchain / RenderPass / Framebuffer | `src/backend/vulkan/details/render_objects/*` |
| Pipeline 构建 + 缓存 + 预加载 | `src/backend/vulkan/details/pipelines/pipeline_cache.*` + `src/core/pipeline/pipeline_build_desc.*` + `pipeline_key.*` |
| Shader 运行期编译 GLSL → SPIR-V | `src/infra/shader_compiler/shader_compiler.*`（shaderc） |
| SPIR-V 反射（含 UBO member / vertex input） | `src/infra/shader_compiler/shader_reflector.*`（SPIRV-Cross） |
| `CompiledShader` | `src/infra/shader_compiler/compiled_shader.*` |
| 材质模板 / 实例 | `src/core/asset/material_template.hpp` + `material_instance.*` |
| Canonical material binding + 多 buffer slot | `MaterialTemplate::rebuildMaterialInterface()` + `MaterialInstance::m_parameterBuffersByName` |
| Shader binding ownership | `src/core/asset/shader_binding_ownership.hpp`（保留 `{CameraUBO, LightUBO, Bones}`） |
| 通用 `.material` YAML loader | `src/infra/material_loader/generic_material_loader.*` |
| `PipelineKey` 结构化身份 | `src/core/pipeline/pipeline_key.*` |
| `FrameGraph` + `FramePass` | `src/core/frame_graph/*` |
| `RenderQueue` + pass/target 过滤 + `VisibilityLayerMask` 交集过滤 | `src/core/frame_graph/render_queue.*` |
| Pass 常量 | `Pass_Forward` / `Pass_Deferred` / `Pass_Shadow`（`src/core/frame_graph/pass.hpp`） |
| `RenderTarget` + `ImageFormat` | `src/core/frame_graph/render_target.*` + `src/core/rhi/image_format.hpp` |
| `IGpuResource` + Dirty 同步通道 | `src/core/rhi/gpu_resource.hpp` + backend `resource_manager.*` |
| `PerDrawData`（128B push constant ABI） | `src/core/scene/object.hpp` |
| OBJ / GLTF mesh 加载 | `src/infra/mesh_loader/*` |
| Texture 加载（stb_image） + placeholder `white` / `black` / `normal` | `src/infra/texture_loader/*` |
| `Skeleton` 资源（骨骼矩阵 UBO） | `src/core/asset/skeleton.hpp` |
| 非 BlinnPhong 材质示例 | `assets/materials/pbr_gold.material` + `assets/shaders/glsl/pbr.{vert,frag}` |
| ImGui overlay + debug panel helper | `src/infra/gui/gui.hpp` + `imgui_gui.cpp` + `debug_ui.*` |

### Runtime / 基础层

| 能力 | 位置 |
|------|------|
| `EngineLoop`（`initialize` / `startScene` / `tickFrame` / `run` / `stop` / `requestSceneRebuild` / `setUpdateHook`） | `src/core/gpu/engine_loop.*` |
| `Clock`（tick / deltaTime / smoothedDeltaTime） | `src/core/time/clock.*` |
| 输入抽象 `IInputState` + `KeyCode` + `MouseButton` | `src/core/input/*` |
| `DummyInputState` / `MockInputState`（测试辅助） | `src/core/input/dummy_input_state.hpp` / `mock_input_state.hpp` |
| SDL3 输入实现 | `src/infra/window/sdl3_input_state.*` |
| Window（SDL3 + GLFW） | `src/infra/window/*` |
| Orbit / FreeFly 相机控制器 | `src/core/scene/orbit_camera_controller.*` + `freefly_camera_controller.*` |
| `Camera::cullingMask` + `SceneNode::visibilityLayerMask` | `src/core/scene/camera.hpp` + `object.hpp` |
| 资产目录 + `cdToWhereAssetsExist` | `src/core/utils/filesystem_tools.*` |
| `GlobalStringTable` / `StringID` / `TypeTag` compose | `src/core/utils/string_table.*` |
| `expSetEnvVK` 早期调用抑制 Vulkan implicit layer 日志 | `src/core/utils/env.*` |

### 工程 / 构建

| 能力 | 位置 |
|------|------|
| CMake + Ninja 跨平台构建 | `CMakeLists.txt` + `src/*/CMakeLists.txt` |
| 集成测试框架 | `src/test/integration/*` |
| `demo_scene_viewer`（正式交互 demo） | `src/demos/scene_viewer/*` |
| spec / requirement 流程 | `openspec/specs/*` + `notes/requirements/*` |

### 场景 / 游戏向

| 能力 | 状态 | 备注 |
|------|------|------|
| `SceneNode` 统一 renderable 模型 | ✅ | `IRenderable` 唯一实现；pass-aware validated cache |
| 相机（透视 / 正交） | ✅ | `core/scene/camera.hpp` |
| Camera visibility layer / culling mask | ✅ | `Camera::cullingMask` + `SceneNode::visibilityLayerMask` + `RenderQueue` 交集过滤 |
| 方向光 | ✅ | `core/scene/light.hpp`（`DirectionalLight` + `DirectionalLightData`） |
| 点光 / 聚光 | ❌ | `LightBase` 抽象已就位，类型未实现 |
| Skeleton 资源存在 | ✅ | 但没有动画播放器 |

## 关键缺口

按“离可玩小游戏最近”的顺序。

### A. 场景层缺乏 transform 层级

`Scene` 持有扁平 `std::vector<IRenderableSharedPtr>`。世界矩阵通过 `PerDrawData.model` 外部推送，没有父子关系、没有本地/世界坐标概念、没有层级脏标记。

**影响**：不能写“武器挂在手上” / “摄像机跟随玩家”这类基础游戏逻辑。

### B. 输入层薄

`IInputState` 只暴露状态量（键 / 鼠标按下），不暴露边沿事件。没有 action mapping，没有 gamepad。

### C. 时间/游戏循环覆盖不全

`Clock` 有 deltaTime / smoothedDeltaTime。**缺**：fixed step（物理用）/ 帧率 cap / 暂停 / 时间缩放。

### D. 没有资产管线

资源加载靠硬编码路径 + `cdToWhereAssetsExist` 启动期 cwd 校准。发布时资产如何被找到没有设计。

- 没有 asset GUID / handle
- 没有 serialize / deserialize
- 场景全由 C++ 代码构造，没有“运行期保存 / 加载”

### E. 没有动画播放器

`SkeletonUBO` 已是 GPU 可消费形态，但没人往里写动画数据。没有 `AnimationClip` 资源，没有 `AnimationPlayer` 组件。

### F. 没有物理

没有刚体、碰撞、射线、角色控制器。

### G. 没有 gameplay 层

没有 component 生命周期（awake / start / update / destroy）、系统调度、脚本语言、事件总线。场景中物体加行为只能在 main 的 while 里加 if-else。

### H. 没有音频

“按键播放 wav”都做不到。

### I. 没有游戏内 UI

ImGui 是开发者 debug UI，不是玩家 UI（没 theming / animation / retained mode）。

### J. 没有编辑器

场景全是代码写的。加新物体需重新编译。

### K. 没有 profiler 集成

靠 `std::cerr` + `LX_RENDER_DEBUG=1`。没有 CPU / GPU 时间线、帧内 event、allocator 统计。

### L. 没有打包 / 发布管线

依赖 `assets/shaders/glsl/` 源文件（运行期编译）+ cwd 启发式查找资源。发布给第三方不可行。

### M. 渲染深度未完全解锁

即使只是“画得好看”，还缺：

- Shadow mapping / CSM
- IBL / 环境光
- HDR + tone mapping pass（目前 tone map 在 shader 里手写，非独立 pass）
- Bloom / FXAA
- Frustum culling（`RenderQueue::buildFromScene` 对所有 renderable 都生成 item）
- PointLight / SpotLight 具体实现

## AI-Native 维度的缺口

与 A–M 正交，**不是“传统缺口打完之后再做”**，而是从 Phase 1 就要纳入考量。

### N. 没有 Web 后端

当前只有 Vulkan。AI 生态天然生长在浏览器里：

- 引擎渲染画面无法嵌入 web 页面
- LLM 无法通过 WASM 试跑生成代码
- 未来 web 编辑器无驱动

方向：引入 WebGPU（via Dawn 或原生）或 WebGL2 作为第二后端，共享 `core/` 抽象。

### O. 没有文本内省 / 结构化 dump

AI 最擅长读文本。当前引擎状态只能通过看像素 / GDB / `std::cerr`。缺失：

- Scene tree 无 `dumpScene(format) → string`
- 组件字段无 JSON schema
- 渲染状态无 `describePipeline(key) → string`
- 资产目录无 `listAssets(filter) → AssetSummary[]`
- 错误多为 `"xxx is null"`，缺乏完整上下文路径

### P. 脚本语言对 AI 不友好

若选 Lua，AI 的掌控力远不如 TypeScript：TS 训练语料大、有静态类型、能直接复用 Vue/React。从一开始把 Phase 6 脚本层定在 TypeScript 上。

### Q. UI 系统对 AI 不友好

LLM 对 HTML + CSS + Vue/React 的掌控力远超任何专有游戏 UI 方案（前端代码占 LLM 训练集 50%+）。UI 层用迷你 HTML+JS 容器 + Vue 子集，而不是重新发明 retained-mode UI。

### R. 没有 MCP 接口

没有 RPC / WebSocket / 命名管道，没有自描述 tool schema，没有权限 / 沙箱机制。

方向：引擎默认启一个 MCP server（stdio + WebSocket 两种 transport），把内部能力封成 MCP tools。

### S. 没有 CLI 交互模式

`src/main.cpp` 只是 `expSetEnvVK()` + 入口提示，不是通用 CLI。缺：

- 纯 headless 模式（不创建窗口，仍能加载场景、执行命令）
- REPL 风格 CLI：`engine-cli --scene foo.json --chat`
- 本地 agent runtime，可连接 OpenAI / Anthropic / 本地模型
- 管道模式：`echo "dump scene" | engine-cli`

### T. 没有 AI 资产生成能力

资产全是“人类艺术家做好 → 导入”。AI-Native 引擎应一句话生成贴图 / 3D / 动画 / 角色；支持 NeRF / 3DGS。需要“资产生成 pipeline 框架”协调远程模型。

### U. 没有 Agent 运行时

即使接了 MCP，完整形态还需**长驻 agent**：监听 CLI 输入 / 持有历史对话 / 调 MCP tools / 产出结构化响应 / 支持 skill 扩展。

没 agent runtime：引擎是被动 MCP server。有 agent runtime：引擎是主动 AI 协作者。

## 工作量粗估

假设**一个熟悉 Vulkan + C++ 的开发者全职**：

| 阶段 | 乐观 | 悲观 | 备注 |
|------|------|------|------|
| Phase 1 渲染深度 + Web 后端 | 6 周 | 12 周 | WebGPU 后端多算 2–4 周 |
| Phase 2 基础层 + 文本内省 | 3 周 | 5 周 | dump 族 API 加 1 周 |
| Phase 3 资产管线 | 3 周 | 6 周 | 序列化格式选型会反复 |
| Phase 4 动画 | 3 周 | 6 周 | 状态机 + 混合吃 50% 时间 |
| Phase 5 物理 | 2 周 | 4 周 | 选 Jolt 集成快；自写翻倍 |
| Phase 6 Gameplay (TS) | 4 周 | 8 周 | TS 集成 + bindings 比 Lua 重 |
| Phase 7 音频 | 1 周 | 2 周 | 纯音频部分较小 |
| Phase 8 Vue UI 容器 | 4 周 | 8 周 | HTML/CSS 子集 + reactivity |
| Phase 9 Web 编辑器 | 6 周 | 12 周 | 前端从零写 |
| Phase 10 MCP + Agent + CLI | 6 周 | 12 周 | 首次引入大块新能力 |
| Phase 11 AI 资产生成 | 6 周 | 14 周 | 模型服务集成成本高 |
| Phase 12 打包发布 + WASM | 2 周 | 4 周 | Emscripten 构建链 |

**总计**：46–93 人周 ≈ 10 个月 – 1 年 10 月。

## 推进节奏建议

以 **一年交付可玩的 AI-Native 原型** 为目标：

### 前 3 个月 · MVP

- Phase 1 前半（Vulkan 部分：shadow / IBL / post）
- Phase 2 全部（transform / input / time / **文本内省**）
- Phase 3 前半（asset GUID + 最小序列化）
- Phase 6 基础（TS 脚本能跑）
- **Phase 10 最小子集**：MCP server 暴露“读 scene / 改 transform / 加组件”3 个 tool

**产物**：Claude Code 通过 MCP 连接引擎，对场景做“把 player 向左移两米”。

### 3–6 月 · Alpha

- Phase 1 后半（WebGPU / WebGL2 后端）
- Phase 4 + Phase 5
- Phase 8 前半（HTML 容器 + 最小 Vue reactivity）
- Phase 9 前半（Web editor 显示场景树 + inspector）
- Phase 10 扩展 tool 集（20+ skill）

**产物**：浏览器内活场景，agent 通过对话修改并看到结果。

### 6–9 月 · Beta

- Phase 7 音频
- Phase 8 完成
- Phase 9 完成
- **Phase 11 核心**（贴图生成 + 3D 模型生成）

**产物**：一句话“给我一只木桶”物体出现在场景里并能物理交互。

### 9–12 月 · 发布

- Phase 11 扩展（动画 / NeRF / 3DGS）
- Phase 12 全部（含 WASM）

**产物**：Windows / Linux / Web 三目标可分发，AI 协作流端到端可用。

## 下一步

读 [Phase 1](phase-1-rendering-depth.md) 或 [Phase 2](phase-2-foundation-layer.md) 挑一个动手。两路径前置都已满足，可并行。

AI-Native 核心变化最关心者：跳到 [Phase 10 · MCP + Agent + CLI](phase-10-ai-agent-mcp.md)。
