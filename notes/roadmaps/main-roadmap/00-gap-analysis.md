# 00 · v0.1.0 之后的 Gap Analysis

> 本页从当前代码出发，盘点下一段路线的真实缺口。已完成能力以 [v0.1.0 发布记录](../../releases/v0.1.0/CHANGELOG.md) 为准。

## 当前已经站住的地基

| 领域 | 当前事实 |
|---|---|
| Editor | `lxe_editor` 是主入口，支持 scene tree、inspector、toolbar、command bus、API、recording |
| Scene | `SceneNode` 有 transform、父子层级、path、component、camera/light/renderable 组织 |
| Material | `.material v2` surface envelope、material contract reflection、RenderPathGraph、RenderFeature、SceneResourceTable 依赖模型已进入主线 |
| Render queue | `FrameGraph` 持有 `FramePass`，`RenderWorkQueue` 按 pass/target 从 scene 生成 `RenderWorkItem` |
| Pipeline | `PipelineKey(MaterialTypeVariant, RenderPathNodeSignature)` + `PipelineBuildDesc` + Vulkan `PipelineCache` 已能预构建 graphics/compute pipeline |
| Light | `DirectionalLight` / `PointLight` / `SpotLight` 数据与 editor 作者入口已存在，`SceneLightsUBO` 已有 |
| Asset files | `.scene.yaml`、`.material`、model/texture loader 可用 |
| Agent-adjacent | CommandBus、HTTP/WebSocket API、recording、manager MCP debug 通道已有基础 |

这些能力让我们不需要再把“能不能搭场景”当作主线。接下来真正的缺口集中在多 pass 渲染、资产管理和正式 agent 入口。

## 当前最大的渲染缺口

| 缺口 | 当前代码状态 | 为什么阻塞下一步 |
|---|---|---|
| RenderPathGraph hard cut | Forward/Deferred/Post/Debug/OfflineRT 正在从 runtime 手写分支收敛到 graph 文件 | 需要防止旧默认 graph、旧 material technique 或 silent fallback 泄漏 |
| Bindless / material storage | `SceneResourceTable` 和 material storage 基础已就位，完整 indirect material batching 仍在推进 | 大场景和 package load 需要稳定 typed index / handle |
| Backend graph execution | core `FrameGraph::compile()` 已有 DAG；backend attachment aliasing、barrier 全量推导和多 queue timeline 仍未完成 | HDR/post/deferred/offline 等路径需要统一执行层 |
| Pipeline identity closure | key 已收敛为 material type/source variant + RenderPathNode signature | 还需要持续审计旧 object/target 独立轴和测试 fixture |
| Package canonical state | 资源 handle、pipeline cache、SceneResourceTable 序列化还在 active REQ 中 | BMW/Sponza 等大资产需要快速、可验证加载 |

结论：当前最大风险已经从“能否表达多 pass”转为“能否把旧材质/管线桥彻底切掉，并让 realtime/offline/package 全部消费同一套 graph/resource truth”。

## 已经不是主阻塞的内容

| 内容 | 现状判断 |
|---|---|
| Point/Spot light 数据 | 已落地；后续是 shading 质量和 shadow 支持，不是对象模型 |
| Editor 创建/编辑对象 | 已满足测试场景搭建需求 |
| 材质参数覆盖 | 已满足实验材质和 scene document 覆盖 |
| CPU task 并行 | 对 shadow/G-Buffer 不是前置；先做会缺少资源依赖真值 |
| Async compute | 当前 shadow/HDR/G-Buffer 都可 graphics-only；compute 后置 |
| Bindless | 能减少 descriptor/layout 压力，但不是单 directional shadow 的前置 |

## 非渲染缺口

| 缺口 | 为什么重要 | 放在何处 |
|---|---|---|
| AssetRegistry / `.meta` / GUID | editor 资产面板、热重载、发布打包、生成资产都需要稳定身份 | Phase 3，v0.1.1 pending |
| 显式 runtime asset root | 发布与 headless 运行不能靠 cwd 启发式 | Phase 3 / Phase 12 |
| Engine CLI | 当前有 editor API 和 manager debug，但还没有用户级 CLI | Phase 10 |
| MCP/Agent 正式入口 | manager MCP 不是引擎能力 manifest；需要 Query/Command 工具化 | Phase 10 |
| Action mapping / fixed step | 游戏逻辑和物理需要 | Phase 2 |
| Animation / Physics / Gameplay | 小型游戏引擎完整性 | Phase 4/5/6 |

## 当前不做的高阶渲染项

| 项 | 后置原因 |
|---|---|
| 256 光源 sparse shadow | 单 directional shadow + CSM 先验证资源图和 pass 执行 |
| Clustered / GPU-driven lighting | 需要 G-Buffer 或大量局部光压力证明 |
| TAA / Volumetric fog | 需要 motion vector/history buffer 等更完整 frame graph |
| Ray tracing | desktop-only 加分项，当前不是最短路径 |
| WebGPU/WebGL2 后端 | 仍重要，但 shadow/G-Buffer 的核心设计先在 Vulkan 跑通 |

## 下一步

先读 [Phase 1 · Rendering Depth](phase-1-rendering-depth.md)。它把 v0.1.1 active 范围收敛为 FrameGraph v1、shadow、CSM，并把 HDR/post、G-Buffer 等后续内容放到 pending。
