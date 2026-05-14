# REQ-041-d: lxe_editor 测试场景快速搭建 v1 — 工具栏几何体 / 光源 / 相机拖拽创建

> 2026-05-11 重整：旧的 `041-d`/`041-f`/`041-g`~`041-j` 以“编辑器 polish / 底层成熟度”拆得过细，且多项已经偏离当前目标。按现有代码审计后，active REQ 收敛回“方便快速建立测试场景文件”这一条主线；本 REQ 负责创建入口。

## 背景

当前 `lxe_editor` 已经具备一条可用但不高效的编辑链路：

- `UiOverlay` 里已经有浮动工具栏，当前第一行提供 `Selection` 编辑模式、`Orbit / FreeFly` 相机控制，以及 reset editor camera / Preview / Debug / Preferences 功能按钮
- `CommandBus` 已有 `add` 命令，但当前只支持 `mesh|light|camera` 三种粗粒度目标，其中 `mesh` 只是空节点，并不会创建具体几何体与材质
- `SceneDocument` 已能序列化 `meshUri` / `materialUri` / `camera` / `directionalLight`
- `SceneRuntime` 当前只会把 `builtin://lxe_editor/helmet` 和 `builtin://lxe_editor/ground_mesh` 解析成真实内容

这意味着：我们已经能“保存场景”，但还不能“快速搭场景”。现在最缺的是一个直接面向测试场景的创建入口，而不是继续扩菜单栏、主题、组件模型 v2、mesh 级 picking 或 DebugDraw v2。

## 当前代码对照（2026-05-14）

对照当前工作区代码，本 REQ 仍未实现，但已有基础能力如下：

| 能力 | 当前事实 | 对本 REQ 的影响 |
|---|---|---|
| Toolbar 外壳 | `UiOverlay::drawToolbarPanel()` 已提供 Selection、Orbit / FreeFly、reset editor camera、Preview、Debug、Preferences | R1 应继续扩展现有浮动 Toolbar，不新建 menubar |
| CommandBus 创建 | `add mesh|light|camera <name> [parentPath]` 已存在；`mesh` 是空 renderable，`light` 是 directional light，`camera` 创建 `CameraComponent` | R5 仍要把粗粒度 `add` 升级为具体 `primitive:*` / `light:directional` / `camera:perspective` |
| Scene document | `SceneNodeDocument` 已保存 `meshUri`、`materialUri`、`camera`、`directionalLight` | R2 可基于现有字段落地，不需要先重做 scene document |
| Runtime 解析 | `SceneRuntime` 只把 `builtin://lxe_editor/helmet` 与 `builtin://lxe_editor/ground_mesh` 解析为真实内容，未知 mesh URI 仍退化为空节点 | builtin primitive 解析仍是 R2 的核心缺口 |
| Editor helper | `SceneInteractionController::enqueueDebugDraw()` 已绘制 camera frustum 与 directional light 线框/箭头，并可在 gizmo 拖拽时抑制 helper | R4 的 camera 可见代理已有雏形，但仍要确认选中强化与创建后体验 |

## 目标

1. 在现有浮动工具栏里直接展示常用测试几何体
2. 支持把几何体、方向光、透视相机拖到视口或场景树里创建节点
3. 新建对象必须自带合理默认值，不要求用户再回控制台补命令
4. 新建内容必须能稳定保存回 `.scene.yaml`

## 需求

### R1: 在现有工具栏内加入“创建盘”，不再单独立菜单栏

- 直接扩展 `src/demos/lxe_editor/ui_overlay.cpp` 现有浮动工具栏；不新增顶部 menubar，也不重做窗口 chrome
- 保留第一行现有工具栏分组：`Selection` 编辑模式、`Orbit / FreeFly` 相机控制，以及 reset editor camera / Preview / Debug / Preferences 功能按钮
- 在其下方新增两行：

| 行 | 内容 |
|---|---|
| 常用几何体 | `Cube` / `Sphere` / `Plane` / `Cylinder` / `Cone` |
| 场景对象 | `Directional Light` / `Camera` |

- 每个条目都是可拖拽源；同时允许点击创建，作为拖拽的无障碍回退路径
- 创建盘默认常驻显示；不再把“工具栏是否存在”作为需求点

### R2: 创建内容必须映射到明确的场景文档语义

本 REQ 不接受“先建一个空 mesh 节点，后面再手填”的路径。每个拖拽条目都要直接落到可保存的 scene document 负载。

首批内建条目：

| 条目 | `meshUri` / 负载 | `materialUri` / 负载 |
|---|---|---|
| Cube | `builtin://lxe_editor/primitives/cube` | `assets/materials/blinnphong_lit.material` |
| Sphere | `builtin://lxe_editor/primitives/sphere` | `assets/materials/blinnphong_lit.material` |
| Plane | `builtin://lxe_editor/primitives/plane` | `assets/materials/blinnphong_lit.material` |
| Cylinder | `builtin://lxe_editor/primitives/cylinder` | `assets/materials/blinnphong_lit.material` |
| Cone | `builtin://lxe_editor/primitives/cone` | `assets/materials/blinnphong_lit.material` |
| Directional Light | `directionalLight` 负载 | 无 mesh/material 必填负载 |
| Camera | `camera` 负载 | 无 mesh/material 必填负载 |

- `SceneRuntime` / `SceneBuilder` 需要补齐这些 builtin primitive 的解析与默认 mesh/material 创建
- 当前代码只存在方向光语义；因此“光源拖拽创建”在 v1 明确限定为 `Directional Light`
- Camera 创建的是普通场景相机节点，不是 editor camera
- 旧的 `add mesh <name>` 粗粒度入口应被具体条目替代，避免继续产生“空 mesh 节点”

### R3: 拖拽放置规则必须可预测

