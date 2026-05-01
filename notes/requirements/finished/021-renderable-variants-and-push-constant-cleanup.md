# REQ-021: Push Constant 收敛、Shader Variant 上移、Renderable 合法性校验

## 背景

当前 `blinnphong_0` 这一条渲染链上，`PC_Draw` 同时承担了两类职责：

1. 传递真正的每-draw 数据：`model`
2. 传递本应决定 shader/pipeline 形态的静态开关：`enableLighting`、`enableSkinning`

这导致三个结构性问题：

- `src/core/rhi/render_resource.hpp` 中的 `PC_Draw` 必须与 `assets/shaders/glsl/blinnphong_0.{vert,frag}` 的 push constant 布局严格保持一致，但当前字段里混入了不应放在 push constant 的 feature 开关
- `enableLighting` / `enableSkinning` 会改变 shader 代码路径，其中 `enableSkinning` 还会影响 vertex 输入、descriptor 需求与 pipeline identity；把它们留在运行时 push constant，会让“不合法组合”拖到 draw 阶段才暴露
- `RenderableSubMesh::getRenderSignature(pass)` 目前把 `Skeleton` 是否存在并入 object-side signature，这延续了“skinning 由 renderable/skeleton 决定”的旧模型；而本需求最终确定 shader variants 应归属于 `MaterialTemplate`
- 当前 `IRenderable` 接口过于贴近底层资源展开（vertex/index/descriptors/objectPC），名字与职责不匹配；它更像“渲染包提供者”而不是“场景中的可渲染对象”
- multi-pass 相关职责当前也处于过渡态：`MaterialTemplate` 已经按 pass 保存 `RenderPassEntry`，但 `MaterialInstance` 只有一个整体 `ResourcePassFlag m_passFlag`，并不能按 instance 启用/禁用单个 pass

同时，当前 `ShaderReflector` 只反射 descriptor/UBO 信息，不反射 vertex stage 的输入属性，因此系统还没有一条统一的“材质 variant 是否与 mesh vertex layout 匹配”的校验路径。

## 设计判断

### D1: 短期内只保留一个引擎级 push constant 格式

短期目标不是支持多个 push constant ABI，而是先把 ABI 稳定下来。当前版本统一采用：

```cpp
struct alignas(16) PC_Base {
  Mat4f model;
};
```

要求：

- 现阶段所有 forward draw 使用同一个 engine-wide push constant 约定
- `PC_Draw` 可以保留为别名/扩展点，但本期不再新增功能字段
- shader 侧的 push constant block 必须与上述结构完全一致
- `PipelineBuildDesc::pushConstant` 继续维持统一范围约定，不因 `lighting/skinning` 开关而分叉

说明：

- “以后可能有多个版本”这个判断没有问题，但当前没有足够多的真实用例支撑一套多版本管理机制
- 本期先把“会影响 shader 接口/PSO 的状态”从 push constant 中剥离，再为未来版本化预留演进空间

### D2: feature 开关迁移为 shader variants

本期把原先落在 push constant 里的 feature 开关迁移为 shader variants。

具体的 forward shader variant 合约、输入契约与逻辑约束，拆到 **REQ-023**。

要求：

- `PC_Draw` / shader push constant block 删除 `enableSkinning`、`enableLighting`
- `blinnphong_0.vert` 与 `blinnphong_0.frag` 改为通过 variants 控制代码路径
- `loadBlinnPhongMaterial(...)` 必须能接收或构造 variant 集合，并把它们同时写入：
  - shader 编译输入
  - `RenderPassEntry::shaderSet.variants`
- variant 作为 pipeline identity 的一部分，继续经由 `ShaderProgramSet::getRenderSignature()` 进入 `PipelineKey`

补充判断：

- `lighting` 若长期演化成“真正的 Unlit 材质族 vs Lit 材质族”，未来可能更适合拆成独立 shader/material family，而不是永久保留在 `blinnphong_0` 里做一个宏
- 但短期内，把相关开关提升为 variant 仍然优于塞进 push constant

### D3: `skinning` 由 MaterialTemplate variant 决定，`Skeleton` 不再参与 `PipelineKey`

