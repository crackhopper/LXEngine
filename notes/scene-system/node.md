# Node 节点：场景里的稳定对象

`SceneNode` 像舞台上的一个对象牌。它有稳定名字、显示名、局部变换、父子关系、可见层和组件列表。组件决定它能不能当相机、光源或可渲染对象。

## 节点先解决 identity 和层级

| 字段/API | 角色 |
|---|---|
| `nodeName` / `getNodeName()` | scene 内稳定标识，`Scene::addRenderable()` 会检查重复 |
| `name` / `getName()` | editor 展示名，也参与路径段显示 |
| `getPath()` | 根据父子关系生成 runtime path |
| `setParent()` / `getChildren()` | 维护 scene tree |
| `setLocalTransform()` | 保存局部 transform |
| `getWorldTransform()` | 按父链延迟计算 world transform |
| `visibilityMask` | 与 camera culling mask 一起过滤可见对象 |

节点本身不等于 mesh，也不等于材质。它是一个容器：transform 和层级在节点上，能力通过组件挂上来。

## root node 是路径树的根

`Scene` 创建时会有一个 root node。普通节点如果没有 parent，加入 scene 时会挂到 root 下。这样 scene 可以同时支持：

| 使用场景 | 依赖 |
|---|---|
| editor hierarchy | `SceneNode` parent/children |
| path lookup | `Scene::findByPath()` |
| 保存 scene | runtime tree 捕获成 `SceneDocument` |
| picking bounds | node transform + component/mesh/light debug bounds |

## 继续阅读

- [Component 组件](component.md)
- [文档到 Runtime](document-runtime-flow.md)
- [可渲染对象](renderable-object.md)
