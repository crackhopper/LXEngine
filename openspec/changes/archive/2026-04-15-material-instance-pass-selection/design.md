## Context

REQ-022 要解决的不是“再加一个布尔开关”，而是把材质系统里 pass 的两层语义彻底分开：

- `MaterialTemplate` 定义材质蓝图能支持哪些 pass
- `MaterialInstance` 决定当前实例实际启用了哪些 pass

当前代码已经有一半在正确方向上：

- `MaterialTemplate` 已经按 `pass -> RenderPassEntry` 建模；
- `MaterialInstance::getRenderSignature(pass)` 已经是 pass-aware；
- `SceneNode` 的 validated cache 和 `supportsPass(pass)` 也已经有 pass 级入口。

但剩下的一半仍停留在旧模型里：

- `MaterialInstance::m_passFlag` 还在充当“真实启用集合”的唯一来源；
- `getRenderState()` 仍然是 Forward-only 过渡接口；
- `setPassEnabled(...)` 虽已存在实现雏形，但未强制 template existence、默认启用规则和 scene-level propagation 边界；
- 共享 `MaterialInstance` 时，结构性变化如何影响多个 `SceneNode` 仍缺少明确契约。

这个变更跨越 `core/asset/material.*`、`core/scene/`、queue 过滤语义和设计文档，属于一次模型收敛，而不是孤立 API 补丁。

## Goals / Non-Goals

**Goals:**

- 明确 `MaterialTemplate` / `MaterialInstance` 在 pass 维度上的所有权边界。
- 让新建 `MaterialInstance` 默认启用 template 中定义的全部 pass。
- 让 `setPassEnabled(pass, ...)` 成为 instance 级唯一显式入口，并对未定义 pass 直接 `FATAL + terminate`。
- 让 `getPassFlag()` 成为启用 pass 集合的派生结果，而不是独立真值。
- 收敛 `getRenderState()` 到 pass-aware 查询。
- 让 `SceneNode` 校验、`supportsPass(pass)` 与 scene-level propagation 一致地尊重实例级 pass 状态。
- 明确普通材质参数更新不属于结构性变化。

**Non-Goals:**

- 本期不支持 `MaterialTemplate` 的运行时结构性热修改。
- 本期不做材质编辑器 UI、资产序列化或 pass enable 持久化格式设计。
- 本期不要求实现事务式回滚；若 pass 状态变化后任一关联节点非法，直接 `FATAL + terminate`。
- 本期不要求新的 scene-wide 索引结构必须一步到位，首版允许扫描实现。

## Decisions

### Decision: `MaterialTemplate` 是静态 pass 蓝图，`MaterialInstance` 只持有启用子集

`MaterialTemplate` 继续独占：

- pass 列表
- `RenderPassEntry`
- pass-specific shader / variants / render state

`MaterialInstance` 只持有“当前启用了模板里哪些 pass”的状态，不允许启用 template 未定义的 pass。

这样做的原因：

- pass 的定义是结构性蓝图，适合 loader 一次性构造；
- instance 只表达使用子集，才能支持“同模板不同实例启停不同 pass”的需求；
- 避免把 pass-specific shader 配置继续散落到 `SceneNode` 或调用点。

备选方案：

- 把 pass enable 继续折叠进 `ResourcePassFlag`。
  该方案无法回答“template 根本没定义这个 pass”这种错误场景，也不利于共享实例行为建模，不采用。

### Decision: 默认启用全部 template passes，`m_passFlag` 退化为派生缓存

新建 `MaterialInstance` 时，默认启用 template 中的所有已定义 pass。`getPassFlag()` 每次都由“定义且启用”的 pass 集合推导，或维护一个严格从该集合重建的缓存；它不再是可独立写入的真值。

这样做的原因：

- “默认启用全部”最符合 template 是能力上限、instance 是默认使用全集的直觉；
- 保留 `ResourcePassFlag` 派生结果可以减少对旧接口的冲击；
- 只允许单向派生，能消除“集合和 bitmask 脱节”的状态分叉。

备选方案：

- 默认全部关闭，由调用方逐个启用。
  这会把 loader / scene 初始化全面推向显式配置，迁移成本更高，不采用。

