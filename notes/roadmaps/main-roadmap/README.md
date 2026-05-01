# Roadmap · LXEngine 走向 AI-Native 小型游戏引擎

> 目标：把当前的 Vulkan 教学渲染器推进到一个 **AI-Native 小型游戏引擎**。
> 本文件是整份 roadmap 的入口和阅读地图。

## AI-Native 的定义

传统引擎把**人类开发者**当作唯一的一等用户：专有编辑器、可视化脚本、GUI-only 工作流。AI-Native 把**AI Agent 和人**当作平权使用者。对应 5 条硬性约束：

1. **引擎内置 Agent + Skills**：CLI / 运行时内嵌长驻 agent，暴露结构化 skills。
2. **默认 MCP 接入**：引擎暴露标准 [Model Context Protocol](https://modelcontextprotocol.io/) 接口。
3. **文本优先内省**：scene tree / 组件 / 资产 / 管线状态统一暴露为 JSON / YAML / DSL 文本。
4. **AI 友好的技术栈**：游戏脚本走 **TypeScript**；UI 走 **Vue 子集 + HTML/JS**；编辑器走 **Web + WebGL/WebGPU**。
5. **AI 深度参与资产生产**：集成文本→贴图 / 文本→3D / 文本→动画 / NeRF / 3DGS 生成管线。

架构级的 20 条不变量见 [principles.md](principles.md)。

## 当前聚焦路径（2026-05-01）

| 顺序 | 目标 | 对应 phase / REQ |
|------|------|-------------------|
| 1 | **ImGui Editor MVP + 命令总线**（场景树 / inspector / TRS gizmo / DebugDraw / 控制台 / F 键预览） | [Phase 1.5](phase-1.5-imgui-editor-mvp.md) REQ-035 / 036 / 037-a / 037-b / 038 / 039 / 040 / 041 |
| 2 | **多光源（point + spot）+ 统一 GPU 合同** | Phase 1 REQ-109 |
| 3 | **Shadow + CSM** | Phase 1 REQ-103 / REQ-104 |
| 4 | **PBR 完整管线**（IBL ambient + shadow + 多光源 + 完整贴图集，伴随 RTR 阅读渐进推进） | Phase 1 REQ-118 |
| 5 | **IBL scene-level 资源 + 预过滤** | Phase 1 REQ-105 / REQ-106 |
| 6 | **G-Buffer / 延迟渲染** | Phase 1 REQ-119 |
| 7 | 引擎 CLI + agent 入口 | Phase 10 REQ-1004 |
| 8 | **NeRF / 3DGS 接入**（AI 资产生成优先项） | Phase 11 REQ-1102 |
| 9 | 角色全链路：立绘 → 3D 模型 → 骨骼 → 动作 | Phase 11 REQ-1103 ~ REQ-1106 |
| 10 | 游戏 UI 生成 | Phase 11 REQ-1107 |
| 11 | 通用贴图 / 3D / 环境兜底 | Phase 11 REQ-1108 |

其他阶段（Phase 2–9 / Phase 12）按 roadmap 原依赖顺序协同推进。

> **2026-05-01 调整说明**：把 Phase 2 transform / picking 与新增的 ImGui editor / DebugDraw / 命令总线 合并为 Phase 1.5，置于 Phase 1 渲染深度之前。理由：进入 RTR (Real Time Rendering 4th) 章节实验阶段，需要一个能在运行时摆放模型/相机/光源、可视化视锥与光源影响范围的交互工具，否则每章实验都要改硬编码场景。命令总线同时为 Phase 10 MCP 预留接入点，MCP shim 推到 Phase 1.6 单独做。Phase 1 内部 REQ 顺序按"编辑器有更多可玩内容 + 视觉反馈最强"重排为 109 → 103/104 → 118 → 105/106 → 119。

## 当前实施进度快照（2026-05-01）

| Phase | 状态 | 已落地关键能力 | 主要缺口 |
|-------|------|----------------|----------|
| [Phase 1](phase-1-rendering-depth.md) 渲染深度 + Web 后端 | 部分开工 | `FrameGraph` / `RenderQueue` / `PipelineCache` / 多 pass 骨架；`Camera::cullingMask` + `SceneNode::visibilityLayerMask` 交集过滤；通用 `.material` YAML loader + `pbr_gold.material` 示例；ImGui overlay；SDL3 路径 | Shadow / CSM / Bloom / FXAA / IBL / PointLight + SpotLight / 多光源 GPU 合同 / 视锥剔除 / WebGPU / WebGL2 / Headless；**Frame Graph 演进** —— 当前 `FrameGraph` 仅为最初阶骨架（无资源依赖 / barrier / aliasing），见 [REQ-042](../../requirements/042-render-target-desc-and-target.md) + [research/frame-graph](../research/frame-graph/README.md)；**Async Compute** —— LX 完全无 compute 路径，见 [research/async-compute](../research/async-compute/README.md)（候选 REQ-A..F，强触发条件主要在 Phase 5 物理走 GPU）；**GPU-Driven + Clustered Lighting** —— REQ-119 G-Buffer / REQ-109 多光源 / REQ-110 Frustum Culling 的实施细节，见 [research/gpu-driven-rendering](../research/gpu-driven-rendering/README.md)（候选 REQ-A..H，是上面三个调研的消费方）；**Shadows + Sparse Resources** —— REQ-103 / REQ-104 / REQ-109 256 光源阴影 + Vulkan sparse residency，见 [research/shadows](../research/shadows/README.md)（候选 REQ-A..G，最下游消费方）；**VRS + Specialization Constants**（后期优化，**优先级低**）—— Phase 1 主路径完成后的性能 tuning，见 [research/variable-rate-shading](../research/variable-rate-shading/README.md)（轻量 4 篇 + 入口索引）；**Temporal 子系统（Volumetric Fog + TAA）** —— 现代渲染管线的 temporal 范式，LX 完全空白，见 [research/temporal-techniques](../research/temporal-techniques/README.md)（候选 REQ-A..F，最下游消费方，依赖 frame-graph / multi-threading/08 / clustered lighting / cubemap shadow / async-compute 全部就位）；**Ray Tracing 子系统（Ch12-15 全）** —— 硬件 RT 整套能力（AS / SBT / ray query + RT shadow / DDGI / RT reflection + SVGF），LX 完全空白，见 [research/ray-tracing](../research/ray-tracing/README.md)（候选 REQ-A..H，**desktop-only 加分项 / 跟 WebGPU 路径冲突，必须双路径 fallback**；最大复杂度调研，依赖前面 7 个调研全部就位） |
| [Phase 1.5](phase-1.5-imgui-editor-mvp.md) ImGui Editor MVP + 命令总线 | 部分开工，REQ-035 / REQ-036 已完成，后续从 REQ-037-a 继续 | `Transform` 值类型 / scene path lookup | [REQ-035](../../requirements/finished/035-transform-component.md) Transform / [REQ-036](../../requirements/finished/036-scene-node-path-lookup.md) 路径查询 / [REQ-037-a](../../requirements/037-a-component-model-foundation.md) IComponent 基础 + Mesh/Material/Skeleton 转 component / [REQ-037-b](../../requirements/037-b-camera-as-component.md) Camera 作为 component / [REQ-038](../../requirements/038-ray-aabb-picking-min.md) ray-AABB picking / [REQ-039](../../requirements/039-debug-draw-subsystem.md) DebugDraw / [REQ-040](../../requirements/040-editor-command-bus.md) 命令总线 / [REQ-041](../../requirements/041-imgui-editor-mvp.md) ImGui Editor MVP；MCP shim 推到 Phase 1.6 |
| [Phase 2](phase-2-foundation-layer.md) 基础层 + 文本内省 | 部分开工 | `Clock`（`src/core/time/clock.*`）；`EngineLoop`（`src/core/gpu/engine_loop.*`）；`IInputState` / `Sdl3InputState` / `DummyInputState` / `MockInputState`；Orbit + FreeFly 控制器；ImGui + debug panel helper | Action mapping / Gamepad / `dumpScene` / AABB + 空间索引（**Transform 层级 / Camera-as-SceneNode / 路径查询 / picking 已提前到 Phase 1.5**） |
| [Phase 3](phase-3-asset-pipeline.md) 资产管线 | 未开工 | 资产目录 + `cdToWhereAssetsExist` | GUID / 序列化 / 热重载 |
| [Phase 4](phase-4-animation.md) 动画 | 未开工 | `Skeleton` / `SkeletonUBO`（`src/core/asset/skeleton.*`） | 动画 clip / 播放器 / 状态机 / 混合 |
| [Phase 5](phase-5-physics.md) 物理 | 未开工 | — | 刚体 / 射线 / 角色控制器 |
| [Phase 6](phase-6-gameplay-layer.md) Gameplay (TypeScript) | 未开工 | — | TS runtime / 组件 / 事件总线 |
| [Phase 7](phase-7-audio.md) 音频 | 未开工 | — | 全部 |
| [Phase 8](phase-8-web-ui.md) Vue UI 容器 | 未开工 | — | HTML/JS 容器 + Vue 子集 |
| [Phase 9](phase-9-web-editor.md) Web 编辑器 | 未开工 | — | 全部 |
| [Phase 10](phase-10-ai-agent-mcp.md) MCP + Agent + CLI | 未开工 | — | 全部 |
| [Phase 11](phase-11-ai-asset-generation.md) AI 资产生成 | 未开工 | — | 全部 |
| [Phase 12](phase-12-release.md) 打包 / 发布 | 未开工 | — | 全部 |

活跃的 `notes/requirements/` 条目：

- [`REQ-019` demo_scene_viewer](../../requirements/019-demo-scene-viewer.md)（仅剩显示环境下的人工验收）
- [`REQ-042` RenderTarget 拆分为 descriptor 与 binding](../../requirements/042-render-target-desc-and-target.md)（草稿，后置到 Phase 1.5 完工后 / Phase 1 [REQ-103 Shadow](phase-1-rendering-depth.md#req-103--shadow-pass--depth-only-pipeline) 之前。详见 [research/frame-graph](../research/frame-graph/README.md)）
- [`REQ-034` 删除 RenderTarget::getHash dead code](../../requirements/finished/034-remove-render-target-get-hash.md)（已归档；独立 cleanup PR，原 REQ-042 R9 拆出，不阻塞 Phase 1.5 / Phase 1）

其他历史 REQ 均已归档或下沉到本 roadmap 的对应 phase。

## 阶段依赖图

```mermaid
flowchart TD
    current["现状<br/>Vulkan 渲染器 + 基础 runtime<br/>00-gap-analysis.md"]
    p1_5["Phase 1.5<br/>ImGui Editor MVP + 命令总线"]
    p1["Phase 1<br/>渲染深度 + Web 后端"]
    p2["Phase 2<br/>基础层 + 文本内省"]
    p3["Phase 3<br/>资产管线"]
    p4["Phase 4<br/>动画"]
    p5["Phase 5<br/>物理"]
    p6["Phase 6<br/>Gameplay (TypeScript)"]
    p7["Phase 7<br/>音频"]
    p8["Phase 8<br/>Vue UI 容器"]
    p9["Phase 9<br/>Web 编辑器"]
    p10["Phase 10<br/>MCP + Agent + CLI"]
    p11["Phase 11<br/>AI 资产生成"]
    p12["Phase 12<br/>打包 / 发布"]

    current --> p1_5
    p1_5 --> p1
    p1_5 --> p2
    p1 --> p3
    p2 --> p3
    p3 --> p4
    p3 --> p5
    p3 --> p8
    p4 --> p6
    p5 --> p6
    p8 --> p6
    p6 --> p7
    p6 --> p9
    p8 --> p9
    p7 --> p10
    p9 --> p10
    p6 --> p10
    p10 --> p11
    p10 --> p12
```

**并行窗口**：

- Phase 1 + Phase 2 + Phase 8 可三路并行。
- Phase 4 + Phase 5 + Phase 8 在 Phase 2/3 落地后同样并行。
- Phase 10 之后，Phase 11 与 Phase 12 并行。

## 阶段索引

| Phase | 标题 | 一句话目标 | 依赖 |
|-------|------|----------|------|
| [principles](principles.md) | **AI-Native 架构原则（宪法）** | 跨阶段的 20 条架构不变量 | — |
| [00](00-gap-analysis.md) | Gap Analysis | 盘点当前状态与 AI-Native 小型游戏引擎的差距 | — |
| [1.5](phase-1.5-imgui-editor-mvp.md) | ImGui Editor MVP + 命令总线 | 场景树 / inspector / TRS gizmo / DebugDraw / 控制台 / F 键预览 | 现状 |
| [1](phase-1-rendering-depth.md) | 渲染深度 + Web 后端 | shadow / IBL / HDR + WebGPU / WebGL2 后端 | Phase 1.5 |
| [2](phase-2-foundation-layer.md) | 基础层 + 文本内省 | transform / input / time + `dumpScene`（部分内容已提前到 Phase 1.5） | Phase 1.5 |
| [3](phase-3-asset-pipeline.md) | 资产管线 | GUID + 序列化 + 热重载 | Phase 2 |
| [4](phase-4-animation.md) | 动画 | 骨骼动画 + 状态机 | Phase 2 / 3 |
| [5](phase-5-physics.md) | 物理 | 刚体 / 射线 / 角色 | Phase 2 |
| [6](phase-6-gameplay-layer.md) | Gameplay (TypeScript) | 组件 + TS 脚本 + 事件总线 | Phase 2 / 3 / 4 / 5 |
| [7](phase-7-audio.md) | 音频 | 3D 声场 + mixer | Phase 2 / 3 |
| [8](phase-8-web-ui.md) | Vue UI 容器 | HTML + Vue 子集 | Phase 1 / 6 |
| [9](phase-9-web-editor.md) | Web 编辑器 | 浏览器内编辑器 + WebSocket IPC | Phase 1 / 2 / 8 |
| [10](phase-10-ai-agent-mcp.md) | MCP + Agent + CLI | 引擎内置 agent，暴露 MCP tools | Phase 2 / 3 / 6 / 9 |
| [11](phase-11-ai-asset-generation.md) | AI 资产生成 | 贴图 / 模型 / 动画 / NeRF / 3DGS 生成管线 | Phase 3 / 10 |
| [12](phase-12-release.md) | 打包 / 发布 | Win / Linux / WASM 三目标 | 全部 |
| [研究留档](../research/README.md) | 技术预研 | 暂不进入 phase 的长期技术方向 | — |

## 如何阅读

- **从零开始**：先读本文件 → [principles.md](principles.md) → [00-gap-analysis.md](00-gap-analysis.md) → 按序浏览 phase。
- **只关心 AI-Native 核心**：[Phase 10](phase-10-ai-agent-mcp.md) → [Phase 2 内省部分](phase-2-foundation-layer.md) → [Phase 6](phase-6-gameplay-layer.md) → [Phase 11](phase-11-ai-asset-generation.md) → [Phase 9](phase-9-web-editor.md)。
- **只想动手**：图形优先走 Phase 1；游戏逻辑优先走 Phase 2 / 3 / 6。

## 实施原则

- `core/` 不 include `infra/` / `backend/`；构造函数注入，禁止 setter DI。
- 每个新增能力先写 `openspec/specs/<capability>/spec.md`，再实现。
- 不追求“完整”，追求“够用”。
- **优先描述接口和能力，具体库 / 算法 / 协议视作可替换选型。** roadmap 不锁定具体实现，方便未来重构。
