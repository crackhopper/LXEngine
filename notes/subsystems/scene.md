# Scene

> Scene 是数据容器，不是 draw 逻辑容器。它负责持有 renderables、camera、light，真正的 `RenderingItem` 组装发生在 `RenderQueue`。
>
> 相关 spec: `openspec/specs/frame-graph/spec.md` + `openspec/specs/pipeline-key/spec.md` + `openspec/specs/render-signature/spec.md`

## 它解决什么问题

- 给 renderer 一个稳定的场景入口。
- 分离“场景里有什么”和“这一帧怎么组装 draw item”。
- 把 scene-level 资源统一暴露给 `RenderQueue`。

## 核心对象

- `Scene`：持有 renderables、camera 列表、light 列表。
- `IRenderable`：renderable 抽象接口。
- `RenderableSubMesh`：当前主要实现，聚合 mesh/material/optional skeleton。
- `RenderingItem`：一次 draw 的完整上下文，当前字段包括 `shaderInfo`、`material`、`objectInfo`、`vertexBuffer`、`indexBuffer`、`descriptorResources`、`passMask`、`pass`、`pipelineKey`。

## 典型数据流

1. `Scene` 持有 renderables。
2. `RenderQueue::buildFromScene(scene, pass, target)` 先取一次 `scene.getSceneLevelResources(pass, target)`。
3. 过滤 `supportsPass(pass)`。
4. 生成 `RenderingItem`。对 `RenderableSubMesh` 来说，先收集材质 descriptor 资源，再在有 skeleton 时追加 `Bones` UBO。
5. 把 camera/light 资源追加进去。
6. 用 `PipelineKey::build(...)` 填好身份。

## 关键约束

- `Scene` 不负责 item factory。
- scene-level UBO 不混进 `IRenderable::getDescriptorResources()`。
- `Scene::getSceneLevelResources(pass, target)` 当前按两条轴过滤：camera 看 `matchesTarget(target)`，light 看 `supportsPass(pass)`。
- 返回顺序固定是 camera UBO 在前，light UBO 在后，且都保持各自容器的插入顺序。
- pass 参数必须贯穿 render signature 链路。
- `RenderingItem::material` 不能丢，因为 backend 需要它派生 `PipelineBuildDesc`。
- `IRenderable::supportsPass(pass)` 默认只是 `getPassMask()` 和 `passFlagFromStringID(pass)` 的按位判断；如果某个 renderable 需要更细粒度规则，要自己 override。
- `RenderableSubMesh::getRenderSignature(pass)` 当前由 `mesh->getRenderSignature(pass)` 和可选的 skeleton signature 组合成 `TypeTag::ObjectRender`。

## 当前实现边界

- `Scene` 已经不是单 camera / 单 directional light 结构，而是 `m_cameras` 和 `m_lights` 两个容器。
- `Scene(IRenderablePtr)` 构造时会自动补一个默认 camera 和一个默认 `DirectionalLight`。这个默认 camera 还会被设成 `RenderTarget{}`，这样不经过 `VulkanRenderer::initScene()` 的测试也能拿到 scene-level 资源。
- 默认 `DirectionalLight` 的 pass mask 不是只有 `Forward`，而是 `Forward | Deferred`；它写入的是 `LightUBO`。
- `Camera` 自身的 `m_target` 默认是 `nullopt`，但 `Scene` 构造里补进去的那个默认 camera 会被立刻设成 `RenderTarget{}`。两者不要混为一谈。
- `ObjectPC` 是 renderable 自带的 push constant 资源，默认只包含 `PC_Base` 的 model 矩阵；实际 draw 前可能再被 renderer 更新成更大的 `PC_Draw`。
- `RenderingItem` 类型定义在 `scene.hpp`，但真正填充字段的逻辑不在 `Scene`，而是在 `RenderQueue` 内部的 `makeItemFromRenderable(...)`。

## 从哪里改

- 想加新的 scene-level 资源：看 `Scene::getSceneLevelResources()`。
- 想加新 renderable 类型：看 `IRenderable`。
- 想改 item 组装字段：看 `RenderQueue::buildFromScene(...)` 和 `RenderingItem`。

## 关联文档

- `notes/subsystems/frame-graph.md`
- `notes/subsystems/material-system.md`
- `notes/subsystems/pipeline-identity.md`
