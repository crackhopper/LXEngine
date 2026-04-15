# 场景对象

> 这篇文档只讲 `src/core/scene/` 这一层如何把“场景中的对象”整理成后续渲染阶段可直接消费的数据，重点覆盖 `Scene`、`SceneNode`、`ValidatedRenderablePassData`，以及它们如何服务 `preloadPipeline` 和最终 `drawcall`。
>
> 对应实现入口：
> `src/core/scene/scene.hpp`
> `src/core/scene/scene.cpp`
> `src/core/scene/object.hpp`
> `src/core/scene/object.cpp`
> `src/core/frame_graph/render_queue.cpp`

## 先说结论

`core` 层的 scene 现在做的不是“保存一堆对象然后等 renderer 临时解释”，而是提前把 draw 所需的结构信息整理好，形成一个很明确的分工：

- `Scene` 负责容器、命名空间、scene-level 资源，以及共享材质 pass 状态变化的传播。
- `SceneNode` 负责 renderable 级别的结构校验，并维护 `pass -> ValidatedRenderablePassData` 缓存。
- `RenderQueue` 不再现场做 mesh/material/skeleton 兼容性判断，只消费 `ValidatedRenderablePassData` 生成 `RenderingItem`。
- `FrameGraph` 再从这些 `RenderingItem` 里收集 `PipelineBuildDesc`，供 backend preload pipeline。

换句话说，scene 现在已经是“前端候选阶段”的稳定数据源，而不是 runtime draw 阶段才开始拼装数据的地方。

## core 层 scene 要解决什么

从 renderer 视角看，进入 draw 之前有两个核心问题必须先回答：

1. 这个 renderable 在某个 pass 下能不能参与渲染。
2. 如果能，它参与渲染时到底需要哪些稳定资源和结构信息。

当前 `core` 层 scene 的设计就是为了把这两个问题提前回答掉。

它承担的事情可以拆成四块：

- 管理 renderable / camera / light 三类 scene 成员。
- 为每个 renderable 的每个 pass 建立结构上已经通过验证的缓存结果。
- 为某个 `(pass, target)` 组合提供 scene-level descriptor 资源。
- 把 draw 所需信息整理成可稳定复用的数据形状，供 `RenderQueue`、`PipelineBuildDesc` 和 backend 继续往后走。

## 关键对象怎么分工

## `Scene`

`Scene` 是 scene-level 容器，持有三类成员：

- `m_renderables`
- `m_cameras`
- `m_lights`

对应代码在 [scene.hpp](/home/lx/proj/renderer-demo/src/core/scene/scene.hpp:31)。

它的核心职责不是校验 mesh 和 shader，而是做 scene 级别的组织工作：

- 持有显式 `sceneName`
- 保证 `nodeName` 在同一个 scene 内唯一
- 把 `SceneNode` 绑定回所属 `Scene`
- 给 `SceneNode` 写入 `sceneName/nodeName` 形式的 debug id
- 在 shared `MaterialInstance` 的 pass 开关变化时，重新触发引用该材质的所有节点重建 validated cache
- 根据 `pass + target` 提供 scene-level descriptor 资源

另外，构造函数会预置一个默认 camera 和一个默认 directional light，保证一些未走完整 renderer 初始化路径的测试仍能拿到基础 scene-level 资源，见 [scene.hpp](/home/lx/proj/renderer-demo/src/core/scene/scene.hpp:35)。

## `IRenderable`

`IRenderable` 是 renderable 抽象层。现在它最关键的接口不是传统的 `getVertexBuffer()` 这些，而是：

- `supportsPass(StringID pass)`
- `getValidatedPassData(StringID pass)`

对应定义在 [object.hpp](/home/lx/proj/renderer-demo/src/core/scene/object.hpp:65)。

这两个接口一起决定了：前端候选阶段可以先做只读过滤，再取出已经验证过的稳定结果，而不是临时解释对象内部状态。

## `SceneNode`

`SceneNode` 是当前主路径的高层 renderable。它聚合：

