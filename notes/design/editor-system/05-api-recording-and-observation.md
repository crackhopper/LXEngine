# API、事件与录制如何观察 editor

远程自动化不是另一套 editor。HTTP、WebSocket、MCP 和 recording 都像接在同一个工作台上的观察窗和远程按钮：按钮仍然按到 CommandBus 上，观察窗通过 snapshot 和 event 读出同一份 session 状态。

## API 为什么暴露 project 而不是文件来源

当前 editor 的工作单元是 project，所以 API 状态也以 project 为顶层上下文。外部工具需要知道“当前在哪个 project、哪个 active scene”，而不是猜这个 scene 来自哪个文件来源。

| API 字段 | 来源 | 意义 |
|---|---|---|
| `scene.sceneName` | `SceneRuntime` / runtime `Scene` | 当前已经绑定到 runtime 的 scene 名称 |
| `scene.dirty` | `LxeEditorSession::isDirty()` | 当前 project 或 active scene 是否有未保存变化 |
| `project.id` | `ProjectDocument::id` | 当前 project 身份 |
| `project.path` | `ProjectSession::projectRoot()` | project 文件夹 |
| `project.activeScene` | `ProjectDocument::activeScene` | project metadata 记录的当前 scene |

`main.cpp` 通过 `LxeEditorApiService::Hooks` 把这些字段接出来。API service 自己不拥有 project 或 scene，只负责把 session 的事实序列化。

## API service 是观察层，不是第二个 session

`LxeEditorApiService` 的构造参数很能说明边界：

| 输入 | 意义 |
|---|---|
| `CommandBus&` | 远程执行仍走同一条命令线 |
| `EditorState&` | 读取 selection、preview、camera 等 editor 状态 |
| `Scene&` | 订阅 runtime scene event |
| `Hooks` | 从 session 读取 project、scene、toolbar、recording、display 状态 |

它不复制 session，也不缓存业务对象。重新打开 scene 后，外层会用新的 scene 和 hooks 重建 API service，事件游标和历史观察位置则按构造逻辑延续。

## 命令事件如何从 project/scene 命令生成

事件流来自三类观察：

| 事件来源 | 当前处理 |
|---|---|
| command history | `observeCommandHistory()` 生成 `command.executed`，并识别 `project init/open/save/close` 与 `scene save` |
| runtime scene event | `observeRuntimeSceneEvent()` 收集 runtime `scene_node.changed` |
| state snapshot diff | `observeStateChanges()` 比较 selection、toolbar、preview、dirty 和 runtime-loaded scene key |

`active_scene.changed` 只在 runtime scene key 变成新的非空值时产生。这样 `scene open` 刚把 project metadata 切过去、但 runtime 还没完成绑定时，不会提前通知外部工具。

## 录制如何保持可重放

Recording controller 记录的是 command step，而不是像素级 UI 操作。只要 toolbar、Console、HTTP、MCP 都走 CommandBus，录制文件就能表达“我们执行了哪些 editor 行为”。

| 录制阶段 | 当前含义 |
|---|---|
| `recording start` | 开始收集后续 command step，并记录当时的 scene path metadata |
| command step | 保存 `{ "line": "..." }`，回放时重新 dispatch |
| `recording stop save` | 把 metadata、steps、build info 写到 `data/lxe_editor/recordings/` |
| replay | 读取 steps，逐条 dispatch command；失败时可用 probe 观察当前 state |

如果录制从 project 创建前开始，metadata 的 scene path 可以为空；可重放性来自 steps 中的 `project init`、`scene open`、编辑命令和 `scene save`。

## 录制文件记录意图，不记录 UI 像素

录制 step 的核心是 command line：

```json
{
  "line": "move /helmet 1 0 0"
}
```

这意味着 replay 不关心当时按钮在哪里、鼠标移动了多少像素。只要 command surface 保持稳定，同一份录制可以来自 Console、toolbar、HTTP 或 MCP，也可以在 headless 诊断里重放。

## MCP/HTTP 如何共用同一套命令语义

HTTP endpoint、WebSocket 和 MCP 诊断都复用 `LxeEditorApiService`。这层只做三件事：

| 通道 | 复用点 |
|---|---|
| HTTP `/api/command` | 调用 `executeCommand(...)`，走同一个 CommandBus |
| HTTP `/api/state/*` | 调用 `captureState()`，读取同一份 project / scene / toolbar / selection snapshot |
| WebSocket | 发送 `collectEventsSince(...)` 得到的同一批 API events |
| MCP manager tools | 通过 manager 间接调用 editor HTTP/API 能力 |

因此 user、测试和 agent 不需要维护三套行为规则。我们只要让 command surface 正确，几条入口就会自然对齐。

## 当前 API 不是 Phase 10 的完整 engine MCP

当前 `lxe_editor` 已经有 HTTP/WebSocket/API service，并且 manager 可以通过这些能力做远程诊断。但 roadmap 里的 Phase 10 是更大的 engine-level MCP / CLI / agent：它要求标准 MCP tools/resources/prompts、headless CLI、agent runtime、权限和成本模型。

这部分尚未实现，设计边界仍停留在 pending `REQ-044-b`，不属于 0.2.0-pre 当前实现。

## 继续阅读

- [CommandBus：共享行为的中线](02-command-first-surface.md)
- [SceneRuntime 与持久化边界](04-scene-runtime-and-persistence.md)
- [Roadmap 边界：哪些是未来能力](06-roadmap-boundaries.md)
