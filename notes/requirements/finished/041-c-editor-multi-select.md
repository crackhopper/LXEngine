# REQ-041-c: 编辑器多选 / 框选 — scene tree 多选 + 视口拖拽框选

> 拆分自 2026-05-06：原 `041-b-editor-polish-v2.md` 早期版本（kitchen-sink "v2 polish"）含 5 件不相关的事；评审后按 README "一个 REQ = 一个连续实施周期" 拆开。本档只留多选 / 框选；undo·redo UI 移到 [REQ-041-d](../041-d-editor-undo-redo-ui.md)；Rename / Duplicate 移到 [REQ-041-e](../041-e-editor-node-rename-duplicate.md)；工具栏 / 菜单栏 / 主题切换移到 [REQ-041-f](../041-f-editor-toolbar-menubar-theme.md)。

## 背景

[REQ-041-a ImGui Editor MVP](041-a-imgui-editor-mvp.md) 单选编辑路径已通畅，但实战中复杂场景常需要"一次选中一组节点统一移动 / 改 layer / 删除"；当前只能逐个 select，再发命令重复几次。

[REQ-041-b 命令总线 v2](041-b-command-bus-v2.md) 落地后，`EditorState` 已升级到节点集合、`select <p1> <p2> ...` 已支持多 path、gizmo 拖拽 delta 已能应用到所有 selected。本 REQ 把 UI 这一层接通：scene tree 面板的 modifier-key 多选 + 视口拖拽矩形框选 + 多 selected 视觉化。

## 目标

1. scene tree 面板：Ctrl+Click 加入选区；Shift+Click 选区到光标项之间所有兄弟节点；普通 Click 替换选区
2. 视口：空白处按下 + 拖拽 → 半透明矩形 → 释放时把命中节点合并发 `select <p1> <p2> ...` 命令
3. 多 selected 视觉化：所有 selected 都画 wireBox（DebugDraw）；primary selected（最后加入项）颜色更亮 + gizmo 挂在它身上
4. 防误操作：矩形面积超过 viewport 50% 时弹二次确认

## 需求

### R1: scene tree 面板多选

- `Ctrl+Click` 节点 → dispatch `select` 命令把节点 path 加入当前选区（[REQ-041-b R3 selectAdd](041-b-command-bus-v2.md)）
- `Shift+Click` 节点 → 在 scene tree 当前展开层级上，从 primary selected 到光标项之间的所有兄弟节点一起加入选区（典型 IDE / 文件管理器语义）
- 普通 `Click` → 整体替换选区为单节点（与 v1 行为一致）
- selected 节点显示：背景高亮；primary selected 用更明显的边框

### R2: 视口拖拽框选

- 视口空白处按下 + 拖拽 → 渲染一个半透明矩形（ImGui draw list，不走 DebugDraw）
- 释放时：遍历 scene 中所有 mesh-bearing SceneNode，把其 world AABB 投影到视口屏幕区，与矩形相交即计入命中
- 命中集合非空 → dispatch `select <p1> <p2> ...`；为空 → dispatch `deselect`
- ImGuizmo 鼠标 hover 优先于框选（避免拖 gizmo 时误开框选）—— 与 v1 R5 picking 优先级约定一致
- 蒙皮 mesh 用 bind pose AABB（与 [REQ-041-h](../041-h-mesh-level-triangle-picking.md) 边界一致）

### R3: 多 selected 视觉化

- 每帧遍历 `EditorState::getSelected()`，对每个节点调 `DebugDraw::wireBox(node->getWorldBounds(), color)`
  - non-primary selected：默认 yellow
  - primary selected：默认 bright cyan + 略粗（DebugDraw v1 不支持线宽，则用更亮颜色 + 内外双线表达"更突出"）
- gizmo 挂在 `getPrimarySelected()` 的 transform 上；拖拽 delta 由 [REQ-041-b](041-b-command-bus-v2.md) 多目标命令机制（`move /a /b 1 0 0`）应用到所有 selected

### R4: 框选过大时二次确认

