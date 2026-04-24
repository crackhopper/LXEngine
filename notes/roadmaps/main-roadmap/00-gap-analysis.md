# 00 · Gap Analysis

> 从当前 `renderer-demo` 到"小型游戏引擎"的能力差距盘点。这是整份 roadmap 的起点。

## 小型游戏引擎的最低定义

本 roadmap 里所说的"小型游戏引擎"，指的是满足下列全部条件的系统：

1. **开发者能脱离引擎源码写游戏**：引擎提供稳定接口，游戏代码不必改引擎内部。
2. **资产以外部文件形态存在**：贴图 / 网格 / 动画 / 场景 / shader 都可以被工具链产出，引擎运行期加载。
3. **运行期可交互**：键盘 / 鼠标 / 手柄至少一种输入通路，能驱动相机和游戏逻辑。
4. **世界有物理与动画**：能做碰撞、射线、刚体响应；能播放骨骼动画。
5. **能发布**：有办法把工程打包成一个在"没装 Vulkan SDK 的机器上"双击能跑的二进制。

这是**最低线**。在此之上的每一项能力（编辑器、脚本、多线程、网络）都是锦上添花。

---

## 当前已有资产清单

按子系统整理，**避免在 roadmap 落地时重复造轮子**。

### 渲染 / GPU

| 能力 | 状态 | 位置 |
|------|------|------|
| Vulkan 设备 / 队列 / 命令缓冲 | ✅ | `src/backend/vulkan/details/` |
| Swapchain / RenderPass / Framebuffer | ✅ | 同上 |
| 顶点 / 索引 / UBO / 纹理 buffer | ✅ | `src/core/asset/`, backend |
| Pipeline 构建 + 缓存 + 预加载 | ✅ | `PipelineCache` / `PipelineBuildDesc` |
| Shader 运行期编译（GLSL→SPIR-V） | ✅ | `ShaderCompiler` (shaderc) |
| SPIR-V 反射（含 UBO member） | ✅ | `ShaderReflector` (SPIRV-Cross) |
| 模板-实例材质 | ✅ | `MaterialTemplate` / `MaterialInstance` |
| 反射驱动 std140 UBO 写入 | ✅ | `MaterialInstance::setVec3` 等 |
| `PipelineKey` 结构化身份 | ✅ | `pipeline_key.hpp` |
| 多相机 / 多光源 Scene | ✅ | `scene.hpp` (REQ-009) |
| `FrameGraph` + 多 `FramePass` | ✅ | `frame_graph.hpp` |
| `RenderQueue` + pass/target 过滤 | ✅ | `render_queue.hpp` |
| Push constant (`PC_Draw`) | ✅ | `gpu_resource.hpp` |
| Dirty 同步通道 | ✅ | `IGpuResource::setDirty()` |
| Mesh / OBJ / GLTF 加载 | ✅ | `src/infra/mesh_loader/` |
| Texture 加载（stb_image） | ✅ | `src/infra/texture_loader/` |
| Skeleton 资源（骨骼矩阵 UBO） | ✅ | `src/core/asset/skeleton.hpp` |
| Blinn-Phong 材质 loader | ✅ | `src/infra/material_loader/blinn_phong_material_loader.cpp` |
| ImGui 集成 | ✅ | `src/infra/gui/` |
| Window (SDL3 / GLFW) | ✅ | `src/infra/window/` |

### 工程 / 基础

| 能力 | 状态 | 位置 |
|------|------|------|
| CMake + Ninja 跨平台构建 | ✅ | `CMakeLists.txt` |
| 集成测试框架 | ✅ | `src/test/integration/` |
| StringID 字符串驻留 + compose | ✅ | `src/core/utils/string_table.hpp` |
| 项目文档 / spec / req 流程 | ✅ | `openspec/specs`, `docs/requirements` |

### 场景 / 游戏向

