# 场景系统：把文档和资源组织成运行时世界

场景系统负责把反序列化得到的场景文档、模型资源、材质实例、相机和光源组织成运行时可以查询、编辑和渲染的对象模型。

我们可以把它想成舞台管理系统：资产系统提供剧本和道具清单，场景系统负责把道具摆到舞台上，给每个对象挂上组件，并把可渲染数据交给 `RenderWorkCompiler` 生成本帧输入。

## 核心对象

| 对象 | 角色 | 类比 |
|---|---|---|
| `Scene` | 持有 root node、renderables、cameras、lights 和 scene-level resources | 舞台管理台 |
| `SceneNode` | 带 transform、层级、组件和可渲染缓存的节点 | 舞台上的一个对象 |
| `IComponent` | 给节点添加能力的组件基类 | 对象身上的功能模块 |
| `CameraComponent` | 把某个节点变成观察场景的相机 | 摄影机 |
| `LightBase` / concrete lights | 把光照参数绑定到场景节点 | 灯具 |
| `MeshComponent` + `MaterialComponent` | 让节点成为可渲染对象 | 几何外形 + 表面配方 |
| `RenderInputDesc` | 本帧某个 input 的 pipeline、binding、resource 和诊断事实 | 排练单上的一次出场说明 |

## 阅读顺序

1. [文档到 Runtime](document-runtime-flow.md)：`.scene.yaml` 读成文档后，怎样创建运行时 `Scene`。
2. [Node 节点](node.md)：节点的 identity、路径、transform、层级和可见性。
3. [Component 组件](component.md)：组件怎样给节点添加 camera、mesh、material 等能力。
4. [相机](camera.md)：相机作为组件怎样提供 view/projection 和 target 过滤。
5. [光源](light.md)：Directional / Point / Spot 如何挂到节点并形成 scene-level light 数据。
6. [可渲染对象](renderable-object.md)：mesh + material 如何让节点进入 render input 编译。

## 权威入口

| 主题 | 继续阅读 |
|---|---|
| 场景文件格式和 URI | [资产系统：场景文件也是资产](../concepts/assets/scene-assets.md) |
| 材质 pass 和 pipeline identity | [材质系统](../concepts/material/index.md) |
| 当前实现源码 | `src/core/scene/scene.hpp`、`src/core/scene/object.hpp` |
| 源码分析 | [Scene 源码分析](../source_analysis/src/core/scene/scene.md) |
