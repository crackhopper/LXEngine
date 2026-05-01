# REQ-022: Material Pass 选择与 Instance 级 Pass Enable/Disable

## 背景

当前材质系统的 pass 职责分布并不对称：

- `MaterialTemplate` 已经通过 `m_passes: unordered_map<StringID, MaterialPassDefinition>` 表达“这个材质蓝图有哪些 pass，每个 pass 用什么 shader + render state”
- `MaterialInstance` 内部持有 `MaterialTemplate`
- 但 `MaterialInstance` 只有一个整体 `ResourcePassFlag m_passFlag`，它只能粗粒度表达“这个材质实例参与哪些 pass mask”

这带来两个问题：

1. pass 的定义与 pass 的启停不在同一条抽象链上
2. `MaterialInstance` 不能显式表达“模板支持 `Forward + Shadow`，但这个实例只开 `Forward`，另一个实例开两个 pass”
3. `MaterialInstance::getRenderState()` 当前仍然是 Forward-only 过渡实现，与 pass-aware 的 template / signature 体系不对称

随着多 pass 渲染推进，这会成为一个稳定的架构缺口。

## 已确认结论

- 一组相互配合的 shader + material 蓝图是绑定的，应由专门 loader 构造
- pass 的**定义**属于 `MaterialTemplate`
- `SceneNode` 持有 `MaterialInstance`
- renderable/material 的合法性校验必须覆盖该对象声明参与的所有 pass，而不是只看 `Forward`

## 目标

1. 明确 `MaterialTemplate` 与 `MaterialInstance` 在 pass 维度上的职责边界
2. 允许 `MaterialInstance` 在模板支持的 pass 集合内，按实例启用或禁用某个 pass
3. 让 `SceneNode` / `IRenderable::supportsPass(pass)` 与材质 pass 状态对齐
4. 把这个模型写进材质系统 level-1 文档

## 设计判断

### D1: pass 定义属于 `MaterialTemplate`

`MaterialTemplate` 负责定义：

- 有哪些 pass
- 每个 pass 的 `MaterialPassDefinition`
- 对应的 shader program / variants / render state

这是材质蓝图的一部分，应该由 loader 一次性构造完成。

### D2: pass 启停属于 `MaterialInstance`

`MaterialInstance` 负责表达：

- 在模板已定义的 pass 集合中，当前实例实际启用了哪些 pass

这意味着：

- instance **不能**启用模板里不存在的 pass
- instance **可以**禁用模板里已定义的 pass

推荐语义：

- template 是“能力上限”
- instance 是“实际使用子集”

### D3: `ResourcePassFlag` 不能继续作为唯一来源

当前 `MaterialInstance::m_passFlag` 是一个整体 bitmask。它能做粗粒度过滤，但不足以表达完整材质 pass 语义，因为：

- 它不能验证模板里是否真的存在对应 `MaterialPassDefinition`
- 它没有清晰回答“instance 关闭了某个模板 pass”这件事
- 它和 `StringID pass` 驱动的 render signature / frame graph pass 常量并不完全同构

因此本需求要求新增一个更明确的 instance-pass 状态表示，`m_passFlag` 可以保留为兼容层或派生缓存，但不能再是唯一真值。

### D4: `supportsPass(pass)` 必须尊重实例级 pass enable 状态

当 `RenderQueue` 或未来的高层 `SceneNode` 询问某个对象是否支持 `Pass_Shadow` 时，判断应至少同时满足：

1. `MaterialTemplate` 定义了该 pass
2. `MaterialInstance` 启用了该 pass
3. `SceneNode` 自身的结构性校验在该 pass 下成立

### D5: pass 状态变化的影响传播属于场景层，不属于 `MaterialInstance`

当 `MaterialInstance` 的 pass enable 状态变化时，受影响的是所有引用它的 `SceneNode`。

这层关系应由场景层或等价的上层拥有者负责，而不是下沉到 `MaterialInstance`：

- `MaterialInstance` 不维护反向引用列表
- `Scene` 或等价场景拥有者负责知道“哪些 `SceneNode` 正在使用哪个 `MaterialInstance`”
- 当 pass enable 状态变化时，由场景层触发相关 `SceneNode` 的重新校验

实现方式可以有两种：

- 简单实现：在场景节点容器上扫描受影响对象
- 优化实现：维护 `MaterialInstance -> SceneNode[]` 索引

本需求约束职责边界，不强制首版采用哪一种实现。

### D6: pass enable 变化是结构性变化，普通参数变化不是