| 能力 | 状态 | 备注 |
|------|------|------|
| 几何体 + 材质绑定（`RenderableSubMesh`） | ✅ | 但没有 transform 层级 |
| 相机透视 / 正交 | ✅ | `core/scene/camera.hpp` |
| 方向光 | ✅ | `core/scene/light.hpp` |
| 点光 / 聚光 | ❌ | `LightBase` 抽象已就位，实现缺失 |
| Skeleton 资源存在 | ✅ | 但**没有动画播放器** |

---

## 关键缺口

按"离一个可玩的小游戏最近"的顺序列出。

### A. 场景层缺乏 transform 层级

现在 `Scene` 只持有一个扁平的 `std::vector<IRenderableSharedPtr>`。物体的"世界矩阵"是**外部代码每帧往 `PC_Draw.model` 里塞的**，没有父子关系、没有本地 / 世界坐标概念、没有层级脏标记。

**后果**：不能写"枪挂在人手上"、"摄像机跟着玩家"这类基本游戏逻辑。

### B. 没有输入层

窗口只暴露 `onClose(callback)` 这一类生命周期事件。键盘 / 鼠标 / 手柄的实时状态、action mapping、输入序列识别，都缺。

### C. 没有时间 / 游戏循环

`main` 循环里是 `while (running) { uploadData(); draw(); }`。没有：
- deltaTime 获取
- fixedUpdate（物理用）
- 帧率 cap
- 暂停 / 时间缩放

### D. 没有资产管线

资源加载全靠硬编码路径 + `cdToWhereShadersExist()` 这类启动期 cwd 校准。发布版里资产怎么在用户机器上被找到，没有设计。

- 没有 asset GUID 或 handle
- 没有 serialize / deserialize
- 场景全由 C++ 代码构造，没法"运行期保存 / 加载"

### E. 没有动画播放器

骨骼矩阵已经是 GPU 可消费的 `SkeletonUBO` 形态（`core/asset/skeleton.hpp:24`），但**没人往里写动画数据**。没有 `AnimationClip` 资源，没有 `AnimationPlayer` 组件。

### F. 没有物理

没有刚体、碰撞、射线、角色控制器。小游戏里任何涉及"碰一下"的逻辑都实现不了。

### G. 没有 gameplay 层

没有 component 生命周期（awake / start / update / destroy）、没有系统调度、没有脚本语言、没有事件总线。想给场景里一个物体加行为，只能在 `main` 的 while 里加 `if-else`。

### H. 没有音频

连最简单的"按键播放一个 wav"都做不到。

### I. 没有游戏内 UI

ImGui 是面向开发者的 debug UI，不是面向玩家的 UI（没有 theming / animation / retained mode）。做开始菜单、HUD、血条都需要额外东西。

### J. 没有编辑器

场景全是代码写的。加一个新物体需要重新编译。新人入手门槛高。

### K. 没有 profiler 集成

目前靠 `std::cerr` 日志和 `LX_RENDER_DEBUG=1` 环境变量。没有 CPU / GPU 时间线，没有帧内 event，没有 allocator 统计。

### L. 没有打包 / 发布管线

可执行依赖 `shaders/glsl/` 的源文件（运行期编译），依赖工程目录的 cwd 启发式查找资源。发布给第三方不可行。

### M. 渲染深度未完全解锁

即使只是"画得好看"，当前也缺：
- Shadow mapping
- IBL / 环境光
- HDR + tone mapping pass（目前 tone map 是在 shader 里手写的，不是独立 pass）
- Bloom / FXAA
- Frustum culling（`RenderQueue::buildFromScene` 对所有 renderable 都生成 item）
- Light 的点光 / 聚光类型

---

## AI-Native 维度的缺口

除了传统引擎能力之外，把引擎推向"AI-Native"需要额外补齐下面几类能力。它们与 A–M 的缺口**正交**，不是"传统缺口打完之后再做"，而是从 Phase 1 开始就要纳入考量。

### N. 没有 Web 后端

当前只有 Vulkan。AI 生态（聊天界面、demo、在线调试）天然生长在浏览器里：

