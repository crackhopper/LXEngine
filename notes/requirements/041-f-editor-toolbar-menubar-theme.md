# REQ-041-f: 编辑器 chrome — 菜单栏 + 工具栏其余按钮 + 主题切换

> 拆分自 2026-05-06：原 `041-b-editor-polish-v2.md` 早期 R3 + R4 段独立成档。把 [REQ-041-d](041-d-editor-undo-redo-ui.md) 已经引入的工具栏 / 状态栏外壳填满（gizmo mode / preview / theme），同时加顶部菜单栏（File / Edit / View / Help）。本 REQ 是结构性 chrome polish，与编辑器其他 v2 子项正交，可独立推进。

## 背景

[REQ-041-a v1](041-a-imgui-editor-mvp.md) 只有 4 个 dock 面板 + 视口 overlay，没有顶部菜单栏。新建场景 / 切换主题 / 切换 gizmo mode / 显示 stats / 触发命令脚本等高频功能没有视觉入口，全靠快捷键 + 控制台。

[REQ-041-d undo/redo UI](041-d-editor-undo-redo-ui.md) 已经把工具栏与状态栏外壳引入，但只放了 Undo / Redo 两个按钮。本 REQ 把工具栏填满（gizmo mode / theme toggle / preview F / stats panel 切换），同时加菜单栏（File / Edit / View / Help）。

主题切换：ImGui 默认 dark 在白天 / 高对比环境不舒服；需要 light + dark 切换且与系统偏好同步。这是一个独立小功能，但与"工具栏放 toggle 按钮"自然合并。

## 目标

1. 顶部菜单栏：File / Edit / View / Help 四个一级菜单
2. 工具栏：在 [REQ-041-d](041-d-editor-undo-redo-ui.md) 的 Undo / Redo 之外，再加 gizmo mode 切换（W/E/R）/ theme toggle / preview F / stats panel toggle
3. 主题切换：light / dark / follow system；切换后 ImGui style + DebugDraw 默认颜色一并更新
4. 所有 menu / 工具栏按钮**必须**走命令总线，**不**给 UI 提供绕过 dispatch 的捷径

## 需求

### R1: 顶部菜单栏

```text
File  Edit  View  Help
```

| 一级 | 二级 |
|---|---|
| File | New / Open / Save（v2 仅占位 disabled，落地等 [Phase 3 资产管线](../roadmaps/main-roadmap/phase-3-asset-pipeline.md) 引入 asset 索引）/ Quit |
| Edit | Undo / Redo / Duplicate / Delete / Select All / Deselect |
| View | Toggle Console / Toggle Inspector / Toggle Scene Tree / Reset Layout / Theme → Light / Dark / Follow System |
| Help | About / Open Docs（外部 URL，调系统 default browser） |

- 每项绑定一条已注册的命令；点击后 dispatch
- File 菜单的 New / Open / Save 在本 REQ 仅占位（disabled），不调度任何命令；landing 等资产管线
- Quit 发 `quit` 命令（命令总线注册一条新 verb，handler 触发 EngineLoop 优雅退出）

### R2: 工具栏其余按钮

[REQ-041-d](041-d-editor-undo-redo-ui.md) 已加 ↶ ↷；本 REQ 在它右侧填：

```text
[↶] [↷] | [W][E][R] | [Theme] [F] | [Stats]
```

- `[W][E][R]` gizmo mode 切换（Translate / Rotate / Scale）；点击发 `gizmo translate` / `gizmo rotate` / `gizmo scale`（新命令，保持与快捷键 W/E/R 同入口）；当前 mode 用 active 状态视觉化
- `[Theme]` 一个图标按钮；点击循环 light → dark → follow → light；同 R3
- `[F]` preview 切换；点击发 `preview toggle`（v1 已有），与快捷键 F 同入口
- `[Stats]` 切换底部 stats 面板（FPS / drawcall / vertex 数等）的可见性；发 `view stats toggle`（新 verb）

### R3: 主题切换

- 主题枚举 `enum class EditorTheme { Light, Dark, FollowSystem }`，存在 `EditorState`
- 切换路径（任一发同一条命令）：
  - 菜单 View → Theme → Light/Dark/Follow
  - 工具栏 [Theme] 按钮（循环切换）
  - 控制台命令 `set theme <light|dark|follow>`
