# REQ-041-d: 编辑器 undo / redo UI 接入 — 工具栏按钮 + 状态栏 + 全局快捷键

> 拆分自 2026-05-06：原 `041-b-editor-polish-v2.md` 早期 R2 段独立成档。把 [REQ-041-b 命令总线 v2](041-b-command-bus-v2.md) 提供的 `bus.undo()` / `bus.redo()` 翻转能力接到 ImGui UI 层，让"上一步是什么 / 下一步可重做什么"对人可见。

## 背景

[REQ-041-b 命令总线 v2](041-b-command-bus-v2.md) 已经把 `bus.undo()` / `bus.redo()` 实现 + 在每个可逆命令上挂 inverse handler。但 v1 编辑器（[REQ-041-a](041-a-imgui-editor-mvp.md)）只有 4 个 dock 面板 + 视口 overlay，没有工具栏 / 状态栏；undo / redo 入口只有控制台命令与全局快捷键，对人不直观——多次拖拽 gizmo 后用户看不到"已积累了几步可撤销 / 当前会撤销什么"。

本 REQ 把 UI 这层接通。**不**重写命令总线 inverse 协议；**不**碰多选 / 框选（[REQ-041-c](041-c-editor-multi-select.md)）；**不**碰工具栏的其他按钮（gizmo mode 切换 / theme toggle 等在 [REQ-041-f](041-f-editor-toolbar-menubar-theme.md) 里）。本 REQ 仅落地 undo / redo 这一对 UI 控件。

## 目标

1. 工具栏 ↶ Undo / ↷ Redo 两个按钮，dispatch `undo` / `redo`
2. 鼠标 hover 时 tooltip 显示"将撤销 X" / "将重做 Y"
3. 全局快捷键 Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z（macOS 风格 redo）路由到同一对 dispatch
4. 状态栏（窗口底部薄条）显示 `undo: 5 / redo: 2` 同步 history 计数

## 需求

### R1: 工具栏 Undo / Redo 按钮

- 在 viewport 上方加一条窄工具栏（高约 32px）；本 REQ 只放 Undo / Redo 两个按钮（其他按钮由 [REQ-041-f](041-f-editor-toolbar-menubar-theme.md) 加）
- 按钮图标：用 ImGui 内嵌字符 ↶ / ↷ 即可（不引入图标资产）；hover 时显示 tooltip
- Undo 按钮按下 → `bus.dispatch("undo")`；Redo → `bus.dispatch("redo")`
- 按钮在 `bus.canUndo() == false` / `bus.canRedo() == false` 时灰显（disabled）

### R2: 工具栏按钮 tooltip

- hover Undo 按钮时 tooltip 显示：`"撤销: " + bus.peekUndoCommand()`
- hover Redo 按钮时 tooltip 显示：`"重做: " + bus.peekRedoCommand()`
- `peekUndoCommand()` / `peekRedoCommand()` 是本 REQ 在 `CommandBus` 上加的小读接口（栈顶字符串，不出栈）
- 空栈时 tooltip 显示 `"撤销: (无)"` / `"重做: (无)"`

### R3: 全局快捷键

- 全局监听（ImGui main viewport 拥有键盘焦点时生效）：
  - `Ctrl+Z` → `bus.dispatch("undo")`
  - `Ctrl+Y` → `bus.dispatch("redo")`
  - `Ctrl+Shift+Z`（macOS 习惯 redo）→ `bus.dispatch("redo")`
- 控制台 input 拥有焦点时（v1 R5 console panel）→ `Ctrl+Z` 让给 input；编辑器全局焦点回退时再生效（避免抢编辑撤销）
- 快捷键表统一注册在 [REQ-041-a R8](041-a-imgui-editor-mvp.md) 的 keymap 模块；本 REQ 在该模块里加 3 行

### R4: 状态栏

- 在主窗口底部加一条薄状态栏（高约 24px）
- 默认内容：`undo: <bus.undoStackSize()> / redo: <bus.redoStackSize()>`
- 计数与 `bus` 内部状态同步刷新（每帧重读，不订阅事件）
- 状态栏的其他字段（FPS / mode / current path 等）**不**在本 REQ 范围；本 REQ 只占 undo / redo 计数这一段；其他字段由 [REQ-041-f](041-f-editor-toolbar-menubar-theme.md) 或后续 REQ 接入

### R5: 测试覆盖

`src/test/integration/test_editor_undo_redo_ui.cpp`（新；走 `dispatchScript` + 模拟工具栏点击事件）：

- 执行 3 个可逆命令后 `bus.undoStackSize() == 3`；状态栏读到 `undo: 3 / redo: 0`
- 工具栏 Undo 按钮 dispatch 与控制台 `undo` 命令产生同一 history 状态
- Ctrl+Z 在 console input 有焦点时**不**触发 bus.undo（焦点退让）；焦点回退后再 Ctrl+Z 触发
- canUndo == false 时 Undo 按钮灰显（disabled）

## 修改范围

- `src/core/editor/toolbar.{hpp,cpp}`（新；本 REQ 仅放 Undo / Redo 两个按钮，其他按钮等 [REQ-041-f](041-f-editor-toolbar-menubar-theme.md)）
- `src/core/editor/status_bar.{hpp,cpp}`（新；undo / redo 计数）
- `src/core/editor/command_bus.hpp` / `.cpp`（在 [REQ-041-b](041-b-command-bus-v2.md) 基础上加 `peekUndoCommand` / `peekRedoCommand` / `canUndo` / `canRedo` / `undoStackSize` / `redoStackSize` 6 个读接口）
- `src/core/editor/keymap.cpp`（注册 Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z）
- `src/test/integration/test_editor_undo_redo_ui.cpp`（新）

## 边界与约束

- **不**实现 undo / redo 业务逻辑；逻辑在 [REQ-041-b R2](041-b-command-bus-v2.md)
- **不**实现 history 面板（"看完整命令列表"）；如果有需求，等多步撤销 v1 跑过再立项
- 状态栏的其他字段（FPS / 当前 mode / 选中节点 path 等）**不**在本 REQ；只占 undo/redo 计数这一段
- 工具栏的其他按钮（gizmo mode 切换 / theme toggle / preview F 等）**不**在本 REQ；见 [REQ-041-f](041-f-editor-toolbar-menubar-theme.md)
- macOS 习惯的 `Cmd+Z / Cmd+Shift+Z`：v1 用 Ctrl 而非 Cmd（GLFW / SDL3 在 macOS 下默认把 Cmd 报为 Super）；macOS 平台特例留到真出现 macOS 用户后立项

## 依赖

- [REQ-041-a ImGui Editor MVP](041-a-imgui-editor-mvp.md) — keymap 模块 + main viewport 焦点逻辑
- [REQ-041-b 命令总线 v2](041-b-command-bus-v2.md) — undo / redo 业务逻辑 + history stack；本 REQ 在它之上加几个读接口

## 后续工作

- 完整 history 面板（list view + 跳转任意点）：等多步撤销 v1 跑过出现真实需求再立项
- macOS Cmd+Z 习惯支持：等真出现 macOS 用户再立项

## 实施状态

待实施。立项窗口：[REQ-041-b 命令总线 v2](041-b-command-bus-v2.md) 落地后开工。本 REQ 与 [REQ-041-c 多选 / 框选](041-c-editor-multi-select.md) 互不依赖，可并行推进。
