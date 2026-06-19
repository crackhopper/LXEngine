# 00 · 0.2.0-pre 之后的 Gap Analysis

本页从当前代码出发，盘点下一段路线的真实缺口。已完成能力以 [v0.2.0-pre 发布记录](../../releases/v0.2.0-pre/CHANGELOG.md) 为准。

## 已经站住的地基

| 领域 | 当前事实 |
|---|---|
| Editor | `src/editor/` 是主入口，支持 project/session、SceneRuntime、ImGui panels、CommandBus、HTTP/WebSocket/API、recording |
| Scene | `SceneNode` 有 transform、父子层级、component、camera/light/renderable 组织；`SceneResourceTable` 管 typed handles |
| Material | `.material v2`、material contract reflection、RenderFeature、RenderPathGraph、surface envelope 已进入主线 |
| Render work | `FramePass` 直接持有 input contract；`RenderWorkCompiler` 生成 `RenderInput[]` 和 `RenderInputDesc[]` |
| Pipeline | `PipelineKey(MaterialTypeVariant, RenderPathNodeSignature)` + `PipelineBuildDesc` + Vulkan `PipelineCache` 已能预构建 graphics/compute pipeline |
| IBL | `feature.environmentLighting`、`feature.surfaceLighting`、environment/material bake manifests、async bake job service 已接入 |
| Offline | `lxe_offline_render` 读取 scene/profile，走 headless Vulkan compute，输出 EXR/PNG/JSON/raw |
| Agent-adjacent | CommandBus、HTTP/WebSocket API、recording、manager MCP debug 通道已有基础 |

## 当前最大缺口

| 缺口 | 为什么重要 | 下一步口径 |
|---|---|---|
| Backend graph execution | core DAG、compiler 和 executor 已有，但 attachment/barrier/diagnostics 还需要继续统一 | 只沿 `RenderInputDesc` / `FrameGraphExecutor` 收敛 |
| RenderFeature 参数事实源 | feature asset 已定义 forward/environment/surface/tone/bloom，但仍有少量 runtime helper 写参数 | feature asset 优先，手写 builder 逐步退场 |
| Package / AssetRegistry | 大资产、IBL cache、offline compare、发布打包需要稳定 GUID/cache identity | Phase 3 |
| Offline path tracing | compute MVP 可跑，但还没有多 bounce、纹理采样、AOV、硬件 RT | Phase 1 后续切片 |
| Agent-level engine API | manager MCP 能管理进程和调试 editor，但不是 engine capability manifest | Phase 10 |

## 已经不作为当前阻塞的问题

| 内容 | 判断 |
|---|---|
| 旧 per-pass queue / work item | 已 hard cut，不再作为当前架构入口 |
| editor 目录旧位置 | `src/editor/` 是唯一当前入口 |
| 多 pass 表达能力 | RenderPathGraph / FrameGraph / RenderWorkCompiler 已能表达当前 Forward/Bloom/Debug/IBL bake |
| CSM 基础 | directional shadow / CSM 已完成，后续是质量和诊断 |
| PBR/IBL 基线 | 当前基线已能验证 PBR + IBL bake，后续是更完整资产和 probe |

## 下一步

先读 [Phase 1 · Rendering Depth](phase-1-rendering-depth.md)。它把 0.2.0-pre 后的渲染路线收敛到 backend graph execution、RenderFeature 参数合同、PBR/IBL/OfflineRT 的同轨推进。
