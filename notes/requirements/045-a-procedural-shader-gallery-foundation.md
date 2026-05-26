# REQ-045-a: Procedural Shader Gallery Foundation

> 2026-05-19 新增：本 REQ 建立 Shadertoy 风格 procedural shader 在 `lxe_editor` 中的第一条稳定路径。目标不是一次性复刻完整 Shadertoy，而是让单 pass fragment/raymarch 效果能作为普通材质进入 scene、gallery 和截图工作流。

## 背景

Shadertoy 风格效果常把几何写在 fragment shader 里：一个全屏三角形或屏幕前平面只负责触发像素执行，真正的“模型”由 SDF、raymarching、噪声、空间折叠等数学函数生成。我们需要把这类 shader 接到 LXEngine 的材质、scene document、editor 和验证路径里，而不是把它当作独立 demo 程序。

当前已有基础：

| 能力 | 当前事实 | 对本 REQ 的意义 |
|---|---|---|
| `.material` loader | `GenericMaterialLoader` 已能编译 `.vert/.frag` 并反射参数 | procedural shader 可以先作为普通材质接入 |
| 节点级参数覆盖 | `nodeMaterialOverrides` 已支持 `binding.member` 参数表 | gallery 可以保存每个节点的视觉调参 |
| Patch primitive | `builtin://lxe_editor/patches/square` 已存在 | 可以承载 screen-plane / billboard 风格效果 |
| FrameGraph | 已有 forward/shadow pass 和 offscreen resource 表达 | 后续可扩展到 post-process / multipass |

## 目标

1. 提供一个可运行的 procedural shader gallery 样例。
2. 让样例使用当前材质系统，而不是新增 backend 硬编码。
3. 让 scene document 能明确标记“这个节点的材质需要 procedural runtime 参数”。
4. 让效果可以和普通 forward/shadow 场景并存。

## 需求

### R1: 提供 Shadertoy 风格单 pass 材质资产

新增一组最小资产：

| 资产 | 作用 |
|---|---|
| `assets/shaders/glsl/rtr_shadertoy_quantum_core.vert` | screen-plane 顶点阶段 |
| `assets/shaders/glsl/rtr_shadertoy_quantum_core.frag` | SDF/raymarch fragment 阶段 |
| `assets/materials/rtr_shadertoy_quantum_core.material` | 声明 Forward pass、参数默认值和渲染状态 |

要求：

- shader 必须通过现有 `ShaderCompiler::compileProgram()`。
- 参数必须通过 reflection 暴露到 `MaterialInstance`。
- 不依赖真实音频输入；v1 使用 `audioBands` / `bass` / `mid` 等普通 uniform 参数模拟音频。
- render state 默认关闭 depth write，避免 screen-plane 覆盖场景深度。

### R2: Scene 中以普通节点承载 procedural shader

gallery v1 使用已有 patch mesh：

```yaml
mesh:
  uri: builtin://lxe_editor/patches/square       # -> buildBuiltinPatchNode(...)
material:
  uri: assets/materials/rtr_shadertoy_quantum_core.material
```

要求：

- 不新增 mesh loader。
- 不把 procedural shader 当作特殊 backend draw。
- 节点仍可移动、缩放、保存、选择。
- receiver-only mesh policy 仍可关闭 shadow pass，避免 procedural plane 参与 shadow caster。

### R3: 提供 gallery scene 入口

新增一个可直接打开的 scene 文件，展示 procedural material 与普通灯光/相机共存。

要求：

- scene 中包含 camera、light、一个 procedural square patch。
- procedural 节点使用 `nodeMaterialOverrides` 设置稳定初始视觉参数。
- scene 文件不依赖外部私有资产。

### R4: 保留材质系统边界

procedural shader 的参数继续属于 material-owned binding。

要求：

- `CameraUBO`、`LightUBO`、`SceneLightsUBO` 这类 system-owned binding 不用于 Shadertoy 兼容参数。
- 推荐使用 `ShadertoyUBO` 或 `ProceduralUBO`。
- `iChannel0` 在 v1 不作为真实音频 texture；音频 texture 留给 `REQ-045-c`。

### R5: 测试覆盖

至少覆盖：

- procedural shader 能编译。
- procedural material 能加载，并反射出 `time`、`resolution`、`audioBands` 等参数。
- gallery scene 能 round-trip 保存 `materialUri`、`nodeMaterialOverrides` 和 procedural 节点信息。
- procedural material 出现在 `materialPresets()` 候选中。

## 修改范围

- `assets/shaders/glsl/`
- `assets/materials/`
- `assets/scenes/`
- `src/demos/lxe_editor/scene_runtime.*`
- `src/test/integration/test_shader_compiler.cpp`
- `src/test/integration/test_generic_material_loader.cpp`
- `src/test/integration/test_scene_document.cpp`
- `src/test/integration/test_scene_runtime.cpp`

## 边界与约束

- 本 REQ 不实现真实音频 FFT。
- 本 REQ 不实现 Shadertoy Buffer A/B/C/D。
- 本 REQ 不实现通用 post-process pass。
- 本 REQ 不要求 shader hot reload。

## 依赖

- `REQ-041-h`：实验材质和节点级参数覆盖底座。
- `openspec/specs/material-system/spec.md`
- `openspec/specs/shader-compilation/spec.md`

## 后续工作

- `REQ-045-b`：统一 Shadertoy runtime 参数流。
- `REQ-045-c`：音频 channel、offscreen/post-process 和 multipass 兼容。

## 实施状态

已实施第一版。当前已有 `rtr_shadertoy_quantum_core` shader/material 资产、`procedural_shader_gallery.scene.yaml` gallery 场景、`proceduralMaterial` scene document opt-in、运行时参数流入口，以及 shader/material/document/runtime 集成测试。`test_scene_runtime` 现在显式覆盖 `assets/materials/rtr_shadertoy_quantum_core.material` 会出现在 `materialPresets()` 候选中，同时保留无效 fixture 与隐藏 material 的过滤行为。

验证命令：

- `cmake -S . -B build -G Ninja`
- `cmake --build build --target CompileShaders test_shader_compiler test_generic_material_loader test_scene_document test_scene_runtime lxe_editor -j2`
- `./build/src/test/test_shader_compiler`
- `./build/src/test/test_generic_material_loader`
- `./build/src/test/test_scene_document`
- `./build/src/test/test_scene_runtime`
