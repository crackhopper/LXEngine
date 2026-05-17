# REQ-041-h: RTR 第五章实验底座 v1 — 实验材质接入与节点级参数覆盖

> 2026-05-11 新增：本 REQ 排在 `041-g` 之后。目标不是实现 Gooch shading，而是让 `lxe_editor` 和材质系统提供足够稳定的接入面，使我们可以快速新增一个实验材质、挂到场景节点、修改参数、保存场景并验证效果。

## 背景

当前材质链路已经具备较好的基础：

- `GenericMaterialLoader` 可以从 `.material` YAML 读取 shader、passes、variant、parameters、resources
- loader 会编译 `assets/shaders/glsl/<shader>.vert/.frag` 并通过反射校验参数
- `assets/shaders/CMakeLists.txt` 会收集 shader 源并生成 SPIR-V
- `041-e` 会让 Inspector 支持 `materialUri` 切换和节点级 `baseColor` 覆盖

但对于 RTR 第五章实验，仍然缺少一层“快速接入新材质”的底座：

- Inspector 只计划支持固定材质 preset，不适合频繁增加实验材质
- scene document 只计划保存 `baseColor` 这种单一节点级覆盖
- 新 shader / material 是否符合 runtime 资源合同，需要有更明确的最小模板和验证路径

## 当前代码对照（2026-05-14）

| 能力 | 当前事实 | 对本 REQ 的影响 |
|---|---|---|
| Material loader | `GenericMaterialLoader`、`.material` YAML、shader reflection 校验和 shader CMake 收集路径已经存在 | R1 主要是把实验接入合同文档化并补模板，不需要改 backend 硬编码 |
| System-owned binding | 当前登记了 `CameraUBO`、`LightUBO`、`Bones`，还没有 `SceneLightsUBO` | 若实验材质要用多光源合同，需要等待或配合 `041-g` |
| Inspector material | `041-e` 尚未实现，当前 Inspector 只显示 `Material: yes/no` | R2/R4/R5 依赖先有 material 区块和 `set <path>.materialUri` |
| Scene document override | 当前没有 `nodeMaterialOverrides`，也没有通用参数表 | R3/R5 是本 REQ 的核心新增保存语义 |
| 实验资产 | 当前 `assets/materials/` 已有 blinnphong / pbr 材质，但未见 `rtr_*.material` 模板 | R6 仍要新增实验模板与测试 |

## 目标

1. 新增一个实验材质时，不需要改 backend 硬编码
2. `lxe_editor` 能从材质目录发现或登记实验材质，并在 Inspector 中快速切换
3. 常用材质参数可以作为节点级覆盖保存到 scene file
4. get-started 能说明“如何新增一个实验材质并放进场景验证”

## 需求

### R1: 定义实验材质最小接入合同

新增实验材质时，最小需要三类资产：

| 资产 | 作用 |
|---|---|
| `assets/shaders/glsl/<shader>.vert` | 顶点阶段 |
| `assets/shaders/glsl/<shader>.frag` | 片元阶段 |
| `assets/materials/<name>.material` | 材质 YAML，声明 shader / parameters / passes |

要求：

- 不需要修改 backend pipeline 硬编码
- `.material` 中的参数必须继续经过 shader reflection 校验
- system-owned binding 只允许使用已登记的名字，例如 `CameraUBO`、`LightUBO`、`SceneLightsUBO`、`Bones`
- material-owned 参数继续通过 `MaterialInstance::setParameter(...)` 写入
- 本 REQ 可以提供一个 `rtr_experiment_template.material` 作为最小模板，但不提供 Gooch 的最终公式

### R2: Scene viewer 提供实验材质目录 / preset 数据源

`041-e` 的 preset 是固定列表。本 REQ 需要把它升级为可扩展的数据源：

- 扫描或登记 `assets/materials/` 下可用于实验的 `.material`
- 至少支持 `assets/materials/rtr_*.material` 作为实验材质候选
- Inspector 的 Material 区块可以从候选列表切换 `materialUri`
- 加载失败时显示稳定错误，不让 Inspector 崩溃

要求：

- v1 不做完整 asset browser
- v1 不做任意文件选择器
- 候选列表可按文件名排序，保证测试稳定
- 切换材质仍走 `set <path>.materialUri <uri>`

### R3: 节点级材质参数覆盖从 `baseColor` 扩展为小型参数表

`041-e` 的 `nodeMaterialOverrides.baseColor` 只覆盖一个字段。为了支持 Gooch 这类实验材质，scene document 需要支持小型参数表。

推荐形状：

```yaml
material:
  uri: assets/materials/rtr_gooch_experiment.material
nodeMaterialOverrides:
  MaterialUBO.surfaceColor: [0.8, 0.2, 0.2]
  MaterialUBO.coolColor: [0.0, 0.0, 0.55]
  MaterialUBO.warmColor: [0.9, 0.8, 0.2]
  MaterialUBO.alpha: 0.25
  MaterialUBO.beta: 0.5
```

要求：

- key 使用 `binding.member`，与 `.material` parameters 的命名方式一致
- v1 支持 `float`、`int`、`Vec3`、`Vec4`
- 节点级覆盖默认只影响当前节点实例
- 保存场景时覆盖必须落到 scene file
- 加载场景时，先加载 `materialUri` 指向的材质，再应用节点级覆盖
- 若覆盖字段不在 shader reflection 中存在，加载或应用时给出稳定诊断

### R4: Inspector 用反射驱动实验参数编辑

