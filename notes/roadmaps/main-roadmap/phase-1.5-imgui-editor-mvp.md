# Phase 1.5 · v0.1.0 Editor Baseline

> 状态：已完成，作为历史索引保留。详细完成项见 [v0.1.0 CHANGELOG](../../releases/v0.1.0/CHANGELOG.md)。

Phase 1.5 的任务是让引擎获得一个能搭测试场景的交互底座：scene tree、inspector、toolbar、TRS gizmo、command bus、debug draw、camera/light/material authoring、scene document 保存与恢复。这个阶段已经随 v0.1.0 进入发布记录，不再作为未来计划维护。

## 对后续路线的贡献

| 已完成能力 | 后续如何使用 |
|---|---|
| `lxe_editor` 场景搭建 | 用来搭 shadow / G-Buffer 测试场景 |
| CommandBus + API + recording | Phase 10 CLI/MCP 的基础 |
| SceneNode transform / hierarchy / component | renderable validation、light/camera authoring、culling 都依赖 |
| CameraComponent | shadow camera / editor preview / render target 绑定基础 |
| Directional / Point / Spot light 数据 | Phase 1 多光源 shading 和 shadow 消费 |
| Material override | 快速实验 PBR / stylized material 参数 |
| DebugDraw | shadow cascade、G-Buffer debug、light bounds 可视化 |

## 不在本页继续维护的内容

| 内容 | 现在看哪里 |
|---|---|
| 已完成需求 | [v0.1.0 CHANGELOG](../../releases/v0.1.0/CHANGELOG.md) |
| 当前编辑器设计 | [Editor System](../../design/editor-system/index.md) |
| 场景系统概念 | [Scene System](../../scene-system/index.md) |
| 后续 Web Editor | [Phase 9](phase-9-web-editor.md) |
| 后续 CLI/MCP | [Phase 10](phase-10-ai-agent-mcp.md) |

## 下一步

继续 [Phase 1 · Rendering Depth](phase-1-rendering-depth.md)。当前 shadow / G-Buffer 不再缺场景搭建工具，真正前置是 FrameGraph v1。