一旦 `USE_SKINNING` 成为 material-side variant，pipeline 是否需要骨骼输入/骨骼 UBO，应由 shader program 形态决定，而不是由 `Skeleton` 资源是否挂在 renderable 上单独决定。

因此本期要求：

- `RenderableSubMesh::getRenderSignature(pass)` 不再把 `skeleton` signature compose 进 `ObjectRender`
- `Skeleton::getRenderSignature()` 不再参与 pipeline identity 主路径
- `PipelineKey` 是否区分 skinned / non-skinned，完全由 `MaterialTemplate` 上的 variant 决定

但 `Skeleton` 仍然保留运行时职责：

- 当 material 需要 `USE_SKINNING` 时，renderable 必须提供 skeleton/Bones UBO
- 当 material 不需要 `USE_SKINNING` 时，renderable 不应再因为“恰好挂了 skeleton”而切出新的 pipeline key

换句话说，`Skeleton` 从“identity contributor”降级为“resource provider + legality dependency”。

进一步澄清：

- `ShaderVariant` 属于 shader/program 形态，挂在 `MaterialTemplate` 一侧是合理的
- 但 `Skeleton` 是 per-renderable / per-object 的运行时资源，不适合作为 material 成员

原因：

- 同一个 material 往往需要被多个 renderable 共享；若 `Skeleton` 成为 material 成员，会把对象实例数据错误地下沉到材质层
- `Skeleton` 的内容不是“编译/管线形态”，而是 draw 时要绑定的一份具体资源，和 `CameraUBO` / `LightUBO` 一样属于运行时输入
- 一个 mesh/material 组合理论上可以在不同对象上绑定不同 skeleton；把 skeleton 挂到 material 会破坏这种复用关系

因此更合适的职责切分是：

- `MaterialTemplate`/shader 决定“我是否需要 Bones 这类资源”
- renderable/object 决定“我此刻提供哪一个 Skeleton/Bones 资源”
- 校验层负责确认二者匹配

### D4: 引入高层 `IRenderable` / `SceneNode` 抽象，校验前移到对象装配阶段

当前 `IRenderable` 暴露的是：

- `getVertexBuffer()`
- `getIndexBuffer()`
- `getDescriptorResources()`
- `getObjectInfo()`
- `getShaderInfo()`

这套接口已经非常接近 backend/queue 消费的展开结果，不适合作为“场景中可渲染元素”的主抽象。

本需求确定以下重构方向：

- 保留 `IRenderable` 这个名字给高层语义：场景中一个可参与渲染的元素
- 新增一个具体类，暂定名 `SceneNode`，作为高层 `IRenderable` 的主要实现
- `SceneNode` 持有结构性成员：`mesh`、`material instance`、`optional skeleton`、`objectPC`
- 现有那个偏细粒度、面向 queue/backend 展开的接口不应继续占用 `IRenderable` 这个名称；可下沉为内部辅助接口，或改成更贴近语义的名字（例如 `IRenderItemSource` / `RenderPacketSource`）

校验时机也据此调整：

- `SceneNode` 创建完成后，立即依据 `MaterialTemplate` 上的 shader/variants 反射结果执行一次合法性校验
- `SceneNode` 不提供默认空构造；正式构造时 `mesh` 和 `materialInstance` 是必需成员，`skeleton` 可选
- 如果后续允许替换 `mesh/material/skeleton` 这类结构性成员，则必须通过 setter 触发重新校验
- `RenderQueue` 只消费已经通过校验的 renderable，不再承担首次对象合法性判定职责
- `RenderQueue` 只基于 `SceneNode` 的已验证结构性结果生成 `RenderingItem`；它可以做过滤、排序、拼接 scene-level resources，但不得重新解释 variant 约束或重新做结构性合法性校验
- `SceneNode` 短期应持有一份轻量的 pass 级结构性校验缓存，供 queue/scene 复用；仅缓存最近一次校验通过的结构性结果，不缓存 UBO/texture 等运行时参数
- 这份缓存不应只保存 `bool`，而应采用 `pass -> validated entry` 的形式，至少能承载每个 pass 已验证过的结构性结果
- `SceneNode` 可脱离 `Scene` 独立存在并完成自校验；`Scene` 不是节点合法性的前置条件
- `SceneNode` 应具备稳定调试标识，但该标识仅用于日志/调试，不进入 pipeline identity
- 本期不引入真正的树状 scene graph / transform hierarchy；`SceneNode` 的逻辑路径先退化为 `Scene` 级命名空间下的唯一名字