- 矩形屏幕面积 > viewport 面积的 50% → 释放时不直接 dispatch `select`，先弹一个 ImGui modal："框选了 N 个节点，确认全选?"
- 确认 → dispatch；取消 → 不 dispatch；阈值 50% 写在 `EditorConfig::boxSelectConfirmThreshold`，可调

### R5: 测试覆盖

`src/test/integration/test_editor_multi_select.cpp`（新；走 `dispatchScript` + 模拟点击事件）：

- `select /a` 后 Ctrl+Click `/b` → `getSelected().size() == 2`，primary = `/b`
- Shift+Click 跨 3 个兄弟节点 → 选区含全部 3 个
- 框选 viewport 含 3 个 mesh → dispatch 后选区含 3 个
- 框选半屏面积超阈值 → modal 弹出；cancel 后选区不变
- gizmo 拖拽时多 selected 都按 delta 移动（单点入口验证）

## 修改范围

- `src/core/editor/scene_tree_panel.cpp`（modifier-key 多选）
- `src/core/editor/viewport_overlay.cpp`（框选矩形 + 多 wireBox + modal 弹窗）
- `src/core/editor/editor_config.{hpp,cpp}`（新 / 扩展；`boxSelectConfirmThreshold`）
- `src/test/integration/test_editor_multi_select.cpp`（新）

## 边界与约束

- 框选**不**做"模式切换"（Add / Subtract / Intersect）；v2 只做 Replace，模式切换留 v3 等真实需求
- **不**做"框选时按住 Alt 反选"；同上理由
- 框选**不**穿透墙：仅看 world AABB 投影面是否与矩形相交；命中三角面级精度需要 [REQ-041-h](../041-h-mesh-level-triangle-picking.md) 的 mesh 数据 + 投影裁剪，留后续
- 命令最终走总线（与 v1 R9 硬约束一致）；UI 不绕过 dispatch

## 依赖

- [REQ-041-a ImGui Editor MVP](041-a-imgui-editor-mvp.md) — 4 面板 + viewport overlay 框架
- [REQ-041-b 命令总线 v2](041-b-command-bus-v2.md) — `EditorState` 多选 API + `select <p1> <p2> ...` 多 path 协议
- [REQ-039-a DebugDraw](039-a-debug-draw-subsystem.md) — `wireBox` 多 selected 视觉化

## 后续工作

- [REQ-041-d undo/redo UI](../041-d-editor-undo-redo-ui.md) — 撤销批量 select / 框选操作
- 框选模式（Add / Subtract / Intersect）：等多选 v1 跑过实战再立项
- 命中三角面级框选：等 [REQ-041-h](../041-h-mesh-level-triangle-picking.md) 落地后再考虑

## 实施状态

已实现，已验证，已归档（2026-05-08）。

- R1：scene tree 已支持普通 Click 替换、Ctrl+Click 加选、Shift+Click 同级区间选；本轮补齐 primary selected 专属高亮边框
- R2：viewport 已支持拖拽框选、AABB 投影命中、普通 replace / Ctrl+Shift append 选区
- R3：多选线框已区分 primary / non-primary；gizmo 继续挂 primary，批量变换沿用 041-b 多目标命令
- R4：大面积框选已接入 `EditorConfig::boxSelectConfirmThreshold` 与确认 modal
- R5：`test_scene_tree_panel` / `test_editor_multi_select` / `test_viewport_overlay` / editor focused 回归已覆盖核心路径

本轮验证（2026-05-08）：

- `cmake --build build --target demo_scene_viewer`
- `cmake --build build --target BuildTest test_scene_tree_panel test_editor_multi_select test_viewport_overlay`
- `ctest --test-dir build --output-on-failure -L auto -LE requires_video_device`
- `ctest --test-dir build --output-on-failure -R 'test_(scene_tree_panel|editor_multi_select|viewport_overlay|inspector_panel|gizmo_adapter|scene_viewer_layout|command_bus|command_bus_v2|debug_ui_smoke)$'`
