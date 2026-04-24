## Context

当前 `MaterialInstance` 同时维护两层运行时状态：

- 实例级默认参数/纹理
- `PassMaterialOverride` 中的 per-pass 覆盖副本

这种设计允许不同 pass 对同名参数持有不同真值，但代价也很直接：

- 运行时状态来源不唯一，排查“当前 pass 最终吃到什么值”更困难
- `syncGpuData()`、descriptor 组装、slot 生命周期都要同时考虑默认值和 override
- 未来想走 bindless / 资源池化 / descriptor 复用时，不容易把材质实例视为稳定的 canonical 资源集合

从用户需求看，项目希望把材质系统收敛到：

- `MaterialTemplate` 定义结构和约束
- `MaterialInstance` 只保存一份 canonical 参数/资源
- 每个 pass 只声明自己“使用哪些 binding”
- 不再允许 pass-specific 写入 API

这要求我们把“跨 pass 复用是否合法”的问题前置到模板构建和 reflection 校验阶段解决，而不是在实例层靠覆盖机制兜底。

## Goals / Non-Goals

**Goals:**
- 让 `MaterialInstance` 成为唯一的材质运行时真值来源。
- 删除 `PassMaterialOverride` 和所有 pass-specific 参数/纹理写接口。
- 让 `MaterialTemplate` 显式保存 canonical material-owned binding registry，以及每个 pass 对这些 binding 的使用子集。
- 在模板构建时对跨 pass 同名 binding 做强一致性校验，确保复用是安全的。
- 让 `getDescriptorResources(pass)` 成为“按 pass 过滤 canonical 资源”的只读视图，而不是带回退逻辑的多源解析。
- 更新 spec、notes、源码分析和测试，统一系统口径。

**Non-Goals:**
- 不在这次重构里引入真正的 bindless descriptor 模型。
- 不重做 shader reflection 格式；仍然复用现有 `ShaderResourceBinding` / member 信息。
- 不改变 `MaterialPassDefinition`、variant ownership、pipeline identity 的总体归属关系。
- 不试图支持“同一个 `MaterialInstance` 在不同 pass 下同名参数具有不同值”的旧能力；这会被明确移除。

## Decisions

### 1. `MaterialInstance` 只保存一份 canonical parameter/resource set

`MaterialInstance` 中所有 material-owned buffer slot 与 texture binding 都按 binding name 存在唯一条目。所有 setter 都直接写这份 canonical 数据。

选择这个方案，而不是保留 default + override 双层模型，原因是：

- 能把材质实例建模为稳定的“资源集合”，更利于缓存和后续 bindless 演进
- 写路径简单很多，`syncGpuData()` 只需要处理一份数据
- API 语义更清晰：写入就是修改材质实例本身，而不是修改某个 pass 的局部影子状态

被放弃的方案：

- 保留 `PassMaterialOverride` 但限制使用场景。这个方案仍然保留双真值模型，只是把复杂性隐藏起来，长期收益不够。

### 2. Pass 只声明“使用哪些 canonical binding”

`MaterialTemplate` 将保留 per-pass material-owned binding 列表，但新增/强化 canonical 视角：同名 binding 若跨 pass 重复出现，必须表示同一个 canonical binding。

`getDescriptorResources(pass)` 的行为改为：

1. 读取该 pass 的 material-owned binding 使用列表
2. 逐个去 canonical slot / texture 表里查找
3. 返回该 pass 实际声明使用的资源，按反射 set/binding 排序

这样 pass 仍然可以只使用整体参数的一个子集，但不再拥有独立数据副本。

被放弃的方案：

- 让每个 pass 继续持有独立 buffer，再尝试做“尽量复用”。这种方案会让复用退化成实现细节，无法从模型上保证唯一真值。

### 3. 跨 pass 同名 binding 不再“warn and continue”，而是 fail fast

既然同名 binding 被视为同一个 canonical binding，那么它的 descriptor type、buffer size、member layout 就必须兼容。否则就不能安全复用。

因此 `MaterialTemplate::buildBindingCache()` 的跨 pass 校验将从“告警”提升为“assert / fatal fail fast”。

选择 fail fast 的原因：

- 不一致布局如果继续运行，只会把问题推迟到更难诊断的 GPU/descriptor 阶段
- canonical 设计的核心前提就是同名 binding 真正可复用；不兼容就应该被视为 authoring error

### 4. pass-specific 写接口整体移除

以下接口将被删除：

- `setParameter(pass, bindingName, memberName, ...)`
- `setTexture(pass, id, tex)`

保留并继续强化的接口是实例级写入：

- `setParameter(bindingName, memberName, ...)`
- `setTexture(id, tex)`
- 旧 convenience setter（仅在无歧义时有效）

原因是用户已经明确确认：从 API 语义上不需要 pass-specific 设置。保留这些接口只会暗示“per-pass 真值”仍然存在。

### 5. Reflection 的职责是“建立兼容性和映射”，不是保存多份真实数据

不同 pass 可以使用不同 shader；shader reflection 负责回答两个问题：

- 这个 pass 声明的 binding 是否属于同一个 canonical binding
- 这个 pass 需要的 binding/member 定义是否与 canonical 定义一致

对于当前实现，这意味着可以继续按 binding name 直接复用 slot，因为同名 binding 已要求完全兼容。后续如果需要扩展到“逻辑参数名相同但物理布局不同”的模型，可以再在 template 层引入显式 mapping；这不在本次重构范围内。

## Risks / Trade-offs

- [失去 per-pass 参数差异能力] → 明确把它定义为不再支持；确有需要时使用不同 binding 名或不同 `MaterialInstance`。
- [历史 shader/材质资产里可能存在同名但不兼容 binding] → 在模板构建时 fail fast，并通过测试与 loader 路径尽早暴露。
- [API 变更影响调用点和测试] → 统一删除 pass-specific setter，编译期即可暴露大多数遗漏。
- [文档/源码分析容易残留旧模型表述] → 在同一轮里同步更新 subsystem notes、source analysis、注释和 spec。

## Migration Plan

1. 先更新 OpenSpec proposal/design/spec/tasks，固定 canonical model 的正式术语。
2. 重构 `MaterialInstance`：删除 `PassMaterialOverride`、`m_passOverrides` 以及相关查找/写入逻辑。
3. 重构 `MaterialTemplate::buildBindingCache()`：保留 per-pass usage list，同时构建/校验 canonical binding 约束。
4. 更新 descriptor 组装逻辑、测试和调用点，移除 pass-specific setter 使用。
5. 更新 notes / source analysis / 注释，说明 canonical parameter model。
6. 编译并运行关键材质/反射/frame-graph 测试确认行为稳定。

回滚策略很直接：若重构暴露出无法立即修复的资产兼容问题，可整体回退该 change；由于 API 是编译期 breaking change，不适合部分回滚。

## Open Questions

- 当前不保留开放问题。本次 change 直接以“单一 canonical 参数集、无 pass-specific setter”作为定案实现。
