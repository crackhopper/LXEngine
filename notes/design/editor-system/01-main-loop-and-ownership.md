# 主循环与对象归属：先看谁搭起工作台

理解 editor 先从“谁创建谁”开始。`lxe_editor` 的入口不是某个 panel，而是 `main.cpp` 里把窗口、renderer、session、API service 和 `EngineLoop` 接起来的启动流程。`LxeEditorSession` 则是 editor 内部的组合根，负责把 scene runtime、CommandBus、panels 和 interaction controller 重新绑定到同一份 scene 上。

## 进程启动时先搭外壳

`src/demos/lxe_editor/main.cpp` 负责外层生命周期：

| 阶段 | 当前代码中的对象 | 作用 |
|---|---|---|
| 创建 session | `LxeEditorSession session(...)` | 管理 editor 内部对象 |
| 启动 API | `LxeEditorApiServer` / `LxeEditorApiService` | 暴露命令、状态和事件 |
| 注册 UI 回调 | `vulkanRenderer->setDrawUiCallback(...)` | 每帧绘制 ImGui editor |
| 启动主循环 | `EngineLoop loop` | 更新窗口、renderer 和当前 scene |
| 进入 scene | `loop.startScene(session.scene())` | 把 session 当前 runtime scene 交给 engine loop |

这里的关键是：renderer 不直接知道 toolbar 或 Inspector 的业务规则。UI 每帧被 draw callback 调用，真正的 editor 行为在 session 绑定的命令、panel 和 runtime 对象里。

## Session 是 editor 内部的组合根

`LxeEditorSession` 持有 editor 内部的主要状态：

| 成员 | 角色 |
|---|---|
| `ProjectSession m_projectSession` | 当前 project、project root、active scene、project dirty 状态 |
| `SceneRuntime m_runtime` | 当前运行中的 scene、editor camera、game camera、scene document 读写 |
| `CommandBus m_commandBus` | 所有关键 editor 行为的共享入口 |
| `ConsolePanel` / `SceneTreePanel` / `InspectorPanel` / `ViewportOverlay` | 面板和视口交互层 |
| `SceneInteractionController` | 鼠标点击、框选、debug hit point 等场景交互 |
| `EditorConfigDocument` / `EditorDataDocument` | 本地 UI 配置和 console history |
| `RecordingController` | editor 录制状态与记录文件 |

我们可以把 session 想成工作台的配电箱：它不一定亲自画每个按钮，但它决定哪些对象接到同一条命令线路上。

## rebuildBindings 是重新接线点

加载新 scene 后，runtime scene 会替换。此时旧 panel 不能继续引用旧 scene，所以 `LxeEditorSession::rebuildBindings()` 会重新创建或绑定这些对象：

1. 更新 `EditorState` 中的 editor camera / preview camera。
2. 让 `CameraRig` attach 到新的 editor camera。
3. 确保 `CommandBus` 存在，并注册 builtin commands。
4. 创建 `ConsolePanel`、`SceneTreePanel`、`InspectorPanel`、`ViewportOverlay`。
5. 创建 `SceneInteractionController`，并把框选转交给 `ViewportOverlay`。
6. 注册 `lxe_editor` 专属命令，例如 mode、debug、recording、state、pick。
7. 调用 `UiOverlay::attach(...)`，把 rig、bus、state、config、panels 和 callbacks 接到 UI。

这就是 editor 系统最重要的因果关系：scene runtime 改变后，所有引用 scene 的 UI 和 interaction 对象都要重新接到新 scene 上。

## 主循环每帧做什么

主循环中的 editor 工作分成两类：

| 类别 | 当前入口 | 说明 |
|---|---|---|
| 绘制 UI | `UiOverlay::drawFrame(...)` | 画 toolbar、panels、stats、help、preferences |
| 观察命令结果 | `LxeEditorSession::pollCommandHistory(...)` | 根据 command metadata 标记 dirty、请求 scene rebuild、resync camera、quit |

CommandBus 执行命令时只返回结果和 metadata；session 在每帧 polling 时把这些结果翻译成 engine loop 或 session 状态变化。这样 command handler 不需要直接持有 `EngineLoop`。

## 继续阅读

- [CommandBus：共享行为的中线](02-command-first-surface.md)
- [面板、toolbar 与视口怎样接入命令](03-panels-toolbar-and-viewport.md)
