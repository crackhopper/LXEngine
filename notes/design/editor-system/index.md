# Editor System：从工作台到命令线路

`lxe_editor` 像一张有远程控制能力的工作台：视口和面板让人直接操作场景，CommandBus 把这些操作变成可记录、可撤销、可由 API/MCP 复用的命令，SceneRuntime 再把 scene document 变成真正运行的 scene。这个文件夹按因果顺序拆解 editor 系统，帮助我们从入口读到具体面板和远程诊断。

## 为什么按这个顺序读

先看生命周期，再看命令，再看 UI。因为面板和 toolbar 不是孤立 UI，它们依赖 session 先创建 scene runtime、CommandBus、EditorState 和 panels。如果先读 UI 代码，很容易把按钮当成行为本身；按当前代码，按钮只是触发命令或切换轻量 UI 状态的入口。

| 顺序 | 页面 | 解决的问题 |
|---|---|---|
| 1 | [主循环与对象归属](01-main-loop-and-ownership.md) | editor 进程启动后，谁创建并持有哪些对象 |
| 2 | [CommandBus：共享行为的中线](02-command-first-surface.md) | 为什么 UI、Console、API、MCP 都复用命令 |
| 3 | [面板、toolbar 与视口怎样接入命令](03-panels-toolbar-and-viewport.md) | 具体按钮和面板如何连到 CommandBus |
| 4 | [SceneRuntime 与持久化边界](04-scene-runtime-and-persistence.md) | scene document、runtime scene、本地 editor 状态怎样分工 |
| 5 | [API、事件与录制如何观察 editor](05-api-recording-and-observation.md) | 远程自动化如何复用同一套 editor 行为 |

## 核心对象地图

| 对象 | 主要位置 | 角色 |
|---|---|---|
| `LxeEditorSession` | `src/demos/lxe_editor/editor_session.*` | editor 的组合根，持有 runtime、CommandBus、panels、scene interaction 和本地状态 |
| `UiOverlay` | `src/demos/lxe_editor/ui_overlay.*` | ImGui 总入口，绘制 toolbar、stats、help、preferences，并调用 panels |
| `CommandBus` | `src/core/editor/command_bus.*` | 命令注册、dispatch、completion、history、undo/redo |
| `SceneRuntime` | `src/demos/lxe_editor/scene_runtime.*` | 把 `.scene.yaml` 文档构造成运行时 `Scene` |
| `EditorState` | `src/core/editor/editor_state.*` | selection、preview camera、editor camera 等短期 editor 状态 |
| `LxeEditorApiService` | `src/demos/lxe_editor/lxe_editor_api_service.*` | HTTP/WebSocket/MCP 使用的状态快照、命令执行和事件流 |

## 当前边界

这组设计文档描述当前代码事实，不把未来 registry 方案当成已实现能力。未来 toolbar / command metadata 的整理由 [REQ-042-b](../../requirements/042-b-tutorial-editor-extension-registry.md) 跟踪。

## 继续阅读

从 [主循环与对象归属](01-main-loop-and-ownership.md) 开始。