- `nodeName`
- `MeshPtr`
- `MaterialInstance::Ptr`
- 可选 `SkeletonPtr`
- `ObjectPCPtr`
- `m_validatedPasses`

定义在 [object.hpp](/home/lx/proj/renderer-demo/src/core/scene/object.hpp:86)。

这里最重要的不是“它有这些字段”，而是它在构造和结构变更时会立即调用 `rebuildValidatedCache()`，把 enabled passes 都校验一遍并缓存结果，见 [object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:107) 和 [object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:192)。

因此，`SceneNode` 的语义已经不是“一个等待 renderer 解释的原始对象”，而是“一个对自己的渲染结构合法性负责的 renderable”。

## `ValidatedRenderablePassData`

`ValidatedRenderablePassData` 是整个链路里最关键的数据形状，定义在 [object.hpp](/home/lx/proj/renderer-demo/src/core/scene/object.hpp:52)。

它表示：

某个 renderable 在某个 pass 下，经过结构校验后，可以稳定参与后续 queue 构建和 pipeline 预收集的一份结果。

字段含义可以直接拆开看：

- `pass`
  这份数据对应哪个 pass。
- `material`
  后续取 render state、参与 pipeline 构建时要用到的材质句柄。
- `shaderInfo`
  该 pass 最终实际使用的 shader 信息，后续提取 stages 和 reflection bindings 要用。
- `objectInfo`
  draw 时绑定的 `ObjectPC`，当前主要承载 model 相关 push constant 数据。
- `vertexBuffer`
  draw 的 vertex buffer 句柄。
- `indexBuffer`
  draw 的 index buffer 句柄。
- `descriptorResources`
  renderable 自身拥有的 descriptor 资源集合，通常包含 material 资源，必要时也会拼入 skeleton 的 `Bones` UBO。
- `passMask`
  当前材质实例的 pass 位掩码。
- `objectSignature`
  object 侧的 render signature，目前由 mesh 的 signature 组合而来。
- `pipelineKey`
  最终的 pipeline 身份，来自 `objectSignature + material render signature`。

如果只保留一句话来理解它：

`ValidatedRenderablePassData` 就是“这个对象在这个 pass 下已经通过结构验证后的 draw 前置包”。

## `SceneNode` 具体做了哪些校验

`SceneNode::rebuildValidatedCache()` 是这条链路的核心。它会先清空旧缓存，然后对当前启用的每个 material pass 逐个重建 validated entry，见 [object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:192)。

当前会做的检查大致分五层。

### 1. 基础存在性

- `nodeName` 不能为空
- `mesh` 不能为空
- `materialInstance` 不能为空
- `material template` 不能为空

这些失败会直接 `FATAL + terminate`，见 [object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:195)。

### 2. pass / shader 有效性

对每个 enabled pass：

- 先从 `MaterialTemplate` 取 pass entry
- 再解析该 pass 的 shader
- 如果 entry 里没有可用 shader，则退回 `m_materialInstance->getShaderInfo(pass)`
- 如果仍然拿不到 shader，直接 fatal

见 [object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:208)。

### 3. skinning 结构一致性

它会检查：

- `USE_SKINNING` variant 是否启用
- shader reflection 里是否真的存在 `Bones` binding
- 如果启用了 skinning，节点上是否真的有 `Skeleton`

只要 variant、binding、skeleton 三者关系不一致，就直接 fatal，见 [object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:224)。

这一步的本质是把“skinning 是不是一条合法渲染路径”在 scene 前端就确定下来，避免 draw 前才发现 shader 和资源模型对不上。

### 4. vertex input 和 mesh layout 一致性

`SceneNode` 会遍历 shader reflection 给出的所有 vertex inputs，再拿 mesh 的 `VertexLayout` 逐个检查：

- location 是否存在
- type 是否匹配

缺 input 或类型不一致都会 fatal，见 [object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:238)。

这一步直接解决了一个过去常见问题：renderer 不必在 queue 构建时再猜“这个 mesh 能不能喂给这份 shader”。

