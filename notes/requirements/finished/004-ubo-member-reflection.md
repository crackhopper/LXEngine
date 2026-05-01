# REQ-004: SPIRV-Cross 反射扩展 — UBO 成员信息

## 背景

`ShaderReflector`（`src/infra/shader_compiler/shader_reflector.cpp`）当前只提取每个 `ShaderResourceBinding` 的 `(set, binding)` 与 UBO 总字节大小 `size`。对 `UniformBuffer` 类型，struct 内部成员的 name/type/offset 没有抽取出来——调用方要知道 `baseColor` 在 offset 0、`shininess` 在 offset 16，只能靠手写的 `BlinnPhongMaterialUBO` struct 和 shader 硬保持同步。

## 目标

让反射结果**完整描述 UBO 布局**，使调用方可以：

- 按成员名（`StringID`）查到 std140 offset + size + type
- 不再需要为每个 shader 手写对齐 struct
- 为 REQ-005（统一材质系统）提供运行期自动写入 UBO 字节 buffer 的能力

## 需求

### R1: StructMemberInfo 类型

在 `src/core/resources/shader.hpp` 新增：

```cpp
struct StructMemberInfo {
  std::string         name;     // 成员名称，如 "baseColor"
  ShaderPropertyType  type;     // Float / Vec2 / Vec3 / Vec4 / Mat4 / Int
  uint32_t            offset;   // 字节偏移（std140 layout）
  uint32_t            size;     // 成员字节大小（std140 declared size）
};
```

`ShaderPropertyType` 需补一个 `Int` 成员——当前枚举只覆盖浮点向量和资源类型，但 UBO 内部常有 `int enableAlbedo` 这类字段。

### R2: ShaderResourceBinding 扩展

`ShaderResourceBinding` 新增字段：

```cpp
struct ShaderResourceBinding {
  // ... 现有字段（name / set / binding / type / descriptorCount / size / offset / stageFlags）...

  /// 仅对 `type == ShaderPropertyType::UniformBuffer` 有意义。
  /// 其他类型保持空 vector。
  std::vector<StructMemberInfo> members;
};
```

现有 `operator==` 不需要比较 `members`（members 完全由 set/binding 的 UBO 决定，是冗余信息）。

### R3: ShaderReflector 抽取 members

`shader_reflector.cpp` 的 `reflectSingleStage` 对 `uniform_buffers` 资源额外遍历 struct 成员：

```cpp
static void extractStructMembers(
    const spirv_cross::Compiler &compiler,
    const spirv_cross::SPIRType &type,
    std::vector<LX_core::StructMemberInfo> &out) {
  const uint32_t count = static_cast<uint32_t>(type.member_types.size());
  out.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto &memberType = compiler.get_type(type.member_types[i]);
    LX_core::StructMemberInfo info;
    info.name   = compiler.get_member_name(type.self, i);
    info.type   = mapMemberType(memberType);
    info.offset = compiler.get_member_decoration(
                    type.self, i, spv::DecorationOffset);
    info.size   = static_cast<uint32_t>(
                    compiler.get_declared_struct_member_size(type, i));
    out.push_back(std::move(info));
  }
}
```

新增 `mapMemberType(const SPIRType&)` — 与 `mapSpvType` 不同，它处理的是 struct 内部的数值成员（`SPIRType::Float/Int` + `vecsize`/`columns`），而不是资源对象。

调用位置：`extractBindings` 中命中 `StorageClassUniform` 且 `basetype == Struct` 时，为生成的 `ShaderResourceBinding` 调用 `extractStructMembers(compiler, type, b.members)`。

### R4: 合并跨 stage 的 members

`ShaderReflector::reflect()` 按 `(set, binding)` 合并多 stage 的绑定。合并时 `members` 必须一致（同一 UBO 在 vertex/fragment 里定义相同）——实现取第一个非空的 `members` 即可；断言合并后两个 vector 相等作为调试保护。

### R5: 确定性序

`members` 顺序按 spirv-cross 的 struct member index 输出（天然稳定），无需再排序。

## 测试

在 `src/test/integration/test_shader_compiler.cpp` 已有的 shader 编译测试基础上，添加一个子用例：

- 编译 `assets/shaders/glsl/blinnphong_0.{vert,frag}`
- 对 `MaterialUBO` 的 binding 断言 `members.size() >= 5`
- 断言 `findBindingMember("baseColor")` 返回 `{ type: Vec3, offset: 0, size: 12 }`（或 std140 的实际值）
- 断言 `findBindingMember("shininess")` 返回 `{ type: Float, offset: 16 }`

## 修改范围

| 文件 | 改动 |
|------|------|
| `src/core/resources/shader.hpp` | 新增 `StructMemberInfo`；`ShaderResourceBinding` 加 `members`；`ShaderPropertyType` 加 `Int` |
| `src/infra/shader_compiler/shader_reflector.cpp` | 新增 `mapMemberType` + `extractStructMembers`；`extractBindings` 中接入；`reflect()` 合并逻辑保留 members |
| `src/test/integration/test_shader_compiler.cpp` | 新增 UBO members 断言用例 |

## 边界与约束

- **仅提取 struct 内部字段**，嵌套 struct / array of struct 暂不支持，遇到时退回空 members 并记日志
- **不修改 std140 的计算规则**：offset 完全信任 spirv-cross
- **不暴露 spirv-cross 类型到 core 层**：core 只看到 `StructMemberInfo`

## 依赖

无——纯 additive 扩展，可独立落地。

## 下游

- REQ-005 消费 `members` 实现 `MaterialInstance` 的 UBO 自动管理
- REQ-003b R1 的 `PipelineBuildInfo.bindings` 直接复用扩展后的 `ShaderResourceBinding`

## 实施状态

已完成（2026-04-13）— 通过 `/finish-req 004` 验证。

**验证结果**：R1–R6 全部 ✓ Implemented（实际在更早的 session 里随 REQ-005 一并落地）
- R1 — `StructMemberInfo` + `ShaderPropertyType::Int` 位于 `src/core/resources/shader.hpp`
- R2 — `ShaderResourceBinding::members` 字段同文件
- R3 — `extractStructMembers` + `mapMemberType` + `extractBindings` hook 位于 `src/infra/shader_compiler/shader_reflector.cpp`
- R4 — `reflect()` 跨 stage 合并逻辑保留 members，调试断言同步 name/type/offset/size
- R5 — 成员顺序按 spirv-cross 声明顺序；最终结果按 `(set, binding)` 排序，成员顺序不变
- R6 — `src/test/integration/test_shader_compiler.cpp` 的 `MaterialUBO members` 测试用例覆盖：`baseColor Vec3@0 size=12`、`shininess Float@12 size=4`、`enableAlbedo/Normal Int`；`members.size() == 6`

**简化**：移除 `mapSpvType` 中死读的 `bufferFlags` 局部变量（存储/uniform 区分已由上层通过独立的 `resources.uniform_buffers` / `resources.storage_buffers` 列表完成）

**测试结果**：
- `test_shader_compiler` — PASS（所有子测试通过，`MaterialUBO members` 断言通过）
- `test_string_table` / `test_pipeline_identity` / `test_pipeline_build_info` / `test_frame_graph` / `test_material_instance` — 全部 PASS（回归无影响）
