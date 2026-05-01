# REQ-032: Pass-Aware Material Binding Interface

## 背景

当前材质系统有两个核心限制：

1. `MaterialInstance` 只识别一个名字叫 `"MaterialUBO"` 的材质参数块
2. `MaterialTemplate` 的 binding 查找会把多 pass 的反射结果压平成一张 name-keyed cache

这导致几个问题叠在一起：

- 材质语义被错误收缩成“一个特殊 UBO + 一些纹理”
- 非 `MaterialUBO` 的材质参数块无法自然表达
- 多个材质-owned buffer 无法表达
- 不同 pass 的同名 binding 可能互相覆盖
- `REQ-030` 已经明确指出：template 级扁平 binding cache 会丢失 pass 作用域

现在已经有了更明确的方向：

- shader 反射提供 binding 的结构事实
- 保留名字集由 [`REQ-031`](031-global-shader-binding-contract.md) 定义
- 非保留 binding 默认归材质所有

接下来需要把 `MaterialTemplate` / `MaterialInstance` 的接口重做成真正的 material-owned binding model。

同时，首版还需要明确：哪些 descriptor 类型是 pass-aware material interface 的正式组成部分。否则即使 ownership 与 pass 解析正确，系统仍可能处于“反射看见了，但材质接口无法表达”的半支持状态。

## 目标

1. 去掉对 `"MaterialUBO"` 的单点依赖。
2. 把材质接口改成按 pass 生效、按 binding 名表达。
3. 让材质实例支持多个 material-owned buffer / texture / 其他 descriptor slot。
4. 明确首版正式支持的 material-owned descriptor 类型范围。
5. 吸收并覆盖 `REQ-030` 中关于 pass-aware binding 解析的要求。

## 需求

### R1: MaterialTemplate 必须构建 pass-aware 的 material interface

`MaterialTemplate` 必须从每个 pass 的 shader reflection 中构建 material-owned binding interface。

这个 interface 必须满足：

- 保留 pass 作用域
- 只纳入 material-owned binding
- 对同名 binding 保留跨 pass 的一致性检查

template 级扁平 `findBinding(name)` 不得继续作为材质系统的权威入口。

### R2: Material-owned binding 的归属必须遵循 REQ-031

`MaterialTemplate` / `MaterialInstance` 在识别材质接口时，必须遵循：

- 保留 system-owned 名字不属于材质
- 其余 descriptor binding 默认归材质

不得再通过 `"MaterialUBO"` 这个名字来推导 ownership。

### R3: MaterialInstance 的参数写入必须按 `bindingName + memberName`

一旦材质可能拥有多个 buffer slot，仅靠 member 名已经不够。

因此 `MaterialInstance` 的 buffer 参数写入接口必须转成：

- 先指定 material-owned buffer binding 名
- 再指定 block/member 名

首版必须提供语义等价于以下形式的接口：

- `setParameter(bindingName, memberName, value)`

如果需要兼容“跨 pass 默认值”的便捷路径，首版可以额外提供：

- `setParameter(pass, bindingName, memberName, value)`
- 或等价的“带默认 pass 语义”的 helper

但不得只保留：

- `setFloat(memberName, value)`

这种只按 member 名寻址的旧合同。

### R4: Descriptor 资源收集必须按目标 Pass 解析

`MaterialInstance::getDescriptorResources(...)` 必须变成 pass-aware。

它返回的材质资源列表必须基于：

- 目标 pass 的实际 reflection bindings
- 目标 pass 下 material-owned bindings 的真实顺序与 set/binding

不得再从 template-global flattened cache 推导排序。

### R5: 多 Pass 同名 binding 冲突必须可检测

如果两个 pass 里存在同名 material-owned binding，但以下任一项不一致：

- descriptor class
- set/binding 语义要求
- buffer size
- reflected members

系统必须给出正式行为。

首版正式行为必须是：

- 允许同名 binding 跨 pass 共存
- 与该 binding 相关的 descriptor 收集、layout 解析、slot 解析必须显式带 pass
- 不得继续保留“最后一个覆盖前面”的静默行为

如果未来需要把“跨 pass 完全一致的同名 binding”提升为共享语义槽，也必须在 pass-aware 查询合同成立之后再做，而不是重新回到 flattened cache。

### R6: MaterialInstance 必须支持多个 material-owned buffer slot

`MaterialInstance` 不得再只持有一份：