`MaterialInstance` 上至少有两类变化：

- 结构性变化：会改变对象是否参与某个 pass，或改变其需要通过哪些 pass 的合法性校验
- 普通参数变化：只影响 UBO/texture 内容，不改变 pass 参与关系

本需求确定：

- pass enable/disable 属于结构性变化
- `setFloat/setInt/setVec*/setTexture/updateUBO` 这类参数更新不属于结构性变化

因此：

- 前者需要由场景层传播并触发关联 `SceneNode` 重新校验
- 后者只走现有 dirty / descriptor 更新路径

### D7: `MaterialTemplate` 运行时视为静态蓝图

本期不把 `MaterialTemplate` 当成运行时可热修改对象处理。

因此以下能力不纳入本期：

- 运行时动态新增/删除 pass
- 运行时替换某个 pass 的 shader/program
- 运行时重写 template 级 variants

如果未来需要 template 热变更或热重载，应单独立项。

## 需求

### R1: `MaterialTemplate` 是 pass 蓝图所有者

- `MaterialTemplate` 继续持有 pass → `MaterialPassDefinition` 映射
- loader 负责创建完整的 pass 蓝图
- 任意 pass-specific shader / variant / render state 只在 template 上定义

### R2: `MaterialInstance` 必须提供 pass enable/disable 能力

新增 instance 级 API，语义至少覆盖：

- `isPassEnabled(pass)`
- `setPassEnabled(pass, bool)` 或等价接口
- 可查询当前实例启用的 pass 集合

约束：

- 若 `pass` 不存在于 template，启用请求必须失败（assert / fatal，策略由实现文档决定）
- 新建 `MaterialInstance` 时，默认启用集合应有明确规则，并在文档中写死
- pass enable 状态允许运行时动态修改，但只能通过显式 API 修改
- pass enable 状态变化属于结构性变化

推荐默认规则：

- 默认启用 template 中定义的全部 pass

错误策略：

- 若调用方尝试对 template 未定义的 pass 执行 `setPassEnabled(pass, ...)`
- 视为编程错误
- 统一采用 `FATAL + terminate`
- 不做静默忽略，也不返回 `false`

共享语义：

- `MaterialInstance` 可以被多个 `SceneNode` 共享
- 运行时修改其 pass enable 状态，会同时影响所有引用该实例的 `SceneNode`
- 若只希望影响单个对象，应为该对象使用独立的 `MaterialInstance`

### R3: `getPassFlag()` 与 pass enable 状态保持一致

在保留 `ResourcePassFlag` 兼容路径的前提下：

- `MaterialInstance::getPassFlag()` 必须由“template 已定义且 instance 已启用”的 pass 集合推导
- 不允许手工维护一个与真实 pass enable 集合分离的独立 bitmask 真值

### R4: `getRenderState()` 必须收敛为 pass-aware 接口

- `MaterialInstance::getRenderState()` 不得继续保留 Forward-only 过渡语义
- 材质 render state 查询必须按 `pass` 进行
- `SceneNode` 的 validated summary 与 `RenderQueue` 的 item 构建链路必须消费 pass-aware render state
### R5: `SceneNode` / `IRenderable::supportsPass(pass)` 必须接入材质实例状态

`supportsPass(pass)` 的判断不得只看粗粒度 pass mask，还必须考虑：

- material instance 是否启用该 pass
- material template 是否定义该 pass

如果某 pass 未启用，则该节点不得进入对应 queue。

### R6: 合法性校验覆盖所有已启用 pass

`SceneNode` 创建后，以及 `mesh/material/skeleton/pass-enable-state` 发生结构性变化后，系统必须对**所有已启用 pass**重新执行校验。

例如：

- `Forward` 合法但 `Shadow` 缺少所需 vertex input → 节点非法
- 某实例关闭 `Shadow` 后，`Shadow` 不再参与该节点合法性要求

换句话说：

- 校验范围只覆盖“当前已启用的 pass”
- 已关闭的 pass 只表示“不参与渲染”，不再构成该节点非法的理由
- 若运行时修改 pass enable 状态导致任一关联 `SceneNode` 在新的已启用 pass 集合下校验失败，统一采用 `FATAL + terminate`

### R7: 普通材质参数更新不触发结构性重校验

- `setFloat/setInt/setVec4/setVec3/setTexture/updateUBO` 不属于结构性变化
- 这些操作不得触发 `SceneNode` 合法性重校验
- 它们继续走现有资源 dirty / descriptor 更新路径
### R8: pass 状态变化的传播由场景层负责