### Decision: 未定义 pass 的 enable/disable 请求视为程序员错误

`setPassEnabled(pass, bool)` 对 template 未定义的 pass 统一执行 `FATAL + terminate`。不返回 `false`，也不静默忽略。

这样做的原因：

- 这类错误本质上是 API 使用方与材质蓝图的契约不一致；
- 如果静默忽略，会让 `supportsPass(pass)`、queue 过滤和节点合法性变得更难调试；
- 项目现有结构性错误策略已经统一走 fatal。

备选方案：

- 未定义 pass 返回 `false` 或 no-op。
  会掩盖编程错误，不采用。

### Decision: pass enable 变化是 scene-level 结构性传播事件

`MaterialInstance` 不维护反向 `SceneNode` 引用。共享实例的影响传播由 `Scene` 负责，例如：

- `Scene::revalidateNodesUsing(materialInstance)`

`MaterialInstance` 只提供 pass-state change 通知或显式调用入口；`Scene` 决定如何找到所有受影响节点并触发重校验。

这样做的原因：

- ownership 在场景层，反向关系也应由场景层掌握；
- 共享 `MaterialInstance` 是允许的，pass enable 变化必须等价地影响所有引用节点；
- 先允许扫描实现，可以在不引入复杂索引的情况下把语义钉死。

备选方案：

- `MaterialInstance` 直接维护 `SceneNode*` 反向列表。
  会引入额外生命周期耦合，违背当前分层，不采用。

### Decision: 普通参数更新不触发结构性重校验

`setFloat/setInt/setVec*/setTexture/updateUBO` 保持在“资源内容变化”语义里，不触发 `SceneNode` 重校验。只有 mesh/material/skeleton/pass enable 状态变化属于结构性变化。

这样做的原因：

- pass enable 会改变对象是否参与某个 pass，确实影响结构；
- UBO/texture 内容改变不改变 pass 参与关系，只影响 draw-time 数据；
- 把两类变化分开，才能保证结构性缓存稳定且性能可控。

备选方案：

- 所有材质更新都触发节点重校验。
  代价高且噪声大，不采用。

## Risks / Trade-offs

- [Risk] 共享 `MaterialInstance` 的 pass enable 修改会一次性影响多个节点，调用方可能低估影响面。 → Mitigation: 在 specs 和设计文档里明确“共享实例意味着共享结构性 pass 状态”。
- [Risk] 场景层扫描受影响节点在大场景里可能变慢。 → Mitigation: 首版允许扫描实现，若出现性能压力再引入 `MaterialInstance -> SceneNode[]` 索引。
- [Risk] `getRenderState()` 改成 pass-aware 之后，旧调用点可能继续假设 Forward-only。 → Mitigation: 在任务里要求先收敛 API，再逐个迁移调用点和测试。
- [Risk] 若某实例关闭某个 pass 后 UBO layout 选择逻辑仍依赖“已启用 pass”，可能暴露多 pass UBO layout 不一致问题。 → Mitigation: 明确要求已启用 passes 的 `MaterialUBO` 布局一致，并在切换 pass 状态时重新验证该前提。

## Migration Plan

1. 先在 OpenSpec 中固定 template/instance/scene 的职责边界和错误策略。
2. 调整 `MaterialInstance` API 与内部状态模型，确保默认启用全部 template passes。
3. 收敛 `getPassFlag()` / `getRenderState()` 到新的 pass-aware 语义。
4. 调整 `SceneNode`、`supportsPass(pass)` 和 scene-level propagation。
5. 更新测试、设计文档和 notes。

回滚策略：

- 若迁移过程中调用点过多，可短期保留兼容桥接层，但不得恢复“独立真值 `m_passFlag`”模型。

## Open Questions

- `IMaterial::getRenderState()` 是直接改成 `getRenderState(StringID pass)`，还是保留无参版本作为兼容桥接并强制内部走 pass-aware 入口。
- `Scene::revalidateNodesUsing(materialInstance)` 首版是否需要去重受影响节点，还是按现有 renderable 容器线性扫描即可。
