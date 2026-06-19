# 主循环与对象归属：先看谁搭起工作台

理解 editor 先从“谁创建谁”开始。`lxe_editor` 的入口不是某个 panel，而是 `main.cpp` 里把窗口、renderer、session、API service 和 `EngineLoop` 接起来的启动流程。`LxeEditorSession` 则是 editor 内部的组合根，负责把 scene runtime、CommandBus、panels 和 interaction controller 重新绑定到同一份 scene 上。

## 进程启动时先搭外壳

`src/editor/main.cpp` 负责外层生命周期：

| 阶段 | 当前代码中的对象 | 作用 |
|---|---|---|
| 创建 session | `LxeEditorSession session(...)` | 管理 editor 内部对象 |
| 启动 API | `LxeEditorApiServer` / `LxeEditorApiService` | 暴露命令、状态和事件 |
| 注册 UI 回调 | `vulkanRenderer->setDrawUiCallback(...)` | 每帧绘制 ImGui editor |
| 启动主循环 | `EngineLoop loop` | 更新窗口、renderer 和当前 scene |
| 进入 scene | `loop.startScene(session.scene())` | 把 session 当前 runtime scene 交给 engine loop |

这里的关键是：renderer 不直接知道 toolbar 或 Inspector 的业务规则。UI 每帧被 draw callback 调用，真正的 editor 行为在 session 绑定的命令、panel 和 runtime 对象里。

## 外壳和工作台之间有一条清楚的边界

`main.cpp` 更像进程外壳，`LxeEditorSession` 更像工作台内部。两者的边界可以这样看：

| 问题 | `main.cpp` 负责 | `LxeEditorSession` 负责 |
|---|---|---|
| 进程怎样启动 | 解析环境、创建窗口/renderer/API server | 不负责 |
| 当前 scene 是谁 | 把 session scene 交给 `EngineLoop` | 创建、加载、替换 runtime scene |
| UI 每帧什么时候画 | 注册 renderer draw UI callback | 给 `UiOverlay` 提供 panel、state、commands |
| API 怎样拿到状态 | 创建 API service，并接 hooks | 提供 project、scene、toolbar、recording 状态 |
| command 结果如何影响 loop | 在 update hook 中调用 session polling | 从 command metadata 推导 dirty/rebuild/quit |

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

## 为什么 CommandBus 不跟着 scene 一起重建

`rebuildBindings()` 会复用已有 `CommandBus`，再重新注册 handler。这样 Console history、API 观察和 undo/redo 这类命令层状态不会因为 scene runtime 切换而丢失；但 handler 捕获的 `Scene`、`SceneRuntime`、panel callback 会更新到新对象。

这是一条很重要的 ownership 规则：

| 对象 | scene 切换后怎样处理 | 原因 |
|---|---|---|
| `SceneRuntime` | 替换 | 当前运行场景已经变了 |
| panels / interaction controller | 重建 | 它们持有当前 scene 或 runtime callback |
| `CommandBus` | 保留 bus，更新 handler | 命令历史和外部入口应该连续 |
| `EditorState` | 保留对象，重绑 camera/selection | preview、selection 等状态要按新 scene 修正 |

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