- 拖到 **场景视口**：
  - 优先使用编辑相机射线与现有场景求交
  - 若命中现有物体，则放在命中点附近，并沿世界上方向做最小上抬，避免 primitive 半埋进表面
  - 若未命中物体，则回退到编辑相机前方固定距离
  - 若能与 `y = 0` 地面平面相交，则优先落到该平面，作为“搭测试场景”的更稳定默认
- 拖到 **场景树节点**：
  - 新节点挂到目标节点下
  - 默认局部变换为 identity；若同时带视口落点，则转换成目标父节点局部坐标
- 仅点击创建：
  - 默认挂到当前选中节点；若无选中则挂到 scene root
  - 默认放在编辑相机前方固定距离

默认变换：

- Primitive：`scale = (1,1,1)`，朝向世界轴
- Directional Light：默认方向沿当前现有 demo 光照方向 `(-0.3, -1.0, -0.5)`
- Camera：默认复制当前 editor camera 的姿态与基础投影参数，便于“从当前观察位置落一个可保存机位”

### R4: 相机节点需要可见的编辑器线框代理

- Camera 节点在编辑模式下应有线框代理；当前已有 frustum 调试绘制，本 REQ 需要补齐或确认 forward 指示与选中强化
- 该代理只用于 editor overlay，可通过现有 DebugDraw/overlay 路径绘制
- 代理不写入 scene document，不影响 gameplay camera 渲染
- 选中 Camera 节点时，代理应比普通节点轮廓更明显，避免“场景里有相机但看不见”

### R5: 命令总线需要提供可复用的创建入口

- UI 拖拽和点击创建都必须走命令总线，不走 UI 私有捷径
- 推荐把当前 `add` 扩展成具体 kind：
  - `add primitive:cube <name> [parentPath]`
  - `add primitive:sphere <name> [parentPath]`
  - `add primitive:plane <name> [parentPath]`
  - `add primitive:cylinder <name> [parentPath]`
  - `add primitive:cone <name> [parentPath]`
  - `add light:directional <name> [parentPath]`
  - `add camera:perspective <name> [parentPath]`
- placement 信息若来自视口拖拽，可通过额外 metadata / 后续 `set translation` 命令补齐，但对外仍表现为一次完整创建操作，支持现有 undo / redo

### R6: 测试覆盖

至少补以下集成测试：

- 工具栏创建盘在 CPU-only ImGui 帧里可绘制，不崩溃
- 拖拽 `Cube` 到空白视口后，场景中出现带 `meshUri/materialUri` 的节点，且可保存后再加载
- 拖拽 `Directional Light` 后，场景里出现可编辑的方向光节点
- 拖拽 `Camera` 后，场景里出现带 `CameraComponent` 的节点，默认姿态接近 editor camera
- 点击创建和拖拽创建都走命令总线 history，undo 后节点消失，redo 后恢复

## 修改范围

- `src/demos/lxe_editor/ui_overlay.cpp`
- `src/demos/lxe_editor/scene_document.{hpp,cpp}`
- `src/demos/lxe_editor/scene_runtime.cpp`
- `src/demos/lxe_editor/scene_builder.{hpp,cpp}`
- `src/core/editor/commands/builtin_commands.cpp`
- `src/core/editor/viewport_overlay.{hpp,cpp}` 或其他承接 editor-space 放置的位置
- `src/test/integration/test_scene_runtime.cpp`
- `src/test/integration/test_scene_document.cpp`
- `src/test/integration/test_lxe_editor_interaction.cpp`

## 边界与约束

- 本 REQ 只做 `lxe_editor` 当前已经有语义支撑的测试场景作者路径；不引入通用 asset browser
- 本 REQ 不做 point light / spot light；当前代码没有对应场景负载与 runtime 路径
- 本 REQ 不依赖 mesh 三角面级 picking；基于现有 picking / 射线和平面回退即可
- 本 REQ 不要求先实现菜单栏、主题系统、组件模型 v2、DebugDraw v2
- builtin primitive 首批只覆盖测试场景高频几何体；glTF 任意模型拖拽不在本 REQ 范围

## 依赖

- [REQ-041-a](finished/041-a-imgui-editor-mvp.md) — 现有 ImGui editor / viewport / toolbar 外壳
- [REQ-041-b](finished/041-b-command-bus-v2.md) — undo / redo / structured command result
- [REQ-041-c](finished/041-c-editor-multi-select.md) — 选中状态与 viewport 交互基础

## 后续工作

- 若 primitive 创建盘稳定，再讨论 asset browser / 模型拖拽
- 若出现更多灯光类型，再单独立 REQ 扩展 light palette
- 若 camera 代理需要更强表达，可再加 shot name / active marker / gameplay camera badge

## 实施状态

已实施并验证。工具栏创建盘已接入 primitive、Directional / Point / Spot Light 与 Camera；点击、视口拖放、Scene Tree 拖放均走 CommandBus `add` 路径。Builtin primitive runtime 构建、scene document 保存/加载、创建历史 undo/redo 和 CPU-only ImGui 绘制已通过集成测试覆盖。

验证命令：

- `cmake --build build --target test_command_bus test_command_bus_v2 test_scene_tree_panel test_inspector_panel test_scene_document test_scene_runtime test_generic_material_loader test_shader_compiler test_material_instance lxe_editor -j2`
- `./build/src/test/test_command_bus`
- `./build/src/test/test_command_bus_v2`
- `./build/src/test/test_scene_tree_panel`
- `./build/src/test/test_inspector_panel`
- `./build/src/test/test_scene_document`
- `./build/src/test/test_scene_runtime`
- `./build/src/test/test_generic_material_loader`
- `./build/src/test/test_shader_compiler`
- `./build/src/test/test_material_instance`