### 5. descriptor 资源完整性

它会先取材质实例自己的 `descriptorResources`，然后再根据 shader reflection bindings 判断 renderable 侧必须补齐哪些资源，见 [object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:254)。

当前规则是：

- `CameraUBO` / `LightUBO` 不属于 renderable 自身负责，它们属于 scene-level
- `MaterialUBO` 必须已经由 material 提供
- `Bones` 必须由 skeleton 提供；如果 shader 要求了 `Bones`，这里会把 skeleton 的 UBO 追加进来

这样做有一个很关键的结果：

renderable 级资源和 scene 级资源的边界在 `core` 层已经被明确拆开了。

## cache 是什么时候重建的

`SceneNode` 的 validated cache 只在结构变化时重建。

当前触发点包括：

- 构造 `SceneNode`
- `setMesh(...)`
- `setMaterialInstance(...)`
- `setSkeleton(...)`
- shared `MaterialInstance` 的 pass enable/disable 状态变化

对应代码在 [object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:107)、[object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:121)、[object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:126)、[object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:133)、[object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp:287)。

不触发重建的则是普通 runtime 数据更新，比如：

- material 参数写入
- texture 变更
- UBO 内容更新
- `ObjectPC` 内 model 数据更新

原因很简单：这些属于“值变化”，不是“结构变化”。pipeline 身份和 draw 形状没变，就不该强制重跑结构校验。

## `Scene` 如何给前端候选阶段提供需要的数据

这里的“前端候选阶段”可以理解成：在真正 draw 之前，系统需要先确定“哪些对象会参与某个 pass”，并且把这些对象的稳定 draw 输入组织起来。

`Scene` 在这个阶段主要提供两类信息。

### 1. renderable 列表

`Scene::getRenderables()` 提供候选对象全集，见 [scene.hpp](/home/lx/proj/renderer-demo/src/core/scene/scene.hpp:67)。

之后 `RenderQueue::buildFromScene(...)` 会遍历这个列表，但不会对每个对象做首次结构分析，而是只问两个问题：

- `supportsPass(pass)` 是不是 `true`
- `getValidatedPassData(pass)` 有没有缓存结果

见 [render_queue.cpp](/home/lx/proj/renderer-demo/src/core/frame_graph/render_queue.cpp:55)。

所以，`Scene` 提供的是“候选对象集合”，而真正的候选资格是由 `SceneNode` 的 validated cache 决定的。

### 2. scene-level descriptor 资源

`Scene::getSceneLevelResources(pass, target)` 提供某个 pass、某个 render target 下所有需要共享注入到 item 里的 scene 级资源，见 [scene.cpp](/home/lx/proj/renderer-demo/src/core/scene/scene.cpp:27)。

它的规则很明确：

- camera 按 `target` 过滤
- light 按 `pass` 过滤
- 返回顺序固定为 camera 在前、light 在后

这是为了把“同一 target 对应哪台 camera”与“这个 pass 里哪些 light 应该生效”拆成两个维度处理。

这个接口对前端候选阶段非常重要，因为 renderable 自己不该持有 camera/light 资源，也不该知道自己最终会被放进哪个 target 对应的 queue 里。

## 它怎么让 `preloadPipeline` 变得方便

pipeline preload 的关键目标是：

在真正 draw 前，把场景里当前会用到的 pipeline 组合先收集出来。

当前链路是这样的：

1. `FrameGraph::buildFromScene(scene)` 为每个 `FramePass` 调 `RenderQueue::buildFromScene(scene, pass, target)`
2. `RenderQueue` 用 `ValidatedRenderablePassData` 生成 `RenderingItem`
3. `RenderQueue::collectUniquePipelineBuildDescs()` 按 `PipelineKey` 去重
4. `FrameGraph::collectAllPipelineBuildDescs()` 再跨 pass 去重
5. backend preload 这些 `PipelineBuildDesc`

其中最关键的一步，是 `ValidatedRenderablePassData` 已经提前给出了：

