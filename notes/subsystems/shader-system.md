# Shader System

> 这个系统把 GLSL 文件变成运行时可消费的 `IShader`。它负责三件事：编译、反射、封装；现在反射结果除了 descriptor bindings，还显式包含 vertex-stage input contract。
>
> 权威 spec: `openspec/specs/shader-compilation/spec.md` + `openspec/specs/shader-reflection/spec.md` + `openspec/specs/forward-shader-variant-contract/spec.md`

## 它解决什么问题

- 运行时编译 shader，而不是依赖构建期 codegen。
- 自动提取 descriptor bindings、UBO member 布局和 vertex input 属性。
- 给材质系统、`SceneNode` 校验和 backend 提供统一的 shader 数据入口。

## 核心对象

- `ShaderCompiler`：调用 shaderc 产出 SPIR-V。
- `ShaderReflector`：调用 SPIRV-Cross 抽取 bindings、UBO members 和 vertex inputs。
- `CompiledShader`：`IShader` 的实现，持有 stages 和反射结果。
- `ShaderProgramSet`：shader 名与 variants 的组合，参与 pipeline 身份。

## 典型数据流

1. `ShaderCompiler::compileProgram(...)`
2. `ShaderReflector::reflect(stages)` 抽 descriptor / UBO 信息
3. `ShaderReflector::reflectVertexInputs(stages)` 抽 vertex input contract
4. `CompiledShader(stages, bindings, vertexInputs, name)`
5. `MaterialTemplate`、`SceneNode` 和 backend 同时消费这个结果

## 关键约束

- 同一 `(set, binding)` 跨 stage 时需要合并 `stageFlags`。
- 材质参数查找靠 UBO member 名字，所以 GLSL 成员名就是接口名。
- vertex input 以 location 为主键做比较，便于和 `VertexLayout` 做结构匹配。
- `CompiledShader::getShaderName()` 用 basename，不带路径和扩展名。
- 复杂 UBO 结构提取失败时，binding 仍可存在，但 `members` 可能为空。
- `blinnphong_0` 现在是一个固定 forward shader family，而不是单一硬编码 shader。它的变体集是 `USE_VERTEX_COLOR`、`USE_UV`、`USE_LIGHTING`、`USE_NORMAL_MAP`、`USE_SKINNING`。
- 这个 family 的接口收缩已经放进 GLSL 声明层，而不是只在函数体里跳过逻辑：没开 `USE_UV` 就不声明 `inUV`，没开 `USE_SKINNING` 就不声明骨骼 attributes 和 `Bones` UBO。
- 运行时 fallback 仍然存在，但位置变成“资源已声明、是否采样由 `MaterialUBO.enableAlbedo/enableNormal` 决定”。因此 reflection 表示的是“当前 variant 允许哪些资源/输入出现”，不是“每一帧一定会走哪条采样路径”。

## 当前实现边界

- `CompiledShader` 现在有 `getVertexInputs()` 和 `findVertexInput(location)` 两条只读查询路径。
- `SceneNode` 的结构校验会直接消费 shader 反射出的 vertex inputs，逐个检查 mesh layout 是否提供对应 location/type。
- skinned / non-skinned shader 的差异已经可以通过反射结果直接观察，不需要靠外部约定猜测。
- `src/test/integration/test_shader_compiler.cpp` 已覆盖 `blinnphong_0` 的 unlit / vertex color / UV / lighting / normal-map / skinning 组合，确保 `ShaderReflector::reflectVertexInputs(...)` 只返回当前 variant 实际声明的输入。
- lit `blinnphong_0` 路径仍保留 ambient fallback；这个行为属于 shader 实现约定，不直接体现在 reflection 输出里，但会影响“标准 lit variant”在旧场景下是否还能看见物体。

## 从哪里改

- 想改 shader 变体：看 `ShaderVariant` / `ShaderProgramSet` / material loader，再看 `blinnphong_0` 的条件声明是否同步更新。
- 想改 vertex layout 校验：先看 `ShaderReflector` 的输出，再看 `SceneNode::rebuildValidatedCache()`。
- 想改 backend descriptor 布局：先看这里的 binding 输出，再看 Vulkan pipeline。

## 关联文档

- `notes/subsystems/material-system.md`
- `notes/subsystems/scene.md`
- `notes/subsystems/vulkan-backend.md`
