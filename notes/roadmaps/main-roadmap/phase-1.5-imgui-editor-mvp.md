# Phase 1.5 · ImGui Editor MVP + 命令总线

> **目标**：把当前硬编码的 `demo_scene_viewer` 升级为一个**最小可用的 ImGui 编辑器**，配一条**文本命令总线** + 嵌入式控制台。让 RTR (Real Time Rendering 4th) 章节实验阶段的"摆放模型 / 相机 / 光源 + 调参 + 切换视角"全部在运行时完成，不再需要改代码 + 重编。
>
> **依赖**：现状即可启动（`SceneNode` parent/child + dirty 传播、`Camera::cullingMask` × `SceneNode::visibilityLayerMask`、`PrimitiveTopology::LineList`、ImGui + SDL3 + Vulkan 集成均已就位）。
>
> **位置**：插在 [Phase 1 渲染深度](phase-1-rendering-depth.md) 与 [Phase 2 基础层](phase-2-foundation-layer.md) 之前。Phase 2 中 transform 层级 / picking / 路径查询 部分被提前到本 phase；Phase 9 Web 编辑器仍是长期目标，本 phase 是过渡产物。

## 当前实施状态

部分开工。`REQ-035` Transform 组件与 `REQ-036` 路径查询已完成；后续从 `REQ-037-a` 开始继续推进。设计已收口在工作机的临时计划文件 `~/.claude/plans/robust-mapping-flame.md`（不在仓库内），本 phase 文档是其落地索引。

## 范围与边界

**做：**

- 场景树面板 + inspector
- TRS gizmo（接入 ImGuizmo，MIT 单头文件库）
- DebugDraw 子系统（`drawLine / wireSphere / frustum / cone / arrow / axis` 一行调用）
- 文本命令总线 + 嵌入式 ImGui 控制台
- 相机视锥可视化、directional light 箭头可视化
- F 键全屏切换游戏相机预览（gizmo / debug 线自动隐藏，依赖 `Layer_EditorOverlay` 与 cullingMask 交集）
- Transform 层级值类型（`Transform { Vec3 t, Quat r, Vec3 s }`）
- Camera 接入 SceneNode，位置与朝向由 transform chain 驱动
- Scene path 查询（`/world/player/arm`）
- ray-AABB picking 暴力版（无空间索引，遍历可见节点）

**不做：**

