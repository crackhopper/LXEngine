# REQ-041-g: RTR 第五章实验底座 v1 — 多类型光源数据与作者入口

> 2026-05-11 新增：本 REQ 排在 `041-f` 之后。`041-d` 已负责“能快速创建一个 Directional Light”，但 Real-Time Rendering 第五章实验需要的是更通用的光源数据底座：我们要能快速放入 Directional / Point / Spot Light，并把它们稳定保存、编辑、同步到 runtime。具体光照公式不在本 REQ 内实现。

## 背景

当前代码里已经有一部分光源基础：

- `src/core/scene/light.hpp` 已有 `LightBase` 抽象接口与 `DirectionalLight`
- `Scene` 已通过 `std::vector<LightBaseSharedPtr>` 管理 scene-level lights
- `Scene::getSceneLevelResources(pass, target)` 会收集 light 的 GPU resource
- `lxe_editor` 的 `SceneDocument` 只保存 `DirectionalLightNodeState`
- `SceneRuntime` 只维护 `directionalLightsByNode`
- `CommandBus` 与 `InspectorPanel` 只认识 directional light 的 `direction / color / intensity`
- 现有 forward shader 使用单个 `LightUBO`，没有 Point / Spot 的稳定数据合同

因此，当前能力足够支持一个方向光 demo，但不适合用作“学习并验证多种光源”的实验环境。

## 当前代码对照（2026-05-14）

| 能力 | 当前事实 | 对本 REQ 的影响 |
|---|---|---|
| Core light | `LightBase` 与 `DirectionalLight` 已存在，`Scene` 通过 light 列表和 node 绑定管理 scene-level lights | R2 仍要新增 `PointLight` / `SpotLight`，但不需要重建 directional light 基础 |
| Scene document | 仍只有 `DirectionalLightNodeState` 与 `directionalLight` YAML 负载 | R1 的 `light.kind` 迁移仍未实现 |
| Runtime | `SceneRuntime` 只创建并捕获 directional light | Point / Spot 的 runtime 构建、保存与 reload 仍是新增工作 |
| Inspector/CommandBus | 只认识 directional light 的 `direction/color/intensity`，命令字段也没有 `light.<field>` 命名空间 | R5/R4 仍要按 light kind 重建编辑和创建入口 |
| Shader binding | system-owned binding 当前包含 `CameraUBO`、`LightUBO`、`Bones`，未登记 `SceneLightsUBO` | R3 的多光源数据合同仍未实现 |
| Editor helper | 方向光已有线框/箭头代理 | R6 需要扩展到 point range sphere 与 spot cone，并保留 directional 代理 |

## 目标

1. `lxe_editor` 能创建、保存、加载 Directional / Point / Spot 三类光源节点
2. Inspector 能编辑三类光源的常用参数
3. runtime 能为实验 shader 提供稳定的光源数据输入合同
4. 需求只建立底座，不实现具体多光源 shading 公式

## 需求

### R1: Scene document 使用类型化 light 负载

`SceneNodeDocument` 需要从只支持 `directionalLight` 扩展为类型化 light 负载。推荐形状：

```yaml
light:
  kind: Point
  color: [1.0, 0.95, 0.85]
  intensity: 2.0
  range: 6.0
```

首批支持三种 `kind`：

| kind | 数据来源 | 必需字段 |
|---|---|---|
| `Directional` | 节点负载 | `direction` / `color` / `intensity` |
| `Point` | 节点 transform + 节点负载 | `color` / `intensity` / `range` |
| `Spot` | 节点 transform + 节点负载 | `direction` / `color` / `intensity` / `range` / `innerConeDegrees` / `outerConeDegrees` |

要求：

- 节点 transform 仍负责位置、旋转和可视化摆放
- `Point` 的位置来自节点世界位置，不在 light 负载里重复存 position
- `Spot` v1 保留显式 `direction` 字段，避免先依赖完整旋转编辑体验
- 旧的 `directionalLight` YAML 仍可读取，并在保存时转成新的 `light.kind: Directional`
- 一个节点最多拥有一个 light 负载

### R2: core light 类型补齐到实验所需的最小集合

在 `LightBase` 体系下补齐：

- `DirectionalLight`
- `PointLight`
- `SpotLight`

要求：

- 三类 light 都能声明 pass participation
- 三类 light 都能返回自己的 GPU 数据 resource
- 新增类型不得绕过 `Scene::addLight()` / `Scene::getSceneLevelResources(...)`
- 不要求实现 Area Light、Shadow、IES、physically-based 单位制
- 不要求在本 REQ 中完成任何具体光照公式

### R3: 建立实验 shader 可消费的光源绑定合同

现有 `LightUBO` 只适合单 directional light。为了让后续实验 shader 能稳定接入多光源，本 REQ 需要定义一个 scene-owned 光源数据合同，例如：

```glsl
layout(set = 0, binding = X) uniform SceneLightsUBO {
  int directionalCount;
  int pointCount;
  int spotCount;
  // fixed-size arrays for the experiment path
} sceneLights;
```

具体要求：

- `SceneLightsUBO` 或等价名字需要加入 system-owned binding 识别
- 数据结构允许固定上限，v1 不做动态 SSBO / clustered lighting
- 上限应清晰写入 C++ 与 GLSL 共享约束，例如 `MaxDirectionalLights / MaxPointLights / MaxSpotLights`
- 超过上限时给出稳定诊断，不静默丢光源
- 保留现有 `LightUBO` 兼容路径，避免一次性改坏现有 blinnphong / pbr shader
- 本 REQ 只要求数据上传合同成立，不要求 shader 实际使用这些 light 得到正确视觉效果