- 没法把引擎渲染画面直接嵌在 web 页面
- 没法让 LLM 通过 WASM 试跑一段生成代码
- 未来的 web 编辑器没有驱动

**要解什么**：引入 WebGPU（via Dawn 或原生）或 WebGL2 作为第二后端，共享 `core/` 的所有抽象。

### O. 没有文本内省 / 结构化 dump

AI 最擅长读文本。当前引擎的内部状态只能通过：
- 看窗口里的像素
- 用 GDB attach
- 读 `std::cerr` 日志

都不是 agent 友好的信息通道。具体缺失：

- Scene tree 无 `dumpScene(format) → string`
- 组件字段无 JSON schema（Phase 6 的反射能顺带解决）
- 渲染状态无 `describePipeline(key) → string`
- 资产目录无 `listAssets(filter) → AssetSummary[]`
- 错误信息多是"xxx is null"，缺乏完整上下文路径

### P. 脚本语言对 AI 不友好

Phase 6 如果选 Lua，AI 的掌控力远不如 TypeScript：
- TS 生态巨大，训练数据充分
- TS 有静态类型，LLM 生成的代码更稳
- TS 与 JSON/YAML 配置文件天然通路
- TS 能直接复用 Vue / React 做 UI（与引擎 UI 层统一）

**要解什么**：从一开始把 Phase 6 的脚本层定在 TypeScript 上，不走 Lua。

### Q. UI 系统对 AI 不友好

传统游戏 UI 通常是：
- 自研 retained-mode DOM（Unity UGUI / Unreal UMG）
- 脚本驱动的 "widget tree"（Godot）
- 编辑器内拖拽生成（可读性对 AI 极差）

LLM 对 **HTML + CSS + Vue/React** 的掌控力远远超过任何一种游戏 UI 方案。原因是训练语料的分布：前端代码占了 LLM 代码训练集的 50%+。

**要解什么**：UI 层用一个"迷你 HTML+JS 容器 + Vue 子集"来实现，而不是重新发明一套 retained-mode UI。

### R. 没有 MCP 接口

当前引擎没有任何"外部 agent 能接入"的通路：

- 没有 RPC / WebSocket / 命名管道
- 没有自描述的 tool schema
- 没有权限 / 沙箱机制

**要解什么**：引擎默认启一个 MCP server（stdio + WebSocket 两种 transport），把内部能力封成 MCP tools。

### S. 没有 CLI 交互模式

引擎只能启动渲染进程。要让 agent 驱动，需要：

- 纯 headless 模式（不创建窗口，仍能加载场景、执行命令）
- REPL 风格的 CLI：`engine-cli --scene foo.json --chat`
- CLI 内部就是一个本地 agent，可连接 OpenAI / Anthropic / 本地模型
- 支持管道模式：`echo "dump scene" | engine-cli`

### T. 没有 AI 资产生成能力

目前资产全是"人类艺术家做好 → 导入"。AI-Native 引擎应该：

- 一句话生成贴图（文到图 / 图到图）
- 一句话生成 3D 模型（Text-to-3D：Zero123 / InstantMesh / Stable3D）
- 一句话生成动画（Motion Diffusion Model / AnimateDiff）
- 一句话生成角色（VRoid 风格 + ControlNet）
- 支持 NeRF / 3DGS 等新型表达（从真实世界扫描得到可渲染资产）

这些模型都在远程服务（本地或云端）跑，引擎侧需要一个"资产生成 pipeline 框架"来协调。

### U. 没有 Agent 运行时

即使接了 MCP，AI-Native 引擎的"完整形态"还需要引擎本身**长驻一个 agent**：

- 监听用户 CLI 输入
- 持有历史对话
- 调用 MCP tools
- 产出结构化响应（修改场景 / 生成资产 / 运行测试）
- 支持 skill 扩展（类似 Claude Code 的 `.claude/skills/`）

