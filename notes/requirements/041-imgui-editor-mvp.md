# REQ-041: ImGui Editor MVP — 场景树 / inspector / TRS gizmo / 视口 overlay / F 键预览

> 本 REQ 是 [Phase 1.5 ImGui Editor MVP + 命令总线](../roadmaps/main-roadmap/phase-1.5-imgui-editor-mvp.md) 的第 7 步（收口）。在 roadmap 中以"REQ-152 ImGui Editor MVP"前向声明。

## 背景

`src/demos/scene_viewer/main.cpp` 当前是硬编码场景：写死立方体 + 地面 + 单方向光 + 一个相机。每次 RTR 章节实验都要改代码 + 重编 + 重启。Phase 1.5 前 6 个 REQ 已经备好基础（Transform / 路径查询 / Camera 作 SceneNode / picking / DebugDraw / 命令总线），本 REQ 把它们接通成一个**最小可用的 ImGui 编辑器**：用户在运行时摆放模型 / 相机 / 光源、拖动 gizmo 调整位置、用控制台敲命令做精确调整、按 F 键预览游戏相机视角。

收口点：跑起来 demo 后用户能完成 [Phase 1.5 stub](../roadmaps/main-roadmap/phase-1.5-imgui-editor-mvp.md#验收) 列出的 5 项操作。

## 目标

1. 接入 ImGuizmo（third_party，MIT 单头文件库）实现 TRS gizmo
2. 4 个 ImGui 面板：scene tree / inspector / console / viewport overlay
3. F 键全屏切换游戏相机预览
4. DebugDraw 可视化：相机视锥（线框）+ directional light 箭头 + 选中节点 wireBox
5. 视口点击 → picking → `select` 命令（与控制台 `select` 命令互通）
6. 所有 gizmo 拖拽结束发 `move/rotate/scale` 命令；keyboard / 控制台 / gizmo 三种输入路径统一走命令总线

## 需求

### R1: ImGuizmo 引入

- `third_party/ImGuizmo/`（MIT 单头文件库 + 单 cpp）
- CMakeLists 把它作为 INTERFACE 库链入 `demo_scene_viewer`
- 写一层薄薄的 adapter `src/core/editor/gizmo_adapter.{hpp,cpp}`：把 LX 的 `Mat4f` / `Vec3f` 与 ImGuizmo 的 `float[16]` / `float[3]` 桥接（约 50 LOC）
- 立项前先 spike 验证 ImGuizmo 与 LX 数学类型 row-major / column-major 约定一致；不一致则 adapter 内部转置

### R2: Scene tree 面板

`src/core/editor/scene_tree_panel.{hpp,cpp}`（新）：

- 顶部输入框：path 直接跳转（输入 `/world/player` + 回车 → 选中并展开）
- 主体：递归渲染 scene root → leaves，每节点一行 `▸ name`（展开 chevron + 名字）
- 节点点击 → 发 `select <path>` 命令
- 右键节点弹菜单：Rename / Duplicate / Remove（v1 仅 Remove；其他 v2）
- 当前选中节点用高亮背景色显示
- 用 `Scene::dumpTree()` 作为渲染数据源是合理的，但 v1 直接遍历 `Scene` 节点更直接（避免文本反 parse）

### R3: Inspector 面板

`src/core/editor/inspector_panel.{hpp,cpp}`（新）：

- 显示 `EditorState::getSelected()` 节点的字段
- 公共字段（所有 SceneNode）：
  - Path（只读文本）
  - Name（可编辑 input text）
  - Transform：translation / rotation (XYZ Euler degrees) / scale 三行 `DragFloat3`；改完发命令
  - Visibility layer mask（u32 bitfield，UI 可视化为 32 个 checkbox）
- Camera 节点（额外）：fov / near / far / projection 类型 / cullingMask
- Light 节点（额外，directional）：direction / color / intensity
- 任何字段改动**最终发 `set <path>.<field> <value>` 命令**到 CommandBus（让 history 完整记录）
- 实现技巧：拖拽中**不**每帧发命令（会刷屏），仅在拖拽结束（`IsItemDeactivatedAfterEdit()`）时发

### R4: Console 面板

复用 [REQ-040](040-editor-command-bus.md) R5 的 `ConsolePanel`；本 REQ 仅在 main 里实例化并加进 ImGui frame loop。

### R5: Viewport overlay（gizmo + 选中线框 + visualizer）

`src/core/editor/viewport_overlay.{hpp,cpp}`（新）：

每帧调用：

```cpp
void renderOverlay(ImDrawList* dl, const Camera &editorCam, const Scene &scene, const EditorState &state);
```

内部行为：

1. 拿 editor camera 的 view + proj 矩阵
2. **gizmo**：若 `state.getSelected() != nullptr`，调 `ImGuizmo::Manipulate(view, proj, op, mode, &nodeWorldMat[0])`
   - 操作模式（op）= TRANSLATE / ROTATE / SCALE，由快捷键 W / E / R 切换（约定与 Unity / Unreal 一致）
   - 拖拽过程中暂存 transform，结束时发 `move/rotate/scale` 命令
3. **选中线框**：`DebugDraw::wireBox(selected.getWorldAABB(), Color::yellow())`
4. **相机视锥可视化**：遍历 `Scene::m_cameras`（除编辑器自己），对每个 camera 调 `DebugDraw::frustum(cam.viewProj(), Color::white())`
5. **方向光箭头**：遍历 `Scene::getLights()`，对每个 directional light 调 `DebugDraw::arrow(origin, origin + dir * len, Color::yellow())`；point/spot 等待 [REQ-109](../roadmaps/main-roadmap/phase-1-rendering-depth.md#req-109--pointlight--spotlight--统一多光源合同) 落地后追加（DebugDraw API 已为它们准备好）
6. **picking**：监听视口区域鼠标点击；命中时 → `editorCam.pickRay(...)` → `scene.pick(ray, Layer_All)` → 发 `select <path>` 命令
7. ImGuizmo 的鼠标 hover 优先于视口点击（避免拖 gizmo 时误中点击 picking）

### R6: F 键预览切换

- 视口 / 全屏鼠标焦点下，按 F：
  - 切换 `Scene::m_cameras` 中"哪个 camera 渲染到 swapchain"
  - 实现选项二选一（在本 REQ 决定）：
    - **方案 A**（`Camera::active` 布尔）：每个 camera 加 `bool m_active`；只有 active 的 camera 参与渲染
    - **方案 B**（target sentinel）：非活跃 camera 的 `target` 设为非 nullopt 但不绑定任何 RenderTarget，让 `RenderQueue::buildFromScene` 自动跳过
  - 选 **方案 A**（更直观，工程量小）
- 编辑器相机的 cullingMask 包含 `Layer_EditorOverlay`；游戏相机不含 → 切换后 gizmo / 视锥 / 光源箭头自动消失
- 控制台命令 `preview on/off/toggle` 与 F 键互通（都走同一段切换函数）

### R7: 启动时编辑器场景的初始状态

- 编辑器 camera 默认 attach 到 root，名字 `editor_cam`，cullingMask 含 `Layer_EditorOverlay`
- 一个示例 game camera attach 到 root，名字 `game_cam`，cullingMask 不含 `Layer_EditorOverlay`
- 一个 directional light（与现有 demo 一致）
- 一个 ground plane + 一个 cube（保留现有 demo 视觉，让 picking 有目标）
- 启动时打开 4 个面板，按 ImGui dock 默认布局

### R8: 关键操作的快捷键

| 键 | 行为 |
|---|---|
| `W` | gizmo TRANSLATE |
| `E` | gizmo ROTATE |
| `R` | gizmo SCALE |
| `F` | preview 切换 |
| `Delete` | 选中节点 → 发 `remove <path>` |
| `Ctrl+D` | 选中节点 → 发 `add ... <name>.copy`（v2 实现，本 REQ 占位） |
| `Esc` | deselect |

### R9: 所有交互最终走命令总线

硬约束：

- gizmo 拖拽结束 → `bus.dispatch("move ...")` 或 `rotate` / `scale`
- 视口点击 picking → `bus.dispatch("select <path>")`
- F 键 → `bus.dispatch("preview toggle")`
- 快捷键删除 → `bus.dispatch("remove <path>")`
- inspector 字段改 → `bus.dispatch("set <path>.<field> <value>")`

理由：所有 history / undo / agent-replay 都依赖单点入口。绝**不**给 UI 提供"绕过 CommandBus 直接改 scene"的捷径，否则 [REQ-040](040-editor-command-bus.md) 的 P-19 价值消失。

## 测试

启动 `demo_scene_viewer` 后人工跑：

1. 场景树面板列出 root + 所有子节点
2. 点击 cube → cube 被选中（高亮 + inspector 显示其 transform）
3. 拖拽 gizmo 三种模式（W / E / R 切换）→ world matrix 实时更新；放开后控制台出现 `move / rotate / scale` 命令记录
4. 视锥框（线框）从编辑器视角看到 game_cam 的 frustum
5. directional light 显示为线框箭头
6. 控制台输入 `move /cube 5 0 0` → cube 平移；inspector 同步刷新
7. 控制台输入 `list nodes` → 输出场景树文本快照；structured 含 JSON
8. 按 F → 全屏切到 game_cam；gizmo / 视锥 / 光源箭头自动消失
9. 再按 F → 回到编辑器视图
10. 视口空白处点击 → deselect；命中 cube 再点 → select cube

自动化（非阻塞）：

- `xvfb-run -a ./src/test/test_editor_smoke` — 用 dispatchScript 跑一段命令序列，dump 场景状态对 golden

## 修改范围

- `third_party/ImGuizmo/`（新引入）
- `src/core/editor/gizmo_adapter.{hpp,cpp}`（新）
- `src/core/editor/scene_tree_panel.{hpp,cpp}`（新）
- `src/core/editor/inspector_panel.{hpp,cpp}`（新）
- `src/core/editor/viewport_overlay.{hpp,cpp}`（新）
- `src/core/scene/camera.hpp` / `.cpp`（加 `m_active` + `setActive/isActive`，per R6 方案 A）
- `src/core/frame_graph/render_queue.cpp`（按 `m_active` 过滤 camera）
- `src/demos/scene_viewer/main.cpp`（构造 4 个面板 + 注册 builtins + 编辑器 camera 初始化）
- `src/demos/scene_viewer/ui_overlay.{hpp,cpp}`（保留作为 stats panel 或合并进 inspector）
- CMakeLists 多处接入 ImGuizmo
- `src/test/integration/test_editor_smoke.cpp`（新，可选）

## 边界与约束

- v1 **不**做 picture-in-picture 预览（依赖 [REQ-042 R1-R8](042-render-target-desc-and-target.md) RenderTarget 重写；R6 选择 `Camera::m_active` 方案绕开此依赖）
- v1 **不**做文件对话框 asset browser（用命令 `add mesh <path>` 替代）
- v1 **不**做多选 / 框选（单选 only）
- v1 **不**做 undo / redo（history 字段已存，逻辑 v2 加）
- v1 **不**做工具栏 / 菜单栏（只 4 个 dock 面板 + 视口 overlay）
- v1 **不**做 dark/light theme 切换（用 ImGui 默认 dark）
- ImGuizmo 是 MIT；本 REQ 引入它**不**改变项目 license 边界
- gizmo 视觉风格保持 ImGuizmo 默认；不做主题定制

### REQ-042 兼容预留

R6 引入的 `Camera::m_active` 与 [REQ-042 R6](042-render-target-desc-and-target.md) 升级的 `Camera::m_target` 在数据模型上**完全解耦**：前者表达"是否参与渲染"（编辑器 / 游戏相机互斥），后者表达"渲染到哪个 attachment 形状"（多 swapchain / MRT 路径）。REQ-042 R1-R8 后置实施时不影响 `m_active` 的语义；本 REQ 也不抢 `m_target` 的迁移工作。两者按 *正交字段* 设计，REQ-042 升级落地后只需把"非活跃 camera 不渲染"的过滤条件保持基于 `m_active`，无返工。

## 依赖

- [REQ-035 Transform 组件](finished/035-transform-component.md) — gizmo 拖拽生成的命令最终落到 `setTranslation/setRotation/setScale`
- [REQ-036 路径查询](finished/036-scene-node-path-lookup.md) — scene tree 用 path 作稳定句柄
- [REQ-037-a IComponent 基础](037-a-component-model-foundation.md) + [REQ-037-b Camera 作为 component](037-b-camera-as-component.md) — gizmo 也能作用于 camera 节点；inspector 按 component 列表渲染
- [REQ-038 picking](038-ray-aabb-picking-min.md) — 视口点击
- [REQ-039 DebugDraw](039-debug-draw-subsystem.md) — frustum / arrow / wireBox 可视化
- [REQ-040 命令总线](040-editor-command-bus.md) — 所有交互的统一入口
- [REQ-017](finished/017-imgui-overlay.md) ImGui + SDL3 + Vulkan 基础设施
- [REQ-018](finished/018-debug-panel-helper.md) 现有 debug panel helper（可选复用）

## 后续工作

- **Phase 1.6 MCP shim**：基于本 REQ + REQ-040 的命令总线起一个 stdio MCP server。外部 AI 控制编辑器 = MCP 客户端 dispatch_command 工具
- **REQ-042 RenderTarget 重写**完成后：把 F 键全屏预览升级为 picture-in-picture 视口（同帧并排渲染编辑器相机 + 游戏相机）
- **REQ-109 PointLight + SpotLight** 落地后：在 viewport_overlay 加几行调用 `DebugDraw::wireSphere` / `cone`，point/spot 影响范围一次性接通
- **Phase 9 Web 编辑器**：本 REQ 的 4 面板 + 命令总线接口直接搬到浏览器；UI 重写为 Vue，命令空间完全复用

## 实施状态

待实施。Phase 1.5 第 7 步（收口）。前 6 个 REQ（035-040）必须全部就位后开工。
