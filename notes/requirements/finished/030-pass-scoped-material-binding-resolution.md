# REQ-030: 材质 binding 查找改为按 Pass 作用域解析

> 状态说明：本 REQ 的核心要求已被 [`REQ-032`](032-pass-aware-material-binding-interface.md) 吸收。
> 后续实施请以 `REQ-032` 为主；本文件保留为问题定义、设计背景与迁移约束。

## 背景

当前代码同时存在两套 binding cache：

- `MaterialPassDefinition::bindingCache`：单个 pass shader 的完整反射绑定
- `MaterialTemplate::m_bindingCache`：把 template shader 和各个 pass shader 的反射绑定按名字压平成一张表

这两层缓存的职责并不相同。前者天然保留了 pass 作用域，后者则只适合“整个模板里，同名 binding 总是同一含义、同一位置”的简单情况。

问题在于，`MaterialInstance::setTexture()` 和 `getDescriptorResources()` 当前走的是 `MaterialTemplate::findBinding(id)`。一旦一个材质模板拥有多个 pass，并且不同 pass 中出现同名 binding 但位于不同的 `set/binding`，template 级扁平缓存就会发生覆盖，材质侧将无法准确知道当前名字对应的是哪一套 layout。

这不是 pipeline 构建入口本身的错误。当前 pipeline build 主要还是直接读取实际 shader 的 `getReflectionBindings()`。真正失真的，是材质系统自己的“按名字到 binding”的解析合同。

## 目标

1. 让材质系统对 binding 的名字解析保留 pass 作用域。
2. 明确 template 级缓存与 pass 级缓存各自的职责。
3. 避免多 pass 材质在同名 binding 不同布局时发生覆盖或错误排序。

## 需求

### R1: Pass 级 binding 信息必须是材质系统的权威来源

- `MaterialPassDefinition` 继续持有该 pass shader 的完整反射 binding 集。
- 对 descriptor layout、资源匹配、运行时 binding 解析这类与某个 pass 强相关的行为，系统必须能够拿到“指定 pass 下”的 binding 信息。
- 文档不得再把 `MaterialPassDefinition::bindingCache` 描述成过渡性缓存。

### R2: Template 级 binding 查找不得丢失 pass 作用域

系统必须满足以下二选一中的至少一种正式合同：

- `MaterialTemplate` 提供 `findBinding(pass, id)` 之类的 pass-aware 查询接口
- 或者 template 级缓存直接改成以 `pass + bindingName` 为 key 的结构化索引

无论采用哪种形式，都必须保证：当不同 pass 中存在同名 binding 但 `set/binding` 不同时，查询结果不会互相覆盖。

### R3: 材质实例的纹理绑定路径必须按目标 Pass 解析

- `MaterialInstance::setTexture(...)` 与 `getDescriptorResources(...)` 相关路径必须能在目标 pass 下解析 binding。
- 如果当前 API 只接受一个名字而不带 pass，则必须明确这是否只支持“所有 pass 同名 binding 完全一致”的受限合同；若不是，就应补充 pass-aware API。
- descriptor 资源排序所使用的 `set/binding` 信息，必须来自目标 pass 的反射结果。

### R4: 多 Pass 同名 binding 冲突必须可检测

- 当一个模板的多个 pass 存在同名 binding 但布局不一致时，系统必须有明确行为。
- 可接受的行为包括：
  - 在构建 template 时直接拒绝并报错
  - 允许存在，但要求所有查询都带 pass
- 不允许继续使用“最后写入覆盖前者”的静默行为作为正式合同。

`REQ-032` 选择的正式方向是：

- 保留同名 binding 跨 pass 共存
- 与 pass 强相关的查询和 descriptor 收集必须显式带 pass
- 不再允许 template-global flattened cache 成为材质 binding 解析的权威入口

### R5: 文档与 spec 需要同步校正

- `notes/concepts/material/` 必须说明 pass 级 binding 才是多 pass 模型的完整表达。
- `notes/subsystems/material-system.md` 和相关 spec 需要明确 template 级缓存的限制或新合同。

## 修改范围

- `src/core/asset/material_template.hpp`
- `src/core/asset/material_instance.hpp`
- `src/core/asset/material_instance.cpp`
- `src/core/asset/material_pass_definition.hpp`
- `notes/concepts/material/`
- `notes/subsystems/material-system.md`
- `openspec/specs/material-system/spec.md`

## 依赖

- `openspec/specs/material-system/spec.md`
- [`REQ-025`](025-custom-material-template-and-loader.md)
- [`REQ-022`](022-material-pass-selection.md)

## 与后续 REQ 的关系

- [`REQ-031`](031-global-shader-binding-contract.md) 定义 binding ownership 的规则来源。
- [`REQ-032`](032-pass-aware-material-binding-interface.md) 吸收本 REQ 的实现合同，并把它扩展为完整的 pass-aware 材质接口。
- [`REQ-033`](033-generic-material-asset-and-defaults.md) 在 `REQ-032` 之上定义通用 material asset / defaults / loader 合同。

## 实施状态

已并入 `REQ-032`，不单独实施。
