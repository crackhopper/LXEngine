# Scene

> Scene 负责持有 renderables、camera、light，并为 `RenderQueue` 提供 scene-level 资源。真正的 draw item 组装已经从“现场拼装”改成“消费 `SceneNode` 预验证结果”。
>
> 相关 spec: `openspec/specs/scene-node-validation/spec.md` + `openspec/specs/frame-graph/spec.md` + `openspec/specs/render-signature/spec.md` + `openspec/specs/forward-shader-variant-contract/spec.md`

## 它解决什么问题

- 给 renderer 一个稳定的场景入口。
- 把“对象是否合法”前移到 `SceneNode` 自身，而不是在 queue 里临时检查。
- 在 scene 级统一管理 camera/light 资源和 node 命名空间。

## 核心对象

- `Scene`：持有 renderables、camera 列表、light 列表，要求显式 `sceneName`。
- `IRenderable`：renderable 抽象接口，新增 `getValidatedPassData(pass)` 只读出口。
- `SceneNode`：当前主路径实现，聚合 `nodeName`、`MeshPtr`、`MaterialInstance::Ptr`、可选 `SkeletonPtr` 与 `ObjectPCPtr`。
- `ValidatedRenderablePassData`：`pass -> validated entry` 缓存项，保存 queue 需要的稳定结构结果。
- `RenderingItem`：一次 draw 的完整上下文，字段仍是 `shaderInfo`、`material`、`objectInfo`、`vertexBuffer`、`indexBuffer`、`descriptorResources`、`passMask`、`pass`、`pipelineKey`。
- `RenderableSubMesh`：仍保留的兼容实现，但不再是推荐的场景主模型。

## 典型数据流

1. 构造 `SceneNode(nodeName, mesh, material, skeleton?)`。
2. `SceneNode` 构造时立即扫描 enabled passes，完成结构性校验并建立 `m_validatedPasses`。
3. `Scene::addRenderable(node)` 检查同一 scene 内 `nodeName` 唯一，并为 `SceneNode` 写入 `sceneName/nodeName` 的调试 `StringID`。
4. `RenderQueue::buildFromScene(scene, pass, target)` 先取一次 `scene.getSceneLevelResources(pass, target)`。
5. queue 只过滤 `supportsPass(pass)`，然后直接消费 `renderable->getValidatedPassData(pass)`。
6. queue 把 scene-level 资源追加到 descriptor 列表末尾，生成 `RenderingItem` 并排序。

## 关键约束

- `SceneNode` 可以脱离 `Scene` 独立存在；scene 只额外提供命名空间和 scene-level 资源。
- `SceneNode` 的结构必填项是 `nodeName`、`mesh`、`materialInstance`；`skeleton` 可选；`objectPC` 继续保留。
- `setMesh(...)`、`setMaterialInstance(...)`、`setSkeleton(...)` 会同步重建 validated cache；`setFloat` / `setTexture` / model 更新不会。
- `supportsPass(pass)` 现在是缓存查询，不再是简单的 pass-mask 按位判断。
- 结构性校验失败统一走 `FATAL + terminate`，错误信息会带 pass、material、shader variants 和 vertex layout。
- `Scene` 内 `nodeName` 必须唯一；重复插入会直接终止。
- 对 `blinnphong_0` 的 forward pass，`SceneNode` 现在除了“按反射 contract 检查 location/type”外，还显式承担 variant-to-resource 约束：
  - `USE_VERTEX_COLOR` 要求 mesh 提供 `inColor`
  - `USE_UV` 要求 mesh 提供 `inUV`
  - `USE_LIGHTING` 要求 mesh 提供 `inNormal`
  - `USE_NORMAL_MAP` 要求 mesh 同时提供 `inTangent + inUV`
  - `USE_SKINNING` 要求 mesh 提供 `inBoneIDs + inBoneWeights`，且节点上必须有 `Skeleton/Bones`

## 当前实现边界

- `SceneNode::getDescriptorResources()` 和 `getShaderInfo()` 的无参版本仍以 `Pass_Forward` 作为默认读取路径，主要是兼容旧接口。
- `RenderableSubMesh` 仍能工作，但它的 validated 数据是兼容层即时拼出来的，不具备 `SceneNode` 那套自维护缓存和 fatal 校验模型。
- `ObjectPC` 仍是 128 字节缓冲，但当前 engine-wide ABI 只要求 `PC_Base` / `PC_Draw` 的 `model` 字段有效。
- `Scene` 构造时仍会补一个默认 camera 和一个默认 directional light，方便不走完整 renderer 初始化的测试。
- `src/core/scene/object.cpp` 里的 fatal 文本现在会直接带上缺失的 input 名字，例如 `missing vertex input 'inUV' at location 2`，便于把 forward variant 失败定位到具体 mesh contract。
- `src/test/integration/test_scene_node_validation.cpp` 已经把 `missing inColor / inUV / inNormal / inTangent / inBoneIDs / inBoneWeights / Skeleton` 这些 forward-path 失败都跑成子进程死亡测试。

## 从哪里改

- 想改结构性校验：看 `src/core/scene/object.cpp` 里的 `rebuildValidatedCache()`。
- 想改 scene-level 资源筛选：看 `Scene::getSceneLevelResources()`。
- 想改 node 唯一性或调试标识：看 `Scene::addRenderable()`。

## 关联文档

- `notes/subsystems/frame-graph.md`
- `notes/subsystems/material-system.md`
- `notes/subsystems/pipeline-identity.md`
