# v0.1.0 CHANGELOG

> 本页记录已经落地的 roadmap 历史能力。后续 roadmap 只保留未来计划与当前缺口；已完成内容统一沉淀到本发布记录。

## 发布定位

`v0.1.0` 把 LXEngine 从“基础 Vulkan 教学渲染器”推进到一个可被人和 agent 共同操作的最小编辑器底座：

- `lxe_editor` 成为当前主交互入口。
- 场景对象具备 TRS transform、层级、路径查询、component 模型、camera-as-component。
- 编辑操作进入 command-first 工作流，可由 ImGui、HTTP / WebSocket API、MCP 诊断通道复用。
- 测试场景作者链路可以创建、选择、编辑、保存 primitive / camera / light / material 参数。
- RTR 第五章实验所需的多类型光源与实验材质接入面已经具备。

## 场景与组件基础

| 能力 | 状态 | 来源 |
|---|---|---|
| `Transform { translation, rotation, scale }` 值类型 | 已完成 | REQ-035 |
| `SceneNode` parent / child 层级与 lazy world transform | 已完成 | REQ-035 |
| `Scene::findByPath(...)` / `SceneNode::getPath()` / `Scene::dumpTree()` | 已完成 | REQ-036 |
| `IComponent` 基础设施 | 已完成 | REQ-037-a |
| `MeshComponent` / `MaterialComponent` / `SkeletonComponent` | 已完成 | REQ-037-a |
| `CameraComponent` 挂载到 `SceneNode` | 已完成 | REQ-037-b |
| camera view 由 owner node transform chain 推导 | 已完成 | REQ-037-b |

当前实现入口主要在：

- `src/core/math/transform.*`
- `src/core/scene/object.*`
- `src/core/scene/scene.*`
- `src/core/scene/component.*`
- `src/core/scene/components/*`

## 编辑器选择与可视化

| 能力 | 状态 | 来源 |
|---|---|---|
| mesh 本地 `BoundingBox` 与 world bounds | 已完成 | REQ-038-a |
| `Ray` / ray-box 求交 | 已完成 | REQ-038-a |
| `Scene::pick(...)` 暴力节点级 picking | 已完成 | REQ-038-a |
| `DebugDraw::drawLine / wireSphere / frustum / cone / arrow / axis` | 已完成 | REQ-039-a |
| editor overlay layer 隔离 | 已完成 | REQ-039-a |
| 选中节点 bounds / camera frustum / light helper 可视化 | 已完成 | REQ-039-a / REQ-041-a |

边界：`DebugDraw` 当前按单线程 frame 内调用模型理解；线程安全调用没有作为 `v0.1.0` 完成项发布。

## 命令总线与 ImGui Editor

| 能力 | 状态 | 来源 |
|---|---|---|
| `CommandBus` 文本协议与 handler 注册 | 已完成 | REQ-040-a |
| 控制台命令历史与基础补全 | 已完成 | REQ-040-a |
| `select / deselect / move / rotate / scale / add / remove / set / get / cam / preview` 等编辑命令 | 已完成 | REQ-040-a / REQ-041-a |
| undo / redo 与参数补全 v2 | 已完成 | REQ-041-b |
| 多选 `EditorState` 与多目标编辑命令 | 已完成 | REQ-041-b |
| ImGui scene tree / inspector / console / viewport overlay | 已完成 | REQ-041-a |
| ImGuizmo TRS 操作 | 已完成 | REQ-041-a |
| F 键 gameplay camera preview | 已完成 | REQ-041-a |
| scene tree 多选 / 视口框选 / 多 selected 视觉化 | 已完成 | REQ-041-c |

当前实现入口主要在：

- `src/editor/commands/command_bus.*`
- `src/editor/commands/builtin_commands.*`
- `src/editor/app/editor_state.*`
- `src/editor/panels/console_panel.*`
- `src/editor/panels/inspector_panel.*`
- `src/editor/panels/scene_tree_panel.*`
- `src/editor/panels/viewport_overlay.*`
- `src/editor/ui/gizmo_adapter.*`
- `src/editor/ui/ui_overlay.*`
- `src/editor/runtime/scene_interaction_controller.*`

## 测试场景作者链路

| 能力 | 状态 | 来源 |
|---|---|---|
| toolbar 创建盘：Cube / Sphere / Plane / Cylinder / Cone | 已完成 | REQ-041-d |
| toolbar 创建 Directional / Point / Spot Light 与 Camera | 已完成 | REQ-041-d / REQ-041-g |
| 点击创建与拖拽放置 | 已完成 | REQ-041-d |
| `SceneDocument` 保存 `meshUri` / `materialUri` / camera / typed light / material override | 已完成 | REQ-041-d ~ REQ-041-h |
| `SceneRuntime` 从 scene document 构造 primitive / camera / light / material | 已完成 | REQ-041-d ~ REQ-041-h |
| project -> scene 的编辑器工作流 | 已完成 | 代码现状 |
| project 内多 scene 的创建、打开、复制、删除与保存 | 已完成 | 代码现状 |
| dirty scene 关闭确认 | 已完成 | 代码现状 |
| editor config / editor data 本地持久化 | 已完成 | 代码现状 |