- 切换实现：
  - `Light` / `Dark`：调 ImGui 自带 `StyleColorsLight()` / `StyleColorsDark()`
  - `FollowSystem`：Linux 读 `gsettings get org.gnome.desktop.interface gtk-theme`；macOS / Windows 留 detect-failed-fallback-Dark
  - 同步切换 DebugDraw 默认颜色（让线框在亮 / 暗背景下都可见）—— 仅切默认色，不动调用方显式传入的 Color

### R4: 状态栏其余字段

[REQ-041-d](041-d-editor-undo-redo-ui.md) 在状态栏占了 undo / redo 计数；本 REQ 加：

- FPS（每秒刷一次）
- 当前 gizmo mode（Translate / Rotate / Scale）
- 当前 primary selected path（截断到 60 字符 + ellipsis）
- 主题状态（"Light" / "Dark" / "Follow: Light"）

每段用 `|` 分隔。所有读取每帧从 `EditorState` / `bus` / `Clock` 同步刷新。

### R5: 测试覆盖

`src/test/integration/test_editor_chrome.cpp`（新；走 `dispatchScript` + 模拟菜单点击）：

- `set theme light` → ImGui colors 命中 Light 偏好
- `set theme dark` → Dark
- `set theme follow` → 平台检测；Linux GNOME light 主题下命中 Light
- 菜单 File → Quit → 引擎 loop 正确退出（exit code 0）
- 工具栏 [W] 按下 → gizmo mode == Translate；与快捷键 W 等价
- 状态栏 FPS 字段每秒至少刷新 1 次（不卡死）

## 修改范围

- `src/core/editor/menu_bar.{hpp,cpp}`（新）
- `src/core/editor/toolbar.cpp`（在 [REQ-041-d](041-d-editor-undo-redo-ui.md) 已建文件上扩展按钮）
- `src/core/editor/status_bar.cpp`（在 [REQ-041-d](041-d-editor-undo-redo-ui.md) 已建文件上扩展字段）
- `src/core/editor/theme.{hpp,cpp}`（新；切换实现 + 平台检测）
- `src/core/editor/commands/theme.cpp` / `gizmo.cpp` / `view.cpp` / `quit.cpp` / `select_all.cpp`（新命令）
- `src/core/editor/editor_state.hpp`（加 `EditorTheme` 枚举与字段）
- `src/test/integration/test_editor_chrome.cpp`（新）

## 边界与约束

- File 菜单的 New / Open / Save 仅占位；asset I/O 落地等 [Phase 3 资产管线](../roadmaps/main-roadmap/phase-3-asset-pipeline.md)
- **不**做布局序列化（保存 / 还原 dock 位置）；ImGui 自带的 `imgui.ini` 即可，编辑器不维护额外 layout 文件
- **不**做插件 / 自定义面板 / 自定义工具栏架构；4 个 dock 面板 + 工具栏 + 菜单栏 + 状态栏是固定架构
- 主题切换的"DebugDraw 默认颜色"：仅切默认色（grid / axis / wireBox 默认色），不动调用方显式传入的 Color 参数
- About / Open Docs：URL 写死在源码里（指 LXEngine notes site），不读配置文件

## 依赖

- [REQ-041-a ImGui Editor MVP](041-a-imgui-editor-mvp.md) — 4 面板 + viewport overlay + keymap
- [REQ-041-d undo/redo UI](041-d-editor-undo-redo-ui.md) — 工具栏 / 状态栏外壳；本 REQ 在它之上扩展按钮 / 字段
- [REQ-041-b 命令总线 v2](041-b-command-bus-v2.md) — 新命令（`gizmo` / `view` / `quit` / `theme` 等）的 dispatch / inverse / structured 路径

## 后续工作

- 文件对话框 asset browser：等 [Phase 3 资产管线](../roadmaps/main-roadmap/phase-3-asset-pipeline.md) 引入 asset 索引后立项；File 菜单的 Open / Save 那时再启用
- 自定义工具栏 / 插件系统：等编辑器使用频次提升 + 出现明确扩展需求再立项
- 主题：用户自定义颜色方案 / 字体大小：等多人使用编辑器再立项

## 实施状态

待实施。立项窗口：[REQ-041-d](041-d-editor-undo-redo-ui.md) 落地后开工（本 REQ 在它建立的工具栏 / 状态栏外壳上加按钮 / 字段）。本 REQ 与 [REQ-041-c](finished/041-c-editor-multi-select.md) / [REQ-041-e](041-e-editor-node-rename-duplicate.md) 正交，可独立推进。