### R4: 工具栏与命令总线支持三类光源创建

在 `041-d` 的场景对象行基础上扩展 light palette：

| 条目 | 命令 | 默认值 |
|---|---|---|
| `Directional Light` | `add light:directional <name> [parentPath]` | 方向 `(-0.3, -1.0, -0.5)`，暖白色，强度 `1.0` |
| `Point Light` | `add light:point <name> [parentPath]` | 放置在拖拽落点，范围 `5.0`，强度 `1.0` |
| `Spot Light` | `add light:spot <name> [parentPath]` | 放置在拖拽落点，朝向编辑相机 forward，范围 `8.0`，内外角 `20/35` |

要求：

- 点击与拖拽创建都走 CommandBus
- 创建后必须立即生成可保存的 `light` scene document 负载
- `041-f` 的 duplicate/copy-paste 必须能复制三类 light 负载并建立独立 runtime light

### R5: Inspector 使用 light kind 驱动参数面板

Inspector 不再通过节点名字猜测是否是 light。它应根据 scene document / runtime 绑定的 light kind 显示对应面板。

| kind | Inspector 字段 |
|---|---|
| `Directional` | `direction` / `color` / `intensity` |
| `Point` | `color` / `intensity` / `range` |
| `Spot` | `direction` / `color` / `intensity` / `range` / `innerConeDegrees` / `outerConeDegrees` |

要求：

- 所有修改都走 CommandBus
- 命令命名可沿用 `set <path>.light.<field> ...`
- 修改后同步 runtime 与 scene document
- undo / redo 后 Inspector snapshot 与 runtime light 数据一致

### R6: 编辑器可视化代理只表达调试几何，不表达最终 shading

三类 light 需要有简单线框代理：

- Directional：方向箭头
- Point：range sphere
- Spot：cone + range

要求：

- 代理走 editor overlay / DebugDraw 路径
- 代理不写入 scene document
- 代理颜色可以来自 light color，但不要求表达真实亮度或衰减

### R7: 测试覆盖

至少补以下测试：

- `SceneDocument` 能 round-trip Directional / Point / Spot light 负载
- 旧 `directionalLight` YAML 能加载并保存为新 `light.kind: Directional`
- CommandBus 创建三类 light 后，runtime 有对应独立 light 实例
- Inspector 修改三类 light 参数后，scene document 与 runtime 同步
- 超过 `SceneLightsUBO` 固定上限时返回稳定错误
- 现有只使用 `LightUBO` 的 shader/material 测试不被破坏

## 修改范围

- `src/core/scene/light.hpp`
- `src/core/scene/scene.{hpp,cpp}`
- `src/core/asset/shader_binding_ownership.hpp`
- `src/core/editor/commands/builtin_commands.cpp`
- `src/core/editor/inspector_panel.{hpp,cpp}`
- `src/core/editor/viewport_overlay.{hpp,cpp}`
- `src/demos/lxe_editor/ui_overlay.cpp`
- `src/demos/lxe_editor/scene_document.{hpp,cpp}`
- `src/demos/lxe_editor/scene_runtime.cpp`
- `assets/shaders/glsl/` 中实验 shader 需要的共享合同
- `src/test/integration/test_scene_document.cpp`
- `src/test/integration/test_scene_runtime.cpp`
- `src/test/integration/test_command_bus.cpp`
- `src/test/integration/test_inspector_panel.cpp`

## 边界与约束

- 本 REQ 不实现 Gooch shading
- 本 REQ 不实现最终多光源 Blinn-Phong / PBR 公式
- 本 REQ 不做 Shadow、Area Light、Clustered / Tiled Lighting、light probe
- 本 REQ 不要求 asset browser 或完整 light preset 系统
- 保留现有 `LightUBO` 兼容路径，避免把已有材质链路一次性迁到新合同

## 依赖

- [REQ-041-d](041-d-scene-authoring-toolbar-palette.md) — 复用工具栏拖拽创建入口
- [REQ-041-e](041-e-scene-authoring-inspector-material-and-visibility.md) — 复用 Inspector 与 CommandBus 编辑路径
- [REQ-041-f](041-f-scene-authoring-node-rename-duplicate.md) — 复制三类 light 节点时需要 scene document 语义完整
- `src/core/asset/shader_binding_ownership.hpp` 与 `openspec/specs/material-system/spec.md` — system-owned binding 合同

## 后续工作

- 在本底座稳定后，单独实现 RTR 第五章的具体光源方程、衰减模型和可视化对比场景
- 若实验需要更多灯光，再扩展 `kind`，不要把 Area Light 塞进本 REQ

## 实施状态

已实施并验证。Scene document 已升级为 `light.kind` typed payload 并兼容旧 `directionalLight` 读取；core/runtime 支持 Directional / Point / Spot Light，CommandBus 与 Inspector 支持三类 light 的创建和编辑，editor overlay 提供对应调试代理，`SceneLightsUBO` 已登记为 system-owned binding 并保留旧 `LightUBO` 兼容路径。

验证命令：

- `cmake --build build --target test_command_bus test_inspector_panel test_scene_document test_scene_runtime test_material_instance lxe_editor -j2`
- `./build/src/test/test_command_bus`
- `./build/src/test/test_inspector_panel`
- `./build/src/test/test_scene_document`
- `./build/src/test/test_scene_runtime`
- `./build/src/test/test_material_instance`