当材质暴露 material-owned 参数时，Inspector 在 Material 区块显示一个小型参数编辑区：

| 类型 | 控件 |
|---|---|
| `float` | DragFloat / InputFloat |
| `int` | InputInt |
| `Vec3` | DragFloat3 或 ColorEdit3，根据字段名启发式选择 |
| `Vec4` | DragFloat4 或 ColorEdit4，根据字段名启发式选择 |

要求：

- 控件修改默认写入 node-level override，不直接改 `.material` 资产
- `041-e` 的 `Apply Override To Material` 语义继续保留为显式动作
- Inspector 只展示 material-owned 参数，不展示 `CameraUBO` / `SceneLightsUBO` 等 system-owned 资源
- 参数列表需要有稳定顺序，便于测试

### R5: CommandBus 提供通用节点材质参数覆盖入口

新增或扩展命令：

```text
set <path>.nodeMaterial.<binding>.<member> <value...>
clear <path>.nodeMaterial.<binding>.<member>
```

要求：

- 支持 `float`、`int`、`Vec3`、`Vec4`
- 成功结果带 structured 输出，包含 path、binding、member、type、value
- 修改支持 undo / redo
- 命令执行时校验当前材质是否真的暴露该参数
- 保存以 scene document 为准，不从 runtime material 反推

### R6: 提供最小实验材质模板与验证路径

为了让后续学习实现能快速开始，本 REQ 需要提供一个“空白但可运行”的实验模板：

- 一个 `.material` 模板
- 一组最小 shader 模板
- 模板可以只做简单可见输出，不实现 Gooch
- 模板命名要明确表示是实验起点，例如 `rtr_experiment_template`

要求：

- `test_shader_compiler` 能编译模板 shader
- `loadGenericMaterial(...)` 能加载模板 material
- `lxe_editor` 能把模板 material 挂到 primitive 节点
- 模板中的参数要覆盖 `float` 与 `Vec3/Vec4` 中至少两类，验证 Inspector 通用参数路径

### R7: 测试覆盖

至少补以下测试：

- 新实验 material 能通过 `GenericMaterialLoader` 加载并通过反射校验
- Inspector 能列出 `assets/materials/rtr_*.material` 候选
- `set <path>.nodeMaterial.<binding>.<member> ...` 能更新当前节点 runtime material，并保存到 scene document
- 两个节点引用同一 material 时，节点级参数覆盖只影响当前节点
- scene document round-trip 后，通用 `nodeMaterialOverrides` 保留并能重新应用
- 无效参数 key 会得到稳定失败信息
- `Apply Override To Material` 不会写回磁盘 `.material` 文件

## 修改范围

- `src/core/editor/inspector_panel.{hpp,cpp}`
- `src/core/editor/commands/builtin_commands.cpp`
- `src/demos/lxe_editor/scene_document.{hpp,cpp}`
- `src/demos/lxe_editor/scene_runtime.cpp`
- `src/infra/material_loader/generic_material_loader.cpp`
- `assets/materials/`
- `assets/shaders/glsl/`
- `src/test/integration/test_inspector_panel.cpp`
- `src/test/integration/test_command_bus.cpp`
- `src/test/integration/test_scene_document.cpp`
- `src/test/integration/test_scene_runtime.cpp`
- `src/test/integration/test_shader_compiler.cpp`

## 边界与约束

- 本 REQ 不实现 Gooch shading 的最终公式
- 本 REQ 不做节点材质参数的完整 asset browser
- 本 REQ 不回写仓库里的 `.material` 资产文件
- 本 REQ 不要求热重载 shader；修改 shader 后重新构建/运行即可
- 本 REQ 不把 texture slot 编辑纳入 v1
- 本 REQ 不要求自动生成 shader 代码，只提供接入合同和模板

## 依赖

- [REQ-041-e](041-e-scene-authoring-inspector-material-and-visibility.md) — 先建立 materialUri 与节点级 baseColor 覆盖
- [REQ-041-f](041-f-scene-authoring-node-rename-duplicate.md) — 复制节点时需要复制节点级材质覆盖
- [REQ-041-g](041-g-rtr-light-experiment-foundation.md) — 实验材质可以选择使用新的多光源 scene-owned 数据合同
- `openspec/specs/material-asset-loader/spec.md` 与 `notes/concepts/material/index.md` — 通用 `.material` loader 与默认参数

## 后续工作

- 在本底座稳定后，单独创建 Gooch shading 实验材质需求或学习记录
- 若实验中 texture 参数变成高频需求，再单独扩展 texture slot Inspector
- 若 shader 迭代速度成为瓶颈，再讨论 shader/material hot reload

## 实施状态

已实施并验证。新增 `rtr_experiment_template` shader/material 模板，Inspector 可发现 `rtr_*.material` 候选并展示反射驱动参数，scene document/runtime 支持通用 `nodeMaterialOverrides` 参数表且保留 `baseColor` 兼容入口，CommandBus 支持 `set` / `clear <path>.nodeMaterial.<binding>.<member>`。

验证命令：

- `cmake --build build --target test_generic_material_loader test_shader_compiler test_scene_document test_command_bus test_inspector_panel test_scene_runtime lxe_editor -j2`
- `./build/src/test/test_generic_material_loader`
- `./build/src/test/test_shader_compiler`
- `./build/src/test/test_scene_document`
- `./build/src/test/test_command_bus`
- `./build/src/test/test_inspector_panel`
- `./build/src/test/test_scene_runtime`
