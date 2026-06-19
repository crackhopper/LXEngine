# Roadmaps

Roadmap 像一张长期施工图：它不重复已经落地的发布记录，也不保存已经被 hard cut 的旧方案；它只保留当前基线之后仍然有效的阶段规划。需要重新进入主线的研究主题，必须先落到当前 requirement 或设计 spec，再写回 roadmap。

## 当前路线入口

| 入口 | 作用 |
|---|---|
| [Main Roadmap](main-roadmap/README.md) | `0.2.0-pre` 之后的总路线、当前主线和 phase 依赖 |
| [Gap Analysis](main-roadmap/00-gap-analysis.md) | 从当前代码事实出发，盘点下一段路线的真实缺口 |
| [AI-Native 引擎核心原则](main-roadmap/principles.md) | 跨阶段架构不变量，phase 文档与它冲突时以原则页为准 |

## Phase 地图

| Phase | 文档 | 主题 |
|---|---|---|
| 1 | [Rendering Depth](main-roadmap/phase-1-rendering-depth.md) | 渲染图执行、PBR/IBL、post/deferred/offline 统一 |
| 2 | [Foundation Layer](main-roadmap/phase-2-foundation-layer.md) | 输入、时间、结构化内省、空间查询 |
| 3 | [Asset Pipeline](main-roadmap/phase-3-asset-pipeline.md) | AssetRegistry、`.meta`、热重载、导入与 package |
| 4 | [Animation](main-roadmap/phase-4-animation.md) | Skeleton 之后的 clip/player/state machine |
| 5 | [Physics](main-roadmap/phase-5-physics.md) | CPU 物理、刚体、碰撞和 query |
| 6 | [Gameplay Layer](main-roadmap/phase-6-gameplay-layer.md) | gameplay lifecycle、脚本层和命令绑定 |
| 7 | [Audio](main-roadmap/phase-7-audio.md) | 最小音频系统 |
| 8 | [Web UI](main-roadmap/phase-8-web-ui.md) | 游戏内 HTML/Vue 子集 UI |
| 9 | [Web Editor](main-roadmap/phase-9-web-editor.md) | 浏览器编辑器 shell 与 IPC |
| 10 | [Agent / MCP / CLI](main-roadmap/phase-10-ai-agent-mcp.md) | engine CLI、MCP server 和 agent 入口 |
| 11 | [AI Asset Generation](main-roadmap/phase-11-ai-asset-generation.md) | 生成资产接入、import 和 provenance |
| 12 | [Release](main-roadmap/phase-12-release.md) | 桌面包、shader 预编译、发布 artifact |

## 维护规则

站点导航由 `scripts/notes/generate_site_config.py` 自动按目录展开。新增 roadmap 页面时，先确认它描述的是当前缺口或明确的未来阶段；已经进入实现队列的内容应链接到 `notes/requirements/`，已经完成的能力应进入发布记录，而不是继续在 roadmap 里重复维护。
