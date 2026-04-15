# Shader System

> 这个系统把 GLSL 文件变成运行时可消费的 `IShader`。它负责三件事：编译、反射、封装。
>
> 权威 spec: `openspec/specs/shader-compilation/spec.md` + `openspec/specs/shader-reflection/spec.md`

## 它解决什么问题

- 运行时编译 shader，而不是依赖构建期 codegen。
- 自动提取 descriptor bindings 和 UBO member 布局。
- 给材质系统和 backend 提供统一的 shader 数据入口。

## 核心对象

- `ShaderCompiler`：调用 shaderc 产出 SPIR-V。
- `ShaderReflector`：调用 SPIRV-Cross 抽取 binding 和 UBO members。
- `CompiledShader`：`IShader` 的实现，持有 stages 和反射结果。
- `ShaderProgramSet`：shader 名与 variants 的组合，参与 pipeline 身份。

## 典型数据流

1. `ShaderCompiler::compileProgram(...)`
2. `ShaderReflector::reflect(stages)`
3. `CompiledShader(stages, bindings, name)`
4. `MaterialTemplate` 和 backend 同时消费这个结果

## 关键约束

- 同一 `(set, binding)` 跨 stage 时需要合并 `stageFlags`。
- 材质参数查找靠 UBO member 名字，所以 GLSL 成员名就是接口名。
- `CompiledShader::getShaderName()` 用 basename，不带路径和扩展名。
- 复杂 UBO 结构提取失败时，binding 仍可存在，但 `members` 可能为空。

## 从哪里改

- 想改 shader 变体：看 `ShaderVariant` / `ShaderProgramSet`。
- 想改材质参数反射：看 `ShaderReflector` 和 `StructMemberInfo`。
- 想改 backend descriptor 布局：先看这里的 binding 输出，再看 Vulkan pipeline。

## 关联文档

- `openspec/specs/shader-compilation/spec.md`
- `openspec/specs/shader-reflection/spec.md`
- `notes/subsystems/material-system.md`
- `notes/subsystems/vulkan-backend.md`