- `m_uboBuffer`
- `m_uboBinding`
- `m_uboResource`

而必须能表达：

- 多个按 bindingName 索引的材质 buffer slot
- 每个 slot 独立的 byte buffer / dirty 状态 / resource wrapper

### R7: 首版必须明确支持的 material-owned descriptor 类型

首版 pass-aware material interface 必须正式支持以下 descriptor 类型：

- `UniformBuffer`
- `StorageBuffer`
- `Texture2D`
- `TextureCube`

对上述类型：

- `MaterialTemplate` 必须能把它们纳入 material-owned interface
- `MaterialInstance` 必须能表达、写入或绑定对应资源
- backend resource sync / descriptor bind 路径必须能完成实际绑定

### R8: 未支持的 descriptor 类型必须 fail fast

对以下或类似暂未支持的 descriptor 类型：

- separate sampler
- sampled image without sampler
- storage image
- input attachment

系统必须在 interface 构建、template 构建或实例化阶段明确拒绝。

不得默默跳过或降级成其他类型。

### R9: 旧的 `MaterialUBO` 约定只可作为 shader 名字，不再是系统特例

首版允许 shader 继续声明：

- `uniform MaterialUBO { ... } material;`

但系统对它的理解只能是：

- 一个普通的 material-owned binding，名字刚好叫 `MaterialUBO`

而不能再是：

- 唯一被材质实例自动识别的 special block

## 测试

- 一个 shader 使用 `SurfaceParams` 而不是 `MaterialUBO` 作为 uniform block 名时，材质实例仍可正确构建和写入参数
- 一个材质拥有两个 material-owned buffer binding 时，参数写入能准确落到对应 binding
- `getDescriptorResources(pass)` 在 forward/shadow 两个 pass 下能按各自反射顺序返回正确资源
- 不同 pass 中同名 material-owned binding 若布局不同，pass-aware 查询仍能返回正确结果，且不得发生静默覆盖
- 一个 material-owned storage buffer binding 能被材质接口正确识别并分配/绑定资源
- 遇到未支持 descriptor 类型时，系统给出明确失败

## 修改范围

- `src/core/asset/material_template.*`
- `src/core/asset/material_pass_definition.*`
- `src/core/asset/material_instance.*`
- `src/core/rhi/render_resource.*`
- `src/backend/vulkan/details/resource_manager.*`
- `src/backend/vulkan/details/commands/command_buffer.*`
- `notes/subsystems/material-system.md`
- `openspec/specs/material-system/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`

## 边界与约束

- 本 REQ 不要求额外 material 配置文件；首版仍采用“反射 + 固定保留名字集”模型
- 本 REQ 不要求引入编辑器 UI schema，但允许后续 `yaml` 资产文件承载默认值与 authoring metadata
- 本 REQ 吸收 `REQ-030` 的核心问题；后续实现时应把 `030` 视为被本需求覆盖的前置分析文档
- 本 REQ 定义的是 render-core/runtime 合同，不限定最终 editor/UI 展示形式

## 依赖

- [`REQ-031`](031-global-shader-binding-contract.md)
- [`REQ-030`](finished/030-pass-scoped-material-binding-resolution.md)
- `openspec/specs/material-system/spec.md`

## 后续工作

- [`REQ-033`](033-generic-material-asset-and-defaults.md)：定义通用 material asset、yaml 默认值和通用 loader 合同
- 后续可选 requirement：若未来要支持 separate sampler / storage image / input attachment，再单独扩展

## 实施状态

2026-04-24 已归档。

- `MaterialTemplate::rebuildMaterialInterface()` 构造 canonical material binding 表 + `m_materialBindingIdsByPass` pass 视图；支持类型为 `UniformBuffer` / `StorageBuffer` / `Texture2D` / `TextureCube`；跨 pass 同名 binding 结构不一致时 `fatalBindingConflict(...)` fail-fast
- `MaterialInstance` 用 `m_parameterBuffersByName` 表达多 material-owned buffer slot；对外 API 为 `setParameter(bindingName, memberName, value)` 与 `setTexture(bindingName, ...)`
- `MaterialInstance::getDescriptorResources(pass)` 按 pass canonical binding ids 解析
- 不再依赖 `"MaterialUBO"` 特例；该名字只是普通 material-owned binding
- 未支持的 descriptor 类型在 template 构造阶段 fail-fast
- 对应 spec：`openspec/specs/material-system/spec.md`
- R1–R9 全部完成
