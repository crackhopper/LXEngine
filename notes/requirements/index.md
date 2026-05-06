# 需求（进行中）

本目录由 `scripts/notes/generate_site_config.py` 自动生成，列出 `notes/requirements/` 下尚未归档的需求文档；文件名编号即建议实施顺序，一个 REQ 文件只覆盖一个连续实施周期。

- [REQ-040-a: Editor 命令总线 — 文本协议 + 注册表 + 历史 + 控制台后端](040-a-editor-command-bus.md)
- [REQ-041-a: ImGui Editor MVP — 场景树 / inspector / TRS gizmo / 视口 overlay / F 键预览](041-a-imgui-editor-mvp.md)
- [REQ-041-b: 编辑器命令总线 v2 — 参数补全 + undo·redo + 多选 EditorState](041-b-command-bus-v2.md)
- [REQ-041-c: 编辑器多选 / 框选 — scene tree 多选 + 视口拖拽框选](041-c-editor-multi-select.md)
- [REQ-041-d: 编辑器 undo / redo UI 接入 — 工具栏按钮 + 状态栏 + 全局快捷键](041-d-editor-undo-redo-ui.md)
- [REQ-041-e: 节点 Rename / Duplicate — 右键菜单 + 控制台命令 + Ctrl+D 快捷键](041-e-editor-node-rename-duplicate.md)
- [REQ-041-f: 编辑器 chrome — 菜单栏 + 工具栏其余按钮 + 主题切换](041-f-editor-toolbar-menubar-theme.md)
- [REQ-041-g: Component 模型 v2 — 同类型多 component + enable / disable](041-g-component-v2-multi-and-enable.md)
- [REQ-041-h: mesh 三角面级 picking — hit point / hit normal + CPU 侧 mesh 数据保留](041-h-mesh-level-triangle-picking.md)
- [REQ-041-i: DebugDraw v2 — persistent draw + 整 mesh 线框一行调用](041-i-debug-draw-persistent-and-mesh.md)
- [REQ-041-j: Component 依赖声明 — `ComponentTraits<T>` Requires / Before / After](041-j-component-dependency-declaration.md)
- [REQ-042: RenderTarget 拆分为 descriptor 与 binding，引入 MRT / layer / pipeline-key 接入](042-render-target-desc-and-target.md)