- `MaterialInstance` 不得维护 `SceneNode` 反向引用
- `Scene` 或等价场景拥有者必须负责把 `MaterialInstance` 的 pass 状态变化传播到受影响节点
- 首版实现允许通过扫描或索引两种方式找到受影响节点
- 无论采用哪种方式，行为必须等价：所有引用该 `MaterialInstance` 的 `SceneNode` 都要被重新校验
- 推荐提供直接语义化接口，例如 `Scene::revalidateNodesUsing(materialInstance)`
- 该重校验过程不要求事务式回滚；若任一受影响节点校验失败，统一 `FATAL + terminate`
### R9: `MaterialTemplate` 运行时不支持结构性热修改

- `MaterialTemplate` 在本期视为静态蓝图
- 运行时动态新增/删除 pass 不属于本期能力
- 运行时替换某 pass 的 shader/program 不属于本期能力
- 若需要 template 热变更，应单独立需求

### R10: loader 负责构造“成套”的 template

每个材质 loader 应负责构造一套互相配合的：

- pass 集合
- shader 组合
- variants
- render state

禁止把 pass-specific shader 配置散落到 `MaterialInstance` 或 `SceneNode` 上临时拼装。

### R11: 文档同步

以下文档必须更新：

- `docs/design/MaterialSystem.md`：level-1 总览页
- `notes/subsystems/material-system.md`：当前实现说明
- 若材质类型 / `IRenderable::supportsPass` 语义变化明显，补充更新术语表与相关架构文档

## 当前实现事实

以下是当前代码库中已经成立、需要作为需求输入的事实：

- `MaterialTemplate::setPass/getEntry/getRenderPassSignature` 已存在
- `blinn_phong_material_loader` 当前只创建了 `Pass_Forward`
- `MaterialInstance::getRenderSignature(pass)` 是 pass-aware 的
- `MaterialInstance::getRenderState()` 当前仍然是 Forward-only 过渡实现，本需求要求收敛
- `MaterialInstance` 当前**没有** instance 级 pass enable/disable API
- `IRenderable::supportsPass(pass)` 当前主要依赖 `getPassMask() & passFlagFromStringID(pass)`

## 非目标

- 本期不要求完成完整 editor/runtime 材质 UI
- 本期不要求把 pass enable 状态序列化进资产文件
- 本期不要求实现 pass 级热重载

## 修改范围

预期至少涉及：

- `src/core/asset/material.{hpp,cpp}`
- 高层 `IRenderable` / `SceneNode` 抽象
- `RenderQueue` pass 过滤路径
- 材质 loader
- 材质系统相关文档

## 2026-04-16 核查结果

### 需求核对

| Requirement | Status | Notes |
|-------------|--------|-------|
| R1 | 已实现 | `MaterialTemplate` 继续持有 `pass -> MaterialPassDefinition` 映射 |
| R2 | 已实现 | `MaterialInstance` 提供 `isPassEnabled` / `setPassEnabled` / `getEnabledPasses`，未定义 pass 走 `FATAL + terminate` |
| R3 | 已实现 | `getPassFlag()` 由“已定义且已启用”的 pass 集合推导 |
| R4 | 已实现 | 材质查询面已收敛到 `MaterialInstance::getRenderState(pass)`，删除 Forward-only 兼容语义 |
| R5 | 已实现 | `SceneNode::supportsPass(pass)` 同时受 instance enable 状态和 validated cache 约束 |
| R6 | 已实现 | `SceneNode::rebuildValidatedCache()` 覆盖所有已启用 pass |
| R7 | 已实现 | 普通材质参数写入不触发 pass-state listener |
| R8 | 已实现 | `Scene::revalidateNodesUsing(materialInstance)` 负责共享实例传播 |
| R9 | 已实现 | `MaterialTemplate` 仍按静态蓝图处理 |
| R10 | 已实现 | loader 继续负责构造完整 template；当前 `blinnphong` loader 只产出 Forward pass |
| R11 | 已实现 | `docs/design/MaterialSystem.md` 与 `notes/subsystems/material-system.md` 已同步当前模型 |

### 本次补齐

- 删除无参 `getRenderState()` 的 Forward-only 兼容入口，避免继续保留旧语义。
- 把空 template 下的 `MaterialInstance::getPassFlag()` 收敛为“空 pass 集合”而不是默认 `Forward`。
- 同步 level-1 材质文档的实现状态描述，移除已过时的“SceneNode 校验尚未完整建立”说法。

### 验证建议

- `test_material_instance`
- `test_scene_node_validation`