- `shaderInfo`
- `vertexBuffer`
- `indexBuffer`
- `material`
- `pipelineKey`

这意味着 `PipelineBuildDesc::fromRenderingItem(...)` 不用再回头理解 scene 结构，只要从 item 里直接提取：

- shader stages
- reflection bindings
- vertex layout
- topology
- render state

这正是 pipeline preload 需要的全部核心输入。

如果没有 `ValidatedRenderablePassData` 这层缓存，`collectUniquePipelineBuildDescs()` 就必须重新解释 mesh/material/shader/skeleton 关系，那会把 preload 逻辑重新拖回“现场拼装”的旧模式。

## 它怎么让 `drawcall` 变得方便

对 drawcall 来说，最麻烦的通常不是 `vkCmdDrawIndexed(...)` 这一句，而是 draw 前的状态组织：

- 用哪份 pipeline
- 绑哪份 vertex/index buffer
- descriptor 从哪里来
- object push constant 从哪里来

当前 scene 链路把这些问题都前移了。

`RenderQueue` 从 `ValidatedRenderablePassData` 生成 `RenderingItem` 时，只做两件事：

- 把 validated 数据搬进 item
- 把 `Scene::getSceneLevelResources(pass, target)` 返回的 scene 级资源追加到 `descriptorResources` 末尾

见 [render_queue.cpp](/home/lx/proj/renderer-demo/src/core/frame_graph/render_queue.cpp:13) 和 [render_queue.cpp](/home/lx/proj/renderer-demo/src/core/frame_graph/render_queue.cpp:55)。

因此到 backend 真正 draw 时，单个 item 已经天然具备：

- `pipelineKey`
- `shaderInfo`
- `vertexBuffer`
- `indexBuffer`
- `descriptorResources`
- `objectInfo`

这就是 drawcall 最想要的形状。

也就是说，scene 并不是直接发 drawcall，但它通过 `SceneNode -> ValidatedRenderablePassData -> RenderingItem` 这条链，把 drawcall 所需的结构准备工作提前做完了。

## 为什么 `ValidatedRenderablePassData` 非常关键

如果只从代码结构上看，它像一个普通 struct；但从系统职责上看，它其实是 scene 前端与后续渲染流程之间的契约层。

它至少解决了四个问题：

- 把 pass 参与资格固定下来，避免 queue 临时猜测
- 把 renderable 自有资源固定下来，避免 queue 再解释 material/skeleton
- 把 pipeline 身份固定下来，方便预加载和排序去重
- 把 draw 所需的稳定字段集中到一个结构里，后续阶段只消费不重建

因此可以把它理解成：

scene 层对“这个对象可以如何被渲染”给出的正式答案。

## 一条完整路径

下面用一条完整路径把这篇文档压缩一下：

1. 上层构造 `SceneNode(nodeName, mesh, material, skeleton?)`
2. `SceneNode` 立即对 enabled passes 做结构校验
3. 每个 pass 生成一份 `ValidatedRenderablePassData`
4. `Scene::addRenderable(...)` 把节点纳入 scene 命名空间，并让 shared material 的 pass 状态变化能回流到节点重验证
5. `FrameGraph` 按 `pass + target` 让 `RenderQueue` 从 scene 拉数据
6. `RenderQueue` 过滤 `supportsPass(pass)`，直接消费 `getValidatedPassData(pass)`
7. queue 把 scene-level camera/light 资源追加进 item
8. item 一方面被 backend 用来 draw，另一方面被转换成 `PipelineBuildDesc` 用来 preload pipeline

这就是当前 `core` 层 scene 的真正价值：

它把“能不能渲染”和“如何渲染”的结构答案尽可能提前、尽可能稳定地整理出来。

## 相关文档

- [Scene 总览](../subsystems/scene.md)
- [Frame Graph](../subsystems/frame-graph.md)
- [Pipeline Identity](../subsystems/pipeline-identity.md)
- [Material System](../subsystems/material-system.md)
