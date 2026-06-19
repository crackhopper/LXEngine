# Editor System：从工作台到命令线路

`lxe_editor` 像一张有远程控制能力的工作台：视口和面板让人直接操作场景，CommandBus 把这些操作变成可记录、可撤销、可由 API/MCP 复用的命令，SceneRuntime 再把 scene document 变成真正运行的 scene。这个文件夹按因果顺序拆解 editor 系统，帮助我们从入口读到具体面板和远程诊断。

这组文档只把当前代码讲成当前能力。roadmap 里的 Web Editor、engine-level MCP / CLI / agent、AssetRegistry / GUID / 热重载都单独放在 [Roadmap 边界](06-roadmap-boundaries.md)，并标注未实施需求。

## 为什么按这个顺序读

先看生命周期，再看命令，再看 UI。因为面板和 toolbar 不是孤立 UI，它们依赖 session 先创建 scene runtime、CommandBus、EditorState 和 panels。如果先读 UI 代码，很容易把按钮当成行为本身；按当前代码，按钮只是触发命令或切换轻量 UI 状态的入口。

| 顺序 | 页面 | 解决的问题 |
|---|---|---|
| 1 | [主循环与对象归属](01-main-loop-and-ownership.md) | editor 进程启动后，谁创建并持有哪些对象 |
| 2 | [CommandBus：共享行为的中线](02-command-first-surface.md) | 为什么 UI、Console、API、MCP 都复用命令 |
| 3 | [面板、toolbar 与视口怎样接入命令](03-panels-toolbar-and-viewport.md) | 具体按钮和面板如何连到 CommandBus |
| 4 | [SceneRuntime 与持久化边界](04-scene-runtime-and-persistence.md) | scene document、runtime scene、本地 editor 状态怎样分工 |
| 5 | [API、事件与录制如何观察 editor](05-api-recording-and-observation.md) | 远程自动化如何复用同一套 editor 行为 |
| 6 | [Roadmap 边界：哪些是未来能力](06-roadmap-boundaries.md) | Web Editor、engine MCP/CLI、AssetRegistry 等方向与当前代码的边界 |

## 核心对象地图

| 对象 | 主要位置 | 角色 |
|---|---|---|
| `LxeEditorSession` | `src/editor/app/editor_session.*` | editor 的组合根，持有 runtime、CommandBus、panels、scene interaction 和本地状态 |
| `UiOverlay` | `src/editor/ui/ui_overlay.*` | ImGui 总入口，绘制 toolbar、stats、help、preferences，并调用 panels |
| `CommandBus` | `src/editor/commands/command_bus.*` | 命令注册、dispatch、completion、history、undo/redo |
| `SceneRuntime` | `src/editor/runtime/scene_runtime.*` | 把 `.scene.yaml` 文档构造成运行时 `Scene` |
| `EditorState` | `src/editor/app/editor_state.*` | selection、preview camera、editor camera 等短期 editor 状态 |
| `LxeEditorApiService` | `src/editor/api/lxe_editor_api_service.*` | HTTP/WebSocket/MCP 使用的状态快照、命令执行和事件流 |

## 新人最容易混淆的三条线

| 线 | 当前入口 | 典型问题 |
|---|---|---|
| 行为线 | `CommandBus` | 点击按钮、输入命令、HTTP/MCP 调用最终是不是同一条行为 |
| 数据线 | `ProjectSession` / `SceneRuntime` / `EditorState` | 哪些状态会保存到 project，哪些只是本机 editor 状态 |
| 观察线 | `LxeEditorApiService` / recording / events | 外部工具如何知道 command 已执行、scene 已保存、selection 已改变 |

读代码时先判断一个修改属于哪条线，可以避免把 UI、持久化和远程观察混在一起。

## 当前边界

这组设计文档描述当前代码事实，不把旧 registry 占位、Web Editor、engine MCP 方案当成已实现能力。toolbar / command / node / light 的扩展说明以当前手工合同为准；更长期的 roadmap 对应 [Roadmap 边界](06-roadmap-boundaries.md)。

## 继续阅读

从 [主循环与对象归属](01-main-loop-and-ownership.md) 开始。读完当前实现后，再看 [Roadmap 边界](06-roadmap-boundaries.md)。
