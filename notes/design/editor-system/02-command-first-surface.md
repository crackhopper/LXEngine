# CommandBus：共享行为的中线

CommandBus 像工作台下面的一条中线。toolbar、Inspector、Scene Tree、Console、HTTP API 和 MCP 不应该各自实现一遍“选择节点”“移动节点”“保存场景”；它们把意图变成 command line，再交给同一个 bus 分发。

## CommandBus 提供什么

`src/editor/commands/command_bus.hpp` 定义了这条中线的核心能力：

| 能力 | 当前接口 | 作用 |
|---|---|---|
| 注册命令 | `registerHandler(...)` | 把 verb 绑定到 handler |
| 注册补全 | `registerCompleter(...)` | 给 Console 和自动化入口提供候选 |
| 执行命令 | `dispatch(line)` | 解析一行文本并调用 handler |
| 执行脚本 | `dispatchScript(text)` | 批量执行多行命令 |
| 历史记录 | `history()` | 让 Console、API event、session polling 观察命令结果 |
| 撤销重做 | `undo()` / `redo()` | 通过 inverse line 管理 undo/redo stack |

`CommandResult` 不只返回成功与否，还能带 `structured` JSON 和 metadata。metadata 是 session 后处理的关键，例如 scene rebuild、camera resync、quit、undo/redo 清理策略。

## 一行命令包含三种信息

命令行像一张小工单。它既要给人能读懂，也要让 API/recording 能稳定复用。

```text
set /helmet.nodeMaterial.roughness 0.35
│   │                         └─ 参数值
│   └─ scene node path + material 参数字段
└─ verb
```

当前 parser 仍是轻量文本协议，不是完整 shell。设计上我们优先让命令足够稳定、可补全、可录制，而不是追求复杂表达式。

## 两类命令在哪里注册

当前 editor 命令分成两层：

| 层 | 注册函数 | 例子 | 为什么放这里 |
|---|---|---|---|
| 通用 editor/scene 命令 | `registerBuiltinCommands(...)` | `select`、`move`、`add`、`scene save`、`preview`、`undo`、`redo` | 位于 `src/editor/commands/`，服务 editor 行为 |
| `lxe_editor` 专属命令 | `registerLxeEditorCommands(...)` | `mode`、`state`、`pick`、`debug`、`recording`、`display ...` | 依赖 `UiOverlay`、`SceneInteractionController`、runtime display hooks |

这层分工让 core editor 命令不直接依赖 demo app 的 API service、display 选择或 recording controller。

## 一条 UI 操作怎样经过命令中线

以 toolbar 的 Preview 按钮为例：

1. `UiOverlay::drawToolbarPanel()` 画出 Preview toggle。
2. 点击按钮后调用 `m_commandBus.dispatch("preview toggle")`。
3. `registerBuiltinCommands(...)` 注册的 `preview` handler 修改 `EditorState::previewEnabled`。
4. `LxeEditorSession::pollCommandHistory(...)` 观察命令历史。
5. API service 在 `observeStateChanges()` 中看到 toolbar snapshot 变化，发出 `PreviewChanged` event。

这条链路说明了 command-first 的价值：同一个 `preview toggle` 可以来自 toolbar、hotkey、Console、HTTP API 或 MCP，最终结果一致。

## 命令结果怎样影响 session

CommandBus 不直接拥有 `EngineLoop`，所以一些全局副作用由 session 观察命令历史后执行：

| metadata / line | `pollCommandHistory()` 的处理 |
|---|---|
| mutating verb，如 `move`、`set`、`add`、`remove` | 标记 scene dirty |
| `scene.rebuild=true` | 调用 `loop.requestSceneRebuild()` |
| `editor_camera.resync=true` | 调用 `CameraRig::resyncFromAttachedCamera()` |
| `editor.quit=true` | 调用 `loop.stop()` |

这让 handler 保持局部：它描述“发生了什么”，session 决定这些结果如何作用到主循环。

`scene.rebuild=true` 只能用于结构性变化，例如新增/删除节点、替换会改变 pass 参与或
资源选择的组件、改变材质 URI 这类会影响 render work 的事实。light transform /
direction / intensity、camera 矩阵、材质参数值、render feature runtime value 这类
参数变化只是 runtime/volatile 数据更新，必须通过 dirty resource upload 进入后端，
不能请求 scene rebuild 或 FrameGraph rebuild。

## structured JSON 给机器看，message 给人看

`CommandResult` 同时服务 Console 和远程工具：

| 字段 | 读者 | 当前用途 |
|---|---|---|
| `ok` | 人和机器 | 判断命令是否成功 |
| `message` | 人 | Console / 日志里的简短反馈 |
| `structured` | 机器 | API、MCP、测试读取的 JSON payload |
| `metadata` | session / bus | dirty、rebuild、inverse、undo/redo 策略 |

这也是为什么 command-first 比“UI 直接改对象”更适合后续 Web Editor 和 agent：同一条命令天然带有人类反馈、机器反馈和后处理信号。

## 当前边界

当前 command metadata 已经有 `brief`、`inverse`、`mutatesState` 等基础，但 toolbar action 还没有统一 metadata registry。toolbar 按钮仍在 `UiOverlay` 中手写。新增入口时按当前合同手工同步 handler、completion、toolbar dispatch、API snapshot 和测试。

## 继续阅读

- [面板、toolbar 与视口怎样接入命令](03-panels-toolbar-and-viewport.md)
- [API、事件与录制如何观察 editor](05-api-recording-and-observation.md)