当前实现入口主要在：

- `src/editor/project/scene_document.*`
- `src/editor/runtime/scene_runtime.*`
- `src/editor/project/project_document.*`
- `src/editor/project/project_session.*`
- `src/editor/project/project_catalog.*`
- `src/editor/app/editor_config_state.*`
- `src/editor/app/editor_data_state.*`

## 光源与 RTR 实验底座

| 能力 | 状态 | 来源 |
|---|---|---|
| `LightBase` + `DirectionalLight` scene-level 管理 | 已完成 | 既有基础 + REQ-041-g |
| `PointLight` / `SpotLight` | 已完成 | REQ-041-g |
| `LightKind` typed scene document payload | 已完成 | REQ-041-g |
| Inspector / CommandBus 编辑 `light.kind / direction / color / intensity / range / cone` | 已完成 | REQ-041-g |
| `SceneLightsUBO` 多光源 GPU 数据合同 | 已完成 | REQ-041-g |
| `assets/shaders/glsl/scene_lights_ubo.glsl` shader 合同 | 已完成 | REQ-041-g |

边界：`v0.1.0` 发布的是多类型光源数据与作者入口；完整多光源 shading、shadow、CSM、IBL 和 G-Buffer 仍属于后续渲染路线。

## 材质与实验参数

| 能力 | 状态 | 来源 |
|---|---|---|
| 通用 `.material` YAML loader | 已完成 | 既有基础 |
| shader reflection 校验材质参数 | 已完成 | 既有基础 |
| Inspector 切换材质 URI | 已完成 | REQ-041-e |
| 节点级 `baseColor` 覆盖 | 已完成 | REQ-041-e |
| 节点级通用材质参数覆盖表 | 已完成 | REQ-041-h |
| 实验材质接入合同 | 已完成 | REQ-041-h |
| `SceneLightsUBO` 作为 system-owned binding | 已完成 | REQ-041-g / REQ-041-h |

边界：`v0.1.0` 不发布 Gooch shading 或完整 RTR 第五章公式，只发布新增实验材质所需的接入面。

## 远程控制与录制

| 能力 | 状态 | 来源 |
|---|---|---|
| `LxeEditorApiService` 复用 `CommandBus` 执行命令 | 已完成 | 代码现状 |
| HTTP command / state query API | 已完成 | 代码现状 |
| WebSocket event stream | 已完成 | 代码现状 |
| editor runtime state 发布 HTTP / WebSocket / MCP discovery 信息 | 已完成 | 代码现状 |
| `RecordingController` enable / start / stop / list / read / replay / probe | 已完成 | 代码现状 |
| manager MCP 配置脚本迁移到 `scripts/lxe_manager/enable_mcp.*` | 已完成 | 代码现状 |

边界：`v0.1.0` 不是完整 Phase 10。引擎内置 agent runtime、capability manifest、成本模型、HITL 强制和通用 CLI 仍未发布。

## 测试覆盖

`v0.1.0` 对应能力已有较密集的集成测试与黑盒测试覆盖，包括：

- command bus：`src/test/integration/test_command_bus*.cpp`
- editor state / 多选 / scene tree / inspector / viewport overlay：`src/test/integration/test_editor_multi_select.cpp`、`test_scene_tree_panel.cpp`、`test_inspector_panel.cpp`、`test_viewport_overlay.cpp`
- picking / DebugDraw：`src/test/integration/test_picking.cpp`、`test_debug_draw.cpp`
- scene document / runtime / project session：`src/test/integration/test_scene_document.cpp`、`test_scene_runtime.cpp`、`test_project_session.cpp`
- API / recording：`src/test/integration/test_lxe_editor_api_*.cpp`、`test_lxe_editor_recording.cpp`
- Python editor workflow / persistence / scene IO / MCP config：`tests/lxe_editor/*.py`

## 未包含

以下内容仍属于后续 roadmap，不作为 `v0.1.0` 已完成项：

- shadow map / CSM
- HDR scene color target / tone mapping pass / Bloom / FXAA
- IBL environment loader 与 prefilter
- 完整 PBR 多光源 + shadow 接收 + 完整贴图集
- G-Buffer / deferred rendering
- WebGPU / WebGL2 / WASM 后端
- action mapping / gamepad / fixed step / 时间缩放
- Asset GUID / registry / `.meta` / runtime asset root contract
- animation player / physics / TypeScript gameplay / audio / player UI
- 完整 MCP + agent runtime + CLI
- AI asset generation
- 打包发布流水线
