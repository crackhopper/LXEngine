# Roadmap 边界：哪些是未来能力

Roadmap 像工作台旁边的施工图。它说明我们打算把 `lxe_editor` 扩展到 Web Editor、engine MCP、CLI agent、AssetRegistry 和热重载，但施工图不等于当前已经能用的设备。这个页面把当前 editor 设计和未来阶段分开，避免初学者把未实施方案当成代码事实。

## 当前已经有的基础

| 能力 | 当前代码里的落点 | 说明 |
|---|---|---|
| ImGui editor | `UiOverlay` + panels | 本地开发者工作台 |
| command-first 行为 | `CommandBus` | UI、Console、API、recording 共用 |
| project + scene persistence | `ProjectSession` + `SceneRuntime` | project 管文件，scene runtime 管当前场景 |
| HTTP/WebSocket 状态观察 | `LxeEditorApiService` | 暴露 command、state、events、recording |
| 录制与回放 | `RecordingController` | 记录 command steps |
| manager/MCP 诊断链路 | 外部 manager 调用 editor API | 当前主要服务调试和自动化测试 |

这些能力支撑当前设计文档前五页。它们不是 Phase 9/10 的完整交付，但已经给未来 Web Editor 和 agent 留下了命令与观察入口。

## Roadmap 中还没有实现的三条线

| Roadmap 方向 | 未实施内容 | 关联 pending REQ |
|---|---|---|
| Phase 9 Web Editor | 浏览器 editor shell、WebSocket IPC schema、Web UI 面板复用 command/event | `REQ-044-a` |
| Phase 10 MCP + Agent + CLI | engine-level MCP server、headless CLI、agent runtime、权限/成本模型 | `REQ-044-b` |
| Phase 3 Asset Pipeline | AssetRegistry、GUID、`.meta`、热重载、editor asset handle | `REQ-044-c` |

这三条线都和 editor 有关，但切入层不同：Web Editor 改的是 UI 外壳，MCP/CLI 改的是外部能力入口，AssetRegistry 改的是 asset identity 和持久化引用。

## 为什么不把它们写进当前实现页

当前实现页要回答“现在代码怎样运行”。Future roadmap 要回答“下一步怎么演进”。如果混在一起，初学者会遇到两个问题：

| 混写带来的问题 | 正确边界 |
|---|---|
| 看到 Web Editor 就以为已有 `editor.html` | 当前只有 ImGui editor，Web Editor 是 REQ-044-a |
| 看到 MCP 就以为引擎有标准 MCP server | 当前是 editor API + manager 诊断链路，engine MCP 是 REQ-044-b |
| 看到 Asset GUID 就以为 scene 已按 GUID 保存 | 当前 scene 主要保存 URI/path，AssetRegistry bridge 是 REQ-044-c |

因此本页只做未来映射，不把 future 当成 current。

## 三条线怎样复用当前设计

| 当前设计点 | Web Editor 怎样复用 | MCP/CLI 怎样复用 | AssetRegistry 怎样复用 |
|---|---|---|---|
| `CommandBus` | 浏览器按钮 dispatch command | MCP tool / CLI 子命令调用 command | 导入、替换、热重载也应有 command |
| `CommandResult` | Web UI 显示 message/structured | tool response 使用 structured JSON | 资产操作返回 guid/path/status |
| `LxeEditorApiService` event | WebSocket 推事件 | agent 订阅状态变化 | asset reload 推 dirty/update event |
| `SceneRuntime` | Web Inspector 改 scene document/runtime | agent 修改 scene 后保存 | scene URI 逐步升级到 asset handle |

这也是当前 editor 设计最值得保留的部分：行为线、数据线、观察线已经分开，未来只需要扩展入口和 schema，而不是重写 editor 逻辑。

## 继续阅读

- [API、事件与录制如何观察 editor](05-api-recording-and-observation.md)
- [Phase 9 Web 编辑器](../../roadmaps/main-roadmap/phase-9-web-editor.md)
- [Phase 10 MCP + Agent + CLI](../../roadmaps/main-roadmap/phase-10-ai-agent-mcp.md)
