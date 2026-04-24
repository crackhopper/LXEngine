# Scene

> Scene 负责持有 renderables、camera、light，并为 `RenderQueue` 提供 scene-level 资源。真正的 draw item 组装已经从“现场拼装”改成“消费 `SceneNode` 预验证结果”。
>
> 相关 spec: `openspec/specs/scene-node-validation/spec.md` + `openspec/specs/frame-graph/spec.md` + `openspec/specs/render-signature/spec.md` + `openspec/specs/forward-shader-variant-contract/spec.md`

## 深入阅读

- [场景对象](../concepts/scene/index.md)：从使用者视角展开 `Scene` / `SceneNode` / `ValidatedRenderablePassData`，解释它们如何服务 `preloadPipeline` 和 `drawcall`

## 它解决什么问题

- 给 renderer 一个稳定的场景入口。
- 把“对象是否合法”前移到 `SceneNode` 自身，而不是在 queue 里临时检查。
- 在 scene 级统一管理 camera/light 资源和 node 命名空间。

## 核心对象

- `Scene`：持有 renderables、camera 列表、light 列表，要求显式 `sceneName`。
- `IRenderable`：renderable 抽象接口，新增 `getValidatedPassData(pass)` 只读出口。
- `SceneNode`：当前主路径实现，聚合 `nodeName`、`MeshSharedPtr`、`MaterialInstanceSharedPtr`、可选 `SkeletonSharedPtr` 与 `PerDrawDataSharedPtr`。
- `transform hierarchy`：`SceneNode` 额外维护 `localTransform`、派生 `worldTransform`、可选 parent 和 child 关系；scene 仍然平铺持有 renderable，hierarchy 只负责空间组合。
- `ValidatedRenderablePassData`：`pass -> validated entry` 缓存项，保存 queue 需要的稳定结构结果。
- `RenderingItem`：一次 draw 的完整上下文，字段仍是 `shaderInfo`、`material`、`drawData`、`vertexBuffer`、`indexBuffer`、`descriptorResources`、`pass`、`pipelineKey`。
- `visibility mask`：`SceneNode` 自身携带的 layer bitmask；camera 持有独立 `cullingMask`，queue 构建时做交集判断。

## 典型数据流

1. 构造 `SceneNode(nodeName, mesh, material, skeleton?)`。
2. `SceneNode` 构造时立即扫描 enabled passes，完成结构性校验并建立 `m_validatedPasses`。
3. `Scene::addRenderable(node)` 检查同一 scene 内 `nodeName` 唯一，为 `SceneNode` 写入 `sceneName/nodeName` 的调试 `StringID`，并接管 shared `MaterialInstance` 的 pass-state 传播。
4. 如果节点挂在 parent 下，`SceneNode` 会按 `parent.world * local` 懒更新自身 `worldTransform`，并把结果写回 `PerDrawData.model`。
5. `RenderQueue::buildFromScene(scene, pass, target)` 先取一次 `scene.getSceneLevelResources(pass, target)`。
6. queue 先收集当前 `target` 下所有匹配 camera 的 `cullingMask` 并做按位 OR；renderable 只有在 `visibilityMask & combinedCameraMask != 0` 时才继续参与当前 queue。
7. queue 把 scene-level 资源追加到 descriptor 列表末尾，生成 `RenderingItem` 并排序。

## 关键约束