- picture-in-picture 相机预览（依赖 [REQ-042](../../requirements/042-render-target-desc-and-target.md) RenderTarget 重写，后置到本 phase 完工后、Phase 1 [REQ-103 Shadow](phase-1-rendering-depth.md#req-103--shadow-pass--depth-only-pipeline) 之前；与之配套的 [REQ-034 删 getHash dead code](../../requirements/finished/034-remove-render-target-get-hash.md) cleanup 已归档、不阻塞本 phase）
- 文件系统 asset browser 对话框（用命令 `add mesh <path>` 替代）
- 多选 / 框选（v1 仅单选）
- 撤销 / 重做（命令总线预留 history 字段，逻辑 v2 再做）
- MCP shim（推到 Phase 1.6）
- point / spot 光源可视化（数据类型在 [REQ-109](phase-1-rendering-depth.md#req-109--pointlight--spotlight--统一多光源合同) 落地，本 phase 仅画 directional 箭头；DebugDraw 的 `wireSphere` / `cone` API 已为后续接入预留）

## 工作分解

### 从 Phase 2 提前

| REQ | 标题 | 主要工作 |
|-----|------|----------|
| [REQ-035](../../requirements/finished/035-transform-component.md) | Transform 组件 | 把现有 `SceneNode::m_localTransform` (Mat4) 重构为 `Transform { Vec3 t, Quat r, Vec3 s }`；保留 lazy world + dirty 传播 |
| [REQ-036](../../requirements/finished/036-scene-node-path-lookup.md) | 场景节点路径查询 | 新增 `Scene::findByPath("/world/player/arm")` |
| [REQ-037-a](../../requirements/037-a-component-model-foundation.md) | IComponent 基础设施 + Mesh / Material / Skeleton 转 component | 抽出 `IComponent` 基类；mesh / material / skeleton 都改写成 component；`SceneNode` 改为持有 component 集合；删除 `node->getMesh()` 等专属 getter，调用点全量迁移 |
| [REQ-037-b](../../requirements/037-b-camera-as-component.md) | Camera 作为 component 接入 SceneNode | `Camera` 类彻底改写为 `CameraComponent`，挂在 `SceneNode` 上；位置 / 朝向由 owner SceneNode 的 transform chain 驱动；硬依赖 037-a |
| [REQ-038](../../requirements/038-ray-aabb-picking-min.md) | ray-AABB picking 暴力版 | 无空间索引；遍历可见节点 + 每 mesh 加载时算一次本地 AABB；选中最近命中 |

### 本 phase 新增

| REQ | 标题 | 主要工作 |
|-----|------|----------|
| [REQ-039](../../requirements/039-debug-draw-subsystem.md) | DebugDraw 子系统 | 公开 API：`drawLine / wireSphere / frustum / cone / arrow / axis`；累积每帧 line 顶点；line topology pipeline；`Layer_EditorOverlay` mask；FrameGraph 后置一个 debug overlay pass。**易用性硬指标：业务代码一行调用画一根世界空间线。** |
| [REQ-040](../../requirements/040-editor-command-bus.md) | Editor 命令总线 | `verb arg1 arg2` 文本协议；handler 注册表；返回 `{ ok, message, payload }`；初版命令集：`select / deselect / move / rotate / scale / add / remove / list / set / get / cam / preview`；预留 history（为后续 undo / MCP 服务） |
| [REQ-041](../../requirements/041-imgui-editor-mvp.md) | ImGui Editor MVP | 接入 ImGuizmo；scene tree / inspector / console / viewport overlay 四个面板；F 键全屏切换游戏相机；视锥与 directional light 用 DebugDraw 画出 |

## 推进顺序

```
Step 0  roadmap 文档更新（README + phase-1 + 本 phase）  ← 文档先行（已完成）
Step 1   REQ-035    Transform 组件                                 ← 基础（已完成）
Step 2   REQ-036    路径查询                                       ← 基础（已完成）
Step 3a  REQ-037-a  IComponent 基础 + mesh/material/skeleton       ← 架构级重构，单独 commit
Step 3b  REQ-037-b  Camera 作为 component                          ← 第一个非 mesh 应用，单独 commit
Step 4   REQ-038    ray-AABB picking + mesh bounds                 ← 选择交互前置
Step 5  REQ-039 DebugDraw 子系统                        ← 可视化基础
Step 6  REQ-040 命令总线 + 控制台面板                   ← 控制层基础
Step 7  REQ-041 ImGui Editor MVP                        ← 收口
```

每个 step 完成时 `demo_scene_viewer` 都应仍能正常运行，不破坏已有可视效果。

## 验收

启动 `demo_scene_viewer` 后用户能够：

1. **场景树面板** + **inspector**：点击节点 → 选中
2. **TRS gizmo**：拖拽完成平移 / 旋转 / 缩放，所有交互最终发出文本命令
3. **DebugDraw**：相机视锥（线框）、directional light 箭头从编辑器视角可见
4. **嵌入式控制台**：`move <node> 1 0 0`、`select <node>`、`add light directional` 等文本命令都能跑通
5. **F 键预览**：全屏切到游戏相机，gizmo / 视锥 / 光源箭头自动隐藏；再按 F 回到编辑器视图

## 与 AI-Native 原则契合

- [P-19 双向命令总线](principles.md)：本 phase 的命令总线就是 P-19 的具体实现起点；Phase 1.6 MCP shim、Phase 9 Web 编辑器、Phase 10 agent 都共用同一组命令
- [P-3 Query / Command / Primitive](principles.md)：gizmo 拖拽 → Command；picking → Query；DebugDraw 一行调用 → Primitive
- [P-16 文本优先 / 文本唯一](principles.md)：所有编辑操作在控制台留下文本痕迹，可被记录、回放、被 agent 消费
- [P-7 多分辨率观察](principles.md)：scene tree 是 outline 视图；inspector 是 full 视图；`list nodes` 命令是 summary 视图

## 与现有架构契合

- 复用 `SceneNode` 现有 parent/child + dirty 传播逻辑（`src/core/scene/object.hpp:122-176`），不重写
- 复用 `Camera::cullingMask` × `SceneNode::visibilityLayerMask` 交集过滤（`src/core/scene/scene.cpp:109-120`）—— gizmo / debug 线挂 `Layer_EditorOverlay` bit 即可与游戏相机隔离
- 复用 `PrimitiveTopology::LineList` + backend `topologyToVk`（`src/core/scene/index_buffer.hpp:18-25`）—— DebugDraw pipeline 直接消费
- 复用 ImGui + SDL3 + Vulkan 集成（[REQ-017](../../requirements/finished/017-imgui-overlay.md)）—— 只新增面板，不动后端
- 复用 `IInputState` / Orbit / FreeFly 控制器（[REQ-012](../../requirements/finished/012-input-abstraction.md) / [REQ-015](../../requirements/finished/015-orbit-camera-controller.md) / [REQ-016](../../requirements/finished/016-freefly-camera-controller.md)）—— 编辑器相机直接复用 FreeFly

## 风险

- **ImGuizmo 与 LX 数学类型适配**：薄薄一层 adapter（约 50 LOC）。立项 [REQ-041](../../requirements/041-imgui-editor-mvp.md) 前先 spike 一次。
- **Component 模型重构（[REQ-037-a](../../requirements/037-a-component-model-foundation.md)）的扇出面**：删除 `SceneNode::getMesh / getMaterialInstance / getSkeleton` 后，`src/` 下所有读这三个字段的位置（`object.cpp` 内的 renderable 路径、demo / loader / test setup）一次性硬迁移。建议作为 Phase 1.5 第一笔架构 commit 单独落地，再开 037-b。
- **Camera-as-component（[REQ-037-b](../../requirements/037-b-camera-as-component.md)）的迁移面**：所有 scene_viewer 构造 camera + Orbit / FreeFly 控制器接口的位置都要改（`Camera*` → `CameraComponent*`）。在 037-a 落地后单独 commit 完成。
- **Mesh 包围盒**：当前 mesh loader 未必计算 bounds。需在 GLTF / OBJ 加载流程加一遍 min/max。
- **多 camera 同 swapchain 渲染语义**：`Camera::matchesTarget(nullopt)` 默认绑定 swapchain，多 camera 同 nullopt 会都渲染。F 键切换在 [REQ-041](../../requirements/041-imgui-editor-mvp.md) R6 中已选定方案 A：每个 `Camera` 加 `m_active` 布尔。

## 下一步

本 phase 完成后开 [Phase 1](phase-1-rendering-depth.md)，按 2026-05-01 更新后的优先路径推进：REQ-109 → REQ-103/104 → REQ-118 → REQ-105/106 → REQ-119。

MCP shim 单独立 Phase 1.6，复用本 phase 的命令总线接口形态，外部 agent 通过 stdio JSON-RPC 调用同一组命令。
