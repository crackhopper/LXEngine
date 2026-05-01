# REQ-024: 移除 `RenderableSubMesh` 兼容抽象

## 背景

当前场景层同时存在两条对象装配路径：

- `SceneNode`：主路径，实现了 pass 级结构校验、validated cache、scene 级重验证传播，以及稳定的调试标识
- `RenderableSubMesh`：旧路径，直接持有 `mesh/material/skeleton/objectPC`，在 `getValidatedPassData(pass)` 时临时拼一份 `ValidatedRenderablePassData`

从当前代码看，`RenderableSubMesh` 已经不再承担独立的架构职责：

1. 它不维护自己的结构性不变量
2. 它不持有 pass 级 validated cache
3. 它不接入 `Scene` 的 material pass-state 重验证传播
4. 它绕过了 `SceneNode::rebuildValidatedCache()` 中已经建立的 fatal 校验链
5. 它保留的是“queue/backend 需要什么就临时展开什么”的旧模型，而不是当前场景层的主抽象

`notes/subsystems/scene.md` 也已经把 `RenderableSubMesh` 明确标为“兼容实现，但不再是推荐的场景主模型”。

这意味着：继续保留这个类型，会让同一层级长期并存两套语义不等价的 renderable 模型，并把未来所有场景/材质/pass 相关演进都拖入“双轨维护”。

## 已确认结论

- `SceneNode` 已是当前推荐主路径
- `RenderableSubMesh` 当前实现只剩兼容层价值
- 对象合法性校验应发生在对象装配阶段，而不是 queue 构建时临时兜底
- `IRenderable` 的高层语义应该统一映射到“已通过结构校验的场景节点”，而不是“随时可展开的一组裸资源”

## 目标

1. 消除 `RenderableSubMesh` 这个与 `SceneNode` 语义重叠的兼容抽象
2. 统一 renderable 主模型到 `SceneNode`
3. 消除“主路径有 fatal 校验，兼容路径无校验”的行为分叉
4. 降低后续 scene/material/pass 演进时的双轨维护成本
5. 把迁移边界和替换规则写成正式需求，避免后续实现时摇摆

## 设计判断

### D1: `RenderableSubMesh` 是冗余抽象，不是必要层级

`RenderableSubMesh` 并没有引入新的领域概念。它本质上只是：

- 聚合 `mesh`
- 聚合 `material`
- 可选聚合 `skeleton`
- 向 queue 暴露 vertex/index/descriptors/objectPC/shader 等展开结果

而这些职责，`SceneNode` 已经以更严格的方式覆盖，并且补上了：

- pass-aware 的结构校验
- validated cache
- scene 集成
- 调试标识

因此 `RenderableSubMesh` 不是“另一种必要对象类型”，而是历史迁移期间留下的薄包装层。

### D2: 兼容层不应长期成为正式模型

短期保留兼容层可以降低迁移成本；但一旦主路径已经稳定，继续长期保留兼容层会带来三个明确问题：

1. 新需求必须同时回答“`SceneNode` 怎么做”和“`RenderableSubMesh` 怎么兼容”
2. 测试矩阵被迫扩成双份
3. 调用方可能继续绕过校验主路径，制造新的架构漂移

因此本需求要求把兼容层视为待删除对象，而不是永久保留接口。

### D3: `SceneNode` 应成为唯一的场景级 renderable 实现

场景层对“可渲染对象”的正式表达应统一为：

- 显式 `nodeName`
- `MeshPtr`
- `MaterialInstance::Ptr`
- 可选 `SkeletonPtr`
- `ObjectPCPtr`
- pass -> validated summary 缓存

若未来还需要“组合对象”“实例化对象”“程序化对象”等新 renderable 形态，它们也必须满足同一条高层 contract，而不是回退到 `RenderableSubMesh` 这种绕过校验的旧形态。

### D4: 删除 `RenderableSubMesh` 不等于删除兼容迁移策略

本需求允许在实施过程中设置有限的迁移阶段，例如：

- 先把调用点全部切到 `SceneNode`
- 再删除 `RenderableSubMesh` 构造入口
- 最后移除 legacy helper 和相关测试

但这些阶段只能服务于删除目标，不得把“临时适配层”重新固化为长期 API。

## 需求

### R1: `SceneNode` 成为唯一推荐且唯一长期保留的场景对象模型

- 场景层正式支持的 renderable 主模型必须统一为 `SceneNode`
- `RenderableSubMesh` 不得继续作为与 `SceneNode` 并列的长期对象模型保留
- 新增场景对象、教程、测试、示例代码必须基于 `SceneNode`

### R2: 所有 `RenderableSubMesh` 调用点必须迁移到 `SceneNode`

至少包括以下范围：

- `src/test/` 中仍直接构造 `RenderableSubMesh` 的集成测试
- tutorial / notes / 示例代码中的旧构造路径
- scene 初始化路径中任何仍假定 legacy renderable 的辅助逻辑

迁移后，调用方必须显式提供 `nodeName`，并使用 `SceneNode` 的构造或工厂入口。

### R3: `IRenderable` 的对外语义不得再为 `RenderableSubMesh` 保留特殊分支

- `Scene`、`RenderQueue`、frame graph、renderer 初始化路径不得再包含“如果是 `RenderableSubMesh` 就走兼容逻辑”的特殊分支
- `IRenderable::getValidatedPassData(pass)` 的正式语义应建立在稳定的 validated cache 之上
- 运行时不得再依赖 legacy 即时拼装的 `buildLegacyValidatedData(...)`

