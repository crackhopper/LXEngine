# Component 组件：给节点添加能力

组件像挂在舞台对象身上的功能模块。一个 `SceneNode` 负责 identity、transform 和层级；组件负责说明这个节点能做什么。

## 当前常用组件

| 组件 | 给节点添加的能力 | 是否影响可渲染结构 |
|---|---|---|
| `MeshComponent` | 提供 `Mesh`、vertex buffer、index buffer 和 mesh pipeline signature | 是 |
| `MaterialComponent` | 提供 `MaterialInstance`、pass、shader 和 descriptor resources | 是 |
| `SkeletonComponent` | 提供 skeleton GPU resource | 是 |
| `CameraComponent` | 提供 view/projection、target、culling mask | 否 |

`IComponent::affectsRenderableStructure()` 用来告诉节点：组件变化后是否需要重建可渲染 pass 缓存。mesh/material/skeleton 会影响 draw 输入，所以它们返回 true。

## 组件变更会通知 owner

组件通过 `attachTo(SceneNode&)` 绑定 owner。影响可渲染结构的组件在添加、移除或替换资源时，会触发 `SceneNode::rebuildValidatedCache()`。

这让场景系统把校验前移：renderer 不需要每帧猜一个节点缺不缺 mesh、material、shader 或 descriptor。节点在结构变化时就会重建自己的 pass 级缓存。

## 继续阅读

- [可渲染对象](renderable-object.md)
- [相机](camera.md)
- [光源](light.md)
