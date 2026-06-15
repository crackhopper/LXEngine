# Node 节点：场景里的稳定对象

`SceneNode` 像舞台上的一个对象牌。它有稳定名字、显示名、局部变换、父子关系、可见层、组件列表和可渲染标记。组件决定它能不能当相机、光源或可渲染对象；节点变更则通过 aspect 事件同步到 `Scene` 和 `SceneResourceTable`。

## 节点先解决 identity 和层级

| 字段/API | 角色 |
|---|---|
| `nodeName` / `getNodeName()` | scene 内稳定标识，`Scene::addRenderable()` 会检查重复 |
| `name` / `getName()` | editor 展示名，也参与路径段显示 |
| `getPath()` | 根据父子关系生成 runtime path |
| `setParent()` / `getChildren()` | 维护 scene tree |
| `setLocalTransform()` / `setTranslation()` / `setRotation()` / `setScale()` | 保存局部 transform，并触发 transform aspect |
| `getWorldTransform()` | 按父链延迟计算 world transform |
| `visibilityMask` | 与 camera culling mask 一起过滤可见对象 |
| `debugOnlyRenderable` / `renderType` | 标记 debug-only renderable 和 renderable 类型，会影响可渲染资源结构 |

节点本身不等于 mesh，也不等于材质。它是一个容器：transform 和层级在节点上，能力通过组件挂上来。

## root node 是路径树的根

`Scene` 创建时会有一个 root node。普通节点如果没有 parent，加入 scene 时会挂到 root 下。这样 scene 可以同时支持：

| 使用场景 | 依赖 |
|---|---|
| editor hierarchy | `SceneNode` parent/children |
| path lookup | `Scene::findByPath()` |
| 保存 scene | runtime tree 捕获成 `SceneDocument` |
| picking bounds | node transform + component/mesh/light debug bounds |

## 变更通过 Aspect 同步

当前 `SceneNode` 的 setter 不是只改本地字段。节点已挂到 `Scene` 后，变更会发出 `SceneEvent{Runtime, SceneNodeChanged, ...}`，并按 aspect 触发必要的资源同步。

| Aspect | 典型触发 | 当前效果 |
|---|---|---|
| `Transform` | 改局部 transform | 同步 object/camera upload 视图需要的 world transform |
| `Identity` | `setName()` | 发出 runtime 事件，供 editor、路径显示和调试状态观察 |
| `Hierarchy` | `setParent()` / `clearParent()` | 发出树结构变更事件，路径会随父链重新计算 |
| `Visibility` | `setVisibilityLayerMask()` | 同步 object 可见层，后续 render input 构建会按 camera mask 过滤 |
| `RenderableStructure` | 改 mesh/material/skeleton、debug-only 标记或 render type | 同步 `SceneResourceTable`，重建节点的 validated pass cache |
| `CameraProperties` | 相机组件字段变更 | 同步 `CameraResource` |
| `LightProperties` | light 组件字段变更 | 发出 light 相关 runtime 事件，供 scene light 资源路径消费 |

`SceneNode::emitRuntimeNodeChanged()` 会根据 aspect 做最小同步：`RenderableStructure` 会先让 `Scene` 同步节点资源状态，再重建 validated pass cache；`Transform`、`Visibility` 和 `CameraProperties` 会同步 scene resource state；没有挂到 `Scene` 的节点只在需要时更新本地缓存。

## 加入 Scene 时注册资源

`Scene::addRenderable()` 会把节点接入当前 scene：没有 parent 的节点挂到 root，节点获得 scene debug id，mesh/material/skeleton/camera/object 等资源按当前组件状态注册进 `SceneResourceTable`，然后重建 validated pass cache。

注册不是一次性动作。后续节点修改仍通过 aspect 事件回到 `Scene::syncNodeResourceState()`，让资源表里的 object、camera、mesh/material 关系保持和 runtime tree 一致。也就是说，我们调试“节点看不见”时，要同时检查三件事：

| 检查点 | 看什么 |
|---|---|
| 树结构 | node 是否挂在 root 下，路径是否正确 |
| 资源结构 | mesh/material/camera/light 是否已经进 `SceneResourceTable` |
| 可见性 | `visibilityMask` 是否和当前 camera culling mask 命中 |

## 继续阅读

- [Component 组件](component.md)
- [文档到 Runtime](document-runtime-flow.md)
- [可渲染对象](renderable-object.md)