### R4: `RenderableSubMesh` 删除后不得丢失现有主路径校验能力

删除 legacy 抽象后，系统必须继续保留并强化以下行为：

- 对象装配时执行结构性校验
- 结构性成员变化时重新校验
- shared `MaterialInstance` pass 状态变化时，由 `Scene` 传播重验证
- queue 只消费已通过校验的 renderable validated 结果

换句话说，迁移的目标是“全部收敛到 `SceneNode` 行为模型”，不是把主路径降级成 legacy 行为。

### R5: `RenderableSubMesh` 类型及其 legacy helper 必须最终删除

本需求完成后，以下内容不得继续保留在主代码路径中：

- `RenderableSubMesh` 类型定义
- `buildLegacyValidatedData(...)`
- 为 legacy renderable 自动补默认名字或 legacy debug id 的逻辑
- 任何仅为兼容 `RenderableSubMesh` 而存在的测试或文档叙述

若实施期间需要短期兼容适配，必须在设计/任务文档中明确标注其删除时点。

### R6: 文档必须统一收敛到 `SceneNode`

以下文档至少需要同步：

- `notes/subsystems/scene.md`
- `notes/concepts/scene-object.md`
- `notes/concepts/mesh-object.md`
- `notes/concepts/material-object.md`
- 仍指导用户构造 `RenderableSubMesh` 的 tutorial/roadmap 文档

更新要求：

- 去掉把 `RenderableSubMesh` 作为正常入口的叙述
- 若需要保留历史说明，应明确写成“legacy/已废弃迁移路径”

### R7: 迁移后测试必须只验证统一对象模型

测试覆盖至少包括：

- `SceneNode` 基本构造和 `supportsPass(pass)` 行为
- `SceneNode` 的 validated pass data 能驱动 queue/item 构建
- 结构性变化触发重校验
- shared material pass enable/disable 通过 `Scene` 传播重验证
- 删除 `RenderableSubMesh` 后，现有 renderer / frame graph / pipeline 相关测试仍通过

同时要求：

- 不再新增任何专门针对 `RenderableSubMesh` 的行为测试
- 旧测试若只是验证 legacy 构造能跑通，应改写为 `SceneNode` 版本

## 当前实现事实

- `src/core/scene/object.hpp` 中 `RenderableSubMesh` 仍是 `IRenderable` 的一个具体实现
- `src/core/scene/object.cpp` 中 `RenderableSubMesh::getValidatedPassData(pass)` 通过 `buildLegacyValidatedData(...)` 即时构造结果
- `RenderableSubMesh::supportsPass(pass)` 目前只看 pass mask，不依赖 `SceneNode` 的 validated cache 语义
- 多处集成测试仍直接 `std::make_shared<RenderableSubMesh>(...)`
- 现有 notes / tutorial 仍把 `RenderableSubMesh` 暴露为用户可直接创建的对象

## 非目标

- 本期不引入真正的树状 scene graph / transform hierarchy
- 本期不重命名 `IRenderable`
- 本期不扩展新的 renderable 类型层级
- 本期不改变 `SceneNode` 已有的核心职责边界，只要求把 legacy 路径收敛进去

## 修改范围

预期至少涉及：

- `src/core/scene/object.{hpp,cpp}`
- `src/core/scene/scene.{hpp,cpp}`
- `src/test/`
- `notes/subsystems/scene.md`
- `notes/concepts/`
- `notes/tutorial/`

## 边界与约束

- 删除 `RenderableSubMesh` 不能靠回退 `SceneNode` 能力来完成
- 不允许因为迁移成本而长期保留“双轨对象模型”
- 若有必须分阶段推进的原因，阶段性兼容层必须明确 sunset 计划
- 迁移过程中不得破坏现有 `SceneNode` 的 fatal 校验语义

## 依赖

- REQ-021 已经确立 `SceneNode` 为主路径并把对象合法性校验前移
- [`REQ-022`](finished/022-material-pass-selection.md) 已确立 material pass enable/disable 与 `SceneNode` 的交互边界
- 当前 `notes/subsystems/scene.md` 已把 `RenderableSubMesh` 定义为兼容实现，这为删除 legacy 抽象提供了文档前提

## 下游工作

- 需要一个对应的实现 change / 任务拆分来逐步迁移调用点并删除 legacy 类型
- 文档索引与教程需要同步刷新，避免用户继续沿用旧入口
- 迁移完成后，应复核相关 subsystem notes，确认不再把 `RenderableSubMesh` 当作当前模型描述

## 测试

- 将所有仍依赖 `RenderableSubMesh` 构造的集成测试迁移为 `SceneNode`
- 补一组覆盖删除后的主路径回归测试：scene -> queue -> rendering item
- 验证 renderer 初始化、frame graph、pipeline identity、pipeline cache 等现有测试在无 `RenderableSubMesh` 条件下仍通过
- 验证文档中的示例代码不再引用 `RenderableSubMesh`

## 实施状态

2026-04-24 已归档。

- `src/core/scene/object.{hpp,cpp}` 不再包含 `RenderableSubMesh`、`buildLegacyValidatedData(...)` 或 legacy debug-id helper
- `SceneNode` 为 `IRenderable` 的唯一具体实现
- `Scene` / `RenderQueue` / frame graph 不再保留兼容分支
- 集成测试、demo 与 notes 已统一使用 `SceneNode`
- R1–R7 全部完成