没有 agent runtime，引擎只是一个被动的 MCP server；有了 agent runtime，引擎变成一个**主动的 AI 协作者**。

---

## 工作量粗估

仅供排期参考。假设**一个熟悉 Vulkan + C++ 的开发者全职**，阶段内部可能还有子任务之间的并行。

| 阶段 | 乐观 | 悲观 | 备注 |
|------|------|------|------|
| Phase 1 渲染深度 + Web 后端 | 6 周 | 12 周 | WebGPU 后端多算 2–4 周 |
| Phase 2 基础层 + 文本内省    | 3 周 | 5 周 | dump 族 API 加 1 周 |
| Phase 3 资产管线            | 3 周 | 6 周 | 序列化格式选型会反复 |
| Phase 4 动画                | 3 周 | 6 周 | 状态机 + 混合通常吃 50% 时间 |
| Phase 5 物理                | 2 周 | 4 周 | 若选 Jolt 集成快；自写会翻倍 |
| Phase 6 Gameplay (TypeScript)| 4 周 | 8 周 | TS 集成 + bindings 比 Lua 重 |
| Phase 7 音频                | 1 周 | 2 周 | 纯音频部分较小 |
| Phase 8 Vue UI 容器         | 4 周 | 8 周 | HTML/CSS 子集 + reactivity |
| Phase 9 Web 编辑器          | 6 周 | 12 周 | 前端从零写 |
| **Phase 10 MCP + Agent + CLI** | 6 周 | 12 周 | 首次引入大块新能力 |
| **Phase 11 AI 资产生成**    | 6 周 | 14 周 | 模型服务集成成本高 |
| Phase 12 打包发布 + WASM    | 2 周 | 4 周 | Emscripten 构建链增加 |

**总计**：46–93 人周 ≈ **10 个月 – 1 年 10 月** 的全职投入，得到一个**刚刚够用**的 AI-Native 小型游戏引擎。

---

## 推进节奏建议

以"**一年交付一个可玩的 AI-Native 原型**"为目标，推荐这样分配：

### 前 3 个月 · MVP

- Phase 1 前半（Vulkan 部分的 shadow / IBL / post）
- Phase 2 全部（transform / input / time / **文本内省**）
- Phase 3 前半（asset GUID + 最小序列化）
- Phase 6 基础版（TS 脚本能跑）
- **Phase 10 最小子集**：MCP server 能对外暴露"读 scene / 改 transform / 加组件" 3 个 tool

**产物**：Claude Code 能通过 MCP 连接引擎，对着一个场景做"把 player 向左移两米"这类操作。

### 3–6 月 · Alpha

- Phase 1 后半（WebGPU/WebGL 后端）
- Phase 4 + Phase 5
- Phase 8 前半（HTML 容器 + 最小 Vue reactivity）
- Phase 9 前半（Web editor 能显示场景树 + inspector）
- Phase 10 扩展 tool 集（20+ 个 skill）

**产物**：浏览器里能看到一个活场景，agent 能通过对话修改它并看到结果。

### 6–9 月 · Beta

- Phase 7 音频
- Phase 8 完成
- Phase 9 完成
- **Phase 11 核心**（贴图生成 + 3D 模型生成）

**产物**：一句话"给我一只木桶"能出现在场景里并能物理交互。

### 9–12 月 · 发布

- Phase 11 扩展（动画 / NeRF / 3DGS 生成）
- Phase 12 全部（含 WASM）

**产物**：Windows / Linux / Web 三目标均可分发，AI 协作流程端到端可用。

---

## 下一步

阅读 [Phase 1](phase-1-rendering-depth.md) 或 [Phase 2](phase-2-foundation-layer.md)，挑一个动手。两条路径的**前置条件都已满足**，可以并行推进，最晚在 Phase 3 开始前合流。

若你对 AI-Native 核心变化最感兴趣，跳到 [Phase 10 · MCP + Agent + CLI](phase-10-ai-agent-mcp.md) 先读。