这个方案优于“在 `RenderQueue::buildFromScene(...)` 首次校验”，因为：

- 非法组合本质上是对象装配错误，而不是排队错误
- 动态运行期新建 `SceneNode` 时可以立即失败，不必等到进入 render queue
- `RenderQueue` 的职责能维持在收集、过滤、排序和去重，而不是对象模型纠错
### D5: 必须引入“shader 接口 vs vertex layout”合法性校验

仅仅把 `USE_SKINNING` 上移到 variant 还不够，因为它会改变 vertex shader 的输入契约。

本期要求新增一条显式校验链，至少覆盖：

- material 当前 variant 编译出来的 vertex shader，实际需要哪些 vertex 输入 location / type / name
- renderable.mesh 的 `VertexLayout` 是否完整提供这些输入
- 当 `USE_SKINNING` 打开时，是否要求存在 `Bones` 绑定且 renderable 真的提供 skeleton UBO
- 当 `USE_SKINNING` 关闭时，shader 不应再声明骨骼输入，也不应要求 `Bones`

推荐做法：

1. 扩展 `ShaderReflector`，让它反射 vertex stage input attributes
2. core/infra 提供一个校验函数，例如：

```cpp
ValidationResult validateRenderableAgainstMaterial(
    const RenderableSubMesh& renderable,
    StringID pass);
```

3. 在 `SceneNode` 构造完成后立即执行校验；若结构性成员变更，则在对应 setter 中重新执行

失败策略：

- 视为程序员错误，不做降级渲染
- 打印 `FATAL` 日志，内容至少包含：
  - pass 名
  - material/shader 名
  - enabled variants
  - mesh vertex layout debug string
  - 缺失/多余的关键输入或 descriptor
- 随后立即终止进程

这里的“退出程序”是合理的，因为这不是运行时可恢复的用户输入错误，而是渲染对象配置非法。

## 需求

### R1: Push constant ABI 收敛

- 本期统一使用仅包含 `model` 的 engine-wide push constant 结构
- C++ 与 GLSL 的 push constant 定义必须一一对应
- 文档中明确：未来若支持多个 push constant ABI，必须在 shader/material 流程中显式建模；本期不实现该能力

### R2: 引入一组受控的 shader variants

- 这些开关从 push constant 删除
- shader 通过 variant 宏控制代码分支
- material loader 负责声明、编译并保存这些 variants
- 具体 variant 集合与约束关系以 **REQ-023** 为准

### R3: variants 归属于 `MaterialTemplate` / loader 产物

本需求最终确定：

- variants 不属于 `MaterialInstance`
- variants 属于 `MaterialTemplate` / loader 产物
- 同一组 variants 对应一个固定 shader/program 形态
- `MaterialInstance` 只保存运行时参数（UBO/texture 等），不承担 variant 身份表达

因此要求：

- loader 在创建 `MaterialTemplate` 时同时确定 shader variants
- `RenderPassEntry::shaderSet.variants` 必须完整表达该模板的 variant 组合
- `MaterialInstance::getRenderSignature(pass)` 继续只包模板侧 pass signature，不新增实例级 variant 维度

### R4: `Skeleton` 脱离 pipeline identity

- 删除 `RenderableSubMesh::getRenderSignature(pass)` 中的 skeleton compose
- 相关测试从“加 skeleton 会改变 `PipelineKey`”改为“改 `USE_SKINNING` variant 会改变 `PipelineKey`”

### R5: 引入 renderable 合法性校验

系统必须在 `SceneNode` 创建完成时执行合法性校验；若后续结构性成员变化，则在 setter 中重新校验。校验内容至少包括：

