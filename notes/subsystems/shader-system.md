# Shader System

> 从 GLSL 源码到带反射 binding 信息的可消费 shader 对象。运行期走完整的"编译 → 反射 → 封装"流程，不依赖任何预处理工具或 build 时 codegen。
>
> 权威 spec: `openspec/specs/shader-compilation/spec.md` + `openspec/specs/shader-reflection/spec.md`
> 深度设计: `docs/design/ShaderSystem.md`

## 核心抽象

### Core 接口 (`src/core/resources/shader.hpp`)

- **`IShader`** (`:110`) — 抽象接口，继承 `IRenderResource`
  - `getAllStages() → const vector<ShaderStageCode>&`
  - `getReflectionBindings() → const vector<ShaderResourceBinding>&`
  - `findBinding(set, binding) → optional<ref<const ShaderResourceBinding>>`（O(1) 常用路径）
  - `findBinding(name) → optional<ref<const ShaderResourceBinding>>`
  - `getShaderName() → string`（例如 `"blinnphong_0"`）
- **`ShaderStageCode`** (`:102`) — `{stage, bytecode}` 值对象
- **`ShaderResourceBinding`** (`:73`) — 单个 descriptor binding 的反射描述：name / set / binding / type / descriptorCount / size / offset / stageFlags / **members**
- **`StructMemberInfo`** (`:45`) — UBO block 内单个成员的 std140 layout：name / type / offset / size
- **`ShaderProgramSet`** (`:153`) — `{shaderName, variants}`，参与 `PipelineKey`
- **`ShaderVariant`** (`:141`) — 条件编译宏及开关

### Infra 实现 (`src/infra/shader_compiler/`)

- **`ShaderCompiler`** (`shader_compiler.hpp:15`) — shaderc 前端
  - `compileProgram(vert, frag, variants) → CompileResult`
  - 输出 `CompileResult { success, errorMessage, stages }`
- **`ShaderReflector`** (`shader_reflector.hpp:7`) — SPIRV-Cross 后端
  - `reflect(stages) → vector<ShaderResourceBinding>`
  - 按 `(set, binding)` 跨 stage 合并
  - 对 `UniformBuffer` 类型抽取 `members` 走 std140 layout（REQ-004 之后）
- **`ShaderImpl`** (`shader_impl.hpp:7`) — `IShader` 的唯一实现
  - 持有 stages + bindings + name
  - 构建 `(set,binding) → binding_index` 的 O(1) 查表

## 典型用法

```cpp
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_impl.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

using namespace LX_infra;

// 1. 编译
auto compiled = ShaderCompiler::compileProgram(
    "shaders/glsl/blinnphong_0.vert",
    "shaders/glsl/blinnphong_0.frag",
    {});
if (!compiled.success) throw std::runtime_error(compiled.errorMessage);

// 2. 反射
auto bindings = ShaderReflector::reflect(compiled.stages);

// 3. 封装
auto shader = std::make_shared<ShaderImpl>(
    std::move(compiled.stages), bindings, "blinnphong_0");

// 4. 查 binding
auto cam = shader->findBinding(1, 0);          // O(1) by (set, binding)
auto mat = shader->findBinding("MaterialUBO"); // by name

// 5. 枚举 UBO 成员（REQ-004）
for (const auto &m : mat->get().members) {
    std::cout << m.name << " @ " << m.offset << " (" << m.size << "B)\n";
}
```

## 调用关系

```
blinnphong_material_loader (infra/loaders/)
  │
  ├── ShaderCompiler::compileProgram() ── shaderc ─→ SPIR-V
  │
  ├── ShaderReflector::reflect()       ── SPIRV-Cross ─→ ShaderResourceBinding[]
  │
  └── ShaderImpl(stages, bindings, name) ─→ IShaderPtr
                                               │
                                               ▼
                            MaterialTemplate::create(name, shader)
                                               │
                                               ▼
                            MaterialInstance 构造时读 getReflectionBindings()
                            查找 "MaterialUBO" 并分配 std140 字节 buffer
                                               │
                                               ▼
                            Backend（VulkanPipeline）构建时读 bindings 生成
                            VkDescriptorSetLayoutBinding 和 vertex input
```

## 注意事项

- **SPIR-V 合并规则**: 同一 `(set, binding)` 出现在多 stage 时，`ShaderReflector::reflect()` 会把 `stageFlags` 按位或合并，**members 取第一个非空的那个**（断言所有 stage 的 members 一致）。
- **UBO member 识别靠名字**: `MaterialInstance` 通过 `StringID(member.name) == id` 查找，member 名必须和 GLSL 声明里一致（`vec3 baseColor;` → `"baseColor"`）。
- **非平凡结构的 fallback**: 如果 UBO 里有嵌套 struct / array of struct，`extractStructMembers()` 会 clear `members` + warn log，binding 仍然返回但 `members` 为空。这正是 `blinnphong_0.vert` 的 `Bones { mat4 bones[128] }` 的情况。
- **不走 build 时 codegen**: 这个项目的选择是运行期 shaderc + SPIRV-Cross，不是 build 时把 shader 编成 C++ header。shader 可以热重载或运行期组合变体。
- **文件名查找**: `ShaderImpl::getShaderName()` 返回的是 **basename**（例如 `"blinnphong_0"`），不含路径或后缀。backend 的 pipeline 缓存 key 不依赖它（走 `PipelineKey`），但调试日志会显示。

## 延伸阅读

- `openspec/specs/shader-compilation/spec.md` — shaderc 前端的编译与变体宏规则
- `openspec/specs/shader-reflection/spec.md` — SPIRV-Cross 后端的 binding 提取 + `StructMemberInfo`（REQ-004 补齐）
- `docs/design/ShaderSystem.md` — 完整数据流图 + `ShaderImpl` 内部查表结构
- 归档: `openspec/changes/archive/2026-04-10-shader-compile-and-reflection/` — 基础版 compile + reflect
- 归档: `openspec/changes/archive/2026-04-13-ubo-member-reflection/` — REQ-004 加入 `StructMemberInfo`