- `SceneNode` 可以脱离 `Scene` 独立存在；scene 只额外提供命名空间和 scene-level 资源。
- `SceneNode` 的 hierarchy 也是可选的；没设 parent 时，`worldTransform == localTransform`。
- `SceneNode` 回指 parent scene 现在走 `weak_ptr` 语义：挂进 scene 后可锁回 parent，scene 销毁后会自动失效，不再依赖裸指针悬挂状态。
- `SceneNode` 的结构必填项是 `nodeName`、`mesh`、`materialInstance`；`skeleton` 可选；`perDrawData` 继续保留。
- `setMesh(...)`、`setMaterialInstance(...)`、`setSkeleton(...)` 会同步重建 validated cache；`setFloat` / `setTexture` / `syncGpuData()` / model 更新不会。
- `setLocalTransform(...)`、`setParent(...)`、`clearParent()` 只触发 world/per-draw dirty 传播，不会重建 validated cache，因为 pipeline 和 descriptor 结构没变。
- `supportsPass(pass)` 现在是缓存查询，不再是简单的 pass-mask 按位判断。
- `SceneNode` 默认 `visibilityMask = 0xffffffff`，`Camera` 默认 `cullingMask = 0xffffffff`，所以旧场景在不显式设置 mask 时行为不变。
- camera 的 mask 只决定“哪些 renderable 进入 queue”；`Scene::getSceneLevelResources(pass, target)` 仍只按 `target` 选 camera、按 `pass` 选 light，不会因为某个 renderable 被裁掉就撤掉 camera UBO。
- 共享 `MaterialInstance` 的 `setPassEnabled(...)` 会由 `Scene::revalidateNodesUsing(materialInstance)` 传播到所有引用该实例的节点；普通参数写入不触发这条结构性重验证。
- 结构性校验失败统一抛 `logic_error`，错误信息会带 pass、material、shader variants 和 vertex layout。
- `Scene` 内 `nodeName` 必须唯一；重复插入会直接终止。
- 对 `blinnphong_0` 的 forward pass，`SceneNode` 现在除了“按反射 contract 检查 location/type”外，还显式承担 variant-to-resource 约束：
  - `USE_VERTEX_COLOR` 要求 mesh 提供 `inColor`
  - `USE_UV` 要求 mesh 提供 `inUV`
  - `USE_LIGHTING` 要求 mesh 提供 `inNormal`
  - `USE_NORMAL_MAP` 要求 mesh 同时提供 `inTangent + inUV`
  - `USE_SKINNING` 要求 mesh 提供 `inBoneIDs + inBoneWeights`，且节点上必须有 `Skeleton/Bones`
- descriptor 结构校验现在区分“结构性必需资源”和“运行时可选资源”：
  - `Bones` 仍然是结构性必需资源
  - material-owned `UniformBuffer` / `StorageBuffer` 仍然必须存在
  - 普通 sampled image 不再一律视为 fatal 缺失，允许 shader 通过运行时 flag 自己决定是否真的采样
- `SceneNode` 也会校验保留 binding 名字的 descriptor 类型是否符合系统合同，例如 `CameraUBO` / `LightUBO` / `Bones` 都必须是 `UniformBuffer`。

## 当前实现边界

- `IRenderable::getDescriptorResources(...)` 已经是显式带 pass 的接口；`getShaderInfo()` 的无参版本仍主要作为 Forward 默认读取路径保留。
- `PerDrawData` 仍是 128 字节缓冲，但当前 engine-wide ABI 只要求 `PerDrawLayoutBase` / `PerDrawLayout` 的 `model` 字段有效。
- 第一版 hierarchy 只支持 renderable-to-renderable 关系；没有单独的 transform-only scene node，也不会改变 `Scene` 对 renderable 的平铺 ownership，所以 child 仍然需要显式加入 `Scene`。
- `Scene` 构造时仍会补一个默认 camera 和一个默认 directional light，方便不走完整 renderer 初始化的测试；节点一旦通过 `addRenderable()` 挂进 scene，也会拿到一个弱 back-reference，用来支持 shared material 重验证传播。
- `src/core/scene/object.cpp` 里的 fatal 文本现在会直接带上缺失的 input 名字，例如 `missing vertex input 'inUV' at location 2`，便于把 forward variant 失败定位到具体 mesh contract。
- `src/test/integration/test_scene_node_validation.cpp` 已经把 `missing inColor / inUV / inNormal / inTangent / inBoneIDs / inBoneWeights / Skeleton` 这些 forward-path 失败都跑成子进程死亡测试，同时覆盖了“可选 sampler 缺失不阻塞校验”的回归用例。

## 从哪里改

- 想改结构性校验：看 `src/core/scene/object.cpp` 里的 `rebuildValidatedCache()`。
- 想改 scene-level 资源筛选：看 `Scene::getSceneLevelResources()`。
- 想改 camera/renderable 可见性过滤：看 `Scene::getCombinedCameraCullingMask()` 和 `RenderQueue::buildFromScene()`。
- 想改 shared material 的结构传播或 node 唯一性/调试标识：看 `Scene::addRenderable()` 和 `Scene::revalidateNodesUsing(...)`。

## 关联文档

- `notes/subsystems/frame-graph.md`
- `notes/subsystems/material-system.md`
- `notes/subsystems/pipeline-identity.md`