- material variant 与 shader 编译结果一致
- shader 顶点输入与 mesh vertex layout 匹配
- shader descriptor 需求与 renderable 提供的 descriptor 资源匹配
- `USE_SKINNING` 与 skeleton/Bones 资源存在性匹配
- variant 逻辑约束与输入契约按 **REQ-023** 生效
- 校验通过后，`SceneNode` 应刷新其 pass 级结构性校验缓存

任一校验失败都必须 `FATAL + terminate`。

### R6: 引入新的高层 `IRenderable` 与 `SceneNode`

系统必须把“场景中的可渲染元素”与“backend 可消费的细粒度资源展开结果”分开建模。

要求：

- `IRenderable` 代表场景层抽象，而不是底层资源包
- 新增 `SceneNode` 作为 `IRenderable` 的主要实现
- `SceneNode` 至少包含：
  - `mesh`
  - `materialInstance`
  - `optional skeleton`
  - `objectPC`
- `SceneNode` 构造时必须提供 `nodeName`
- `SceneNode` 构造后立即校验
- 若后续允许替换 `mesh/material/skeleton`，必须通过 setter 执行重新校验
- `setMaterialInstance(...)` 与 `setMesh(...)` 都视为一次完整重新装配，必须重新跑所有已启用 pass 的校验并刷新缓存
- `setSkeleton(nullptr)` 允许，但属于结构性变化；若当前已启用 pass 仍要求 skinning，则必须 `FATAL + terminate`
- `SceneNode` 应保存一份轻量的 pass 级结构性校验缓存，至少覆盖：
  - 哪些 pass 当前已通过校验
  - 每个 pass 对应的结构性 shader/variant 结果
  - 每个 pass 的结构性资源约束结论
  - 生成 `RenderingItem` 所需的稳定结构性信息
- `SceneNode::supportsPass(pass)` 应直接基于 material instance 的 pass enable 状态与自身缓存回答，不得在该调用中临时重新做结构性推导
- `SceneNode` 的 pass 级结构性缓存不直接整体对外暴露；对外只提供有限只读查询
- `SceneNode::supportsPass(pass)` 遇到未知或未启用 pass 时直接返回 `false`，不视为错误
- `RenderQueue` 从 `IRenderable` 提取构建 `RenderingItem` 所需数据，但不再承担首次对象合法性判定
- `objectPC` 在本期继续作为 `SceneNode` 成员保留，但明确视为过渡设计；其当前职责仅限于承载统一 push constant 的 `model`
- 以下操作会使 `SceneNode` 的 pass 级结构性缓存失效并立即重建：
  - 构造 `SceneNode`
  - `setMesh(...)`
  - `setMaterialInstance(...)`
  - `setSkeleton(...)` / `setSkeleton(nullptr)`
  - `MaterialInstance` 的 pass enable 状态变化，并经 `Scene` 传播到该节点
- 以下操作不会使该缓存失效：
  - `setFloat/setInt/setVec*/setTexture/updateUBO`
  - `objectPC` / `model` 的更新
- `SceneNode` 对外只提供有限只读查询，短期至少包括：
  - `supportsPass(pass)`
  - 调试标识 / 名字查询
  - 对 `mesh/materialInstance/skeleton` 的只读访问
  - 必要时按 pass 查询一个 validated summary，而不是完整内部缓存对象
- 该 validated summary 应包含当前 pass 下用于构建 `RenderingItem` 的稳定结构性视图，并包含最终用于 `PipelineKey` 的 object-side signature

### R7: `Scene` 与 `SceneNode` 的调试标识规则

- `Scene` 构造时必须提供 `sceneName`
- `SceneNode` 构造时必须提供 `nodeName`
- 当 `SceneNode` 挂接到 `Scene` 后，由 `Scene` 基于 `sceneName + nodeName` 生成稳定调试标识的 `StringID`
- 该标识只用于日志、调试和定位，不进入 `PipelineKey`
- 本期不要求树状层级路径；调试标识中的 “path” 先退化为 `Scene` 级唯一名字
- 同一 `Scene` 内 `nodeName` 必须唯一；发生冲突时统一 `FATAL + terminate`

命名约束：

- 不得把当前 `IRenderable` 改名为 `IRenderResource`，因为该名称已在 core/rhi 层被占用且语义明确
- 若需要保留一个底层展开接口，应使用不与资源层冲突的新名

### R8: Shader 侧接口按 variant 收缩

- shader 接口必须按 variant 严格收缩
- `blinnphong_0` 的具体 variant 行为、输入契约与测试覆盖以 **REQ-023** 为准

### R9: 测试与文档更新

至少补齐以下覆盖：

- 相同 mesh + 相同 material template，仅切换 `USE_SKINNING` → `PipelineKey` 不同
- 同一 renderable，仅增减 `Skeleton`，但 material variant 不变 → `PipelineKey` 不变
- `USE_SKINNING` 打开但 mesh 缺骨骼顶点输入 → 触发 fatal
- `USE_SKINNING` 打开但未提供 skeleton/Bones → 触发 fatal
- `USE_SKINNING` 关闭但 shader 仍反射出骨骼输入或 Bones 绑定 → 视为 shader 违规
- `SceneNode` 构造时发现 mesh/material/skeleton 不匹配 → 直接 fatal
- 通过 setter 替换 `mesh/material/skeleton` 后会重新校验
- 同一 `Scene` 内 `nodeName` 冲突 → 直接 fatal
- push constant 结构与 shader 对齐的集成测试
- forward shader 的 variant 逻辑与输入契约测试见 **REQ-023**
- material pass 体系的独立需求文档与 level-1 设计页更新（见下游）

## 非目标

- 本期不设计通用的多 push constant ABI/反射系统
- 本期不引入“骨骼存在即自动切换 shader variant”的隐式魔法
- 本期不把非法 renderable 静默跳过或自动修复
- 本期不重做完整材质体系，只聚焦 `blinnphong_0` 链路与共性接口
- 本期不直接完成“MaterialInstance 按 pass 开关”的完整落地；该能力拆到独立材质系统需求
- 若 `SceneNode` 的 pass 级结构性校验缓存实现复杂度明显超预期，可降级并拆出独立需求；当前默认纳入本期

## 下游

- **REQ-022**：材质 pass 选择与 `MaterialInstance` 级 pass enable/disable
- **REQ-023**：通用 Forward Shader 的 Variant 合约
- `docs/design/MaterialSystem.md`：level-1 材质系统总览需要同步反映 Template/Instance/pass 的最终职责边界

## 结论

你的总体方向是对的：`enableLighting` / `enableSkinning` 应离开 push constant，尤其 `enableSkinning` 必须进入 shader/material/pipeline 的静态身份链路。

但要让这件事在当前架构里自洽，至少还要补上两条你原提法里尚未显式写出的要求：

1. variants 明确归属于 `MaterialTemplate`，不再误挂到 `MaterialInstance`

## 实施状态

- 日期：2026-04-15
- 验证结果：R1-R9 已复核；原先存在两处 drift，现已修正
  - `Scene` 挂接 `SceneNode` 时补齐稳定的 `sceneName/nodeName` 调试 `StringID`
  - 补充 `blinnphong_0` push constant ABI 对齐测试，明确验证 C++ 与 GLSL 都只保留统一 `model`
- 额外修正：`MaterialInstance` 的 `MaterialUBO` 布局选择改为基于启用 pass 的 shader；legacy `RenderableSubMesh` 路径改为按 pass 选择 shader metadata，避免多 pass shader 覆盖时带错反射信息
- 测试：
  - `test_material_instance` — PASS
  - `test_shader_compiler` — PASS
  - `test_scene_node_validation` — PASS
  - `test_pipeline_identity` — PASS
  - `test_pipeline_build_info` — PASS
  - `test_frame_graph` — PASS
- 结论：REQ-021 当前实现与代码库状态一致，可归档
2. 反射层必须新增 vertex input 契约，不能只靠现有 UBO/descriptor 反射
3. 场景层要引入真正的高层 `IRenderable` / `SceneNode`，并把合法性校验前移到对象装配阶段

缺少这些约束，需求会停留在“概念上正确，接口上落不下去”的状态。
