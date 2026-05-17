# 文档到 Runtime：场景怎样被装配起来

场景装配像把一张舞台布置图落到真实舞台上。资产系统已经把 `.scene.yaml` 读成 `SceneDocument`；场景系统接手之后，要把文档节点变成 `SceneNode`、组件、相机、光源和可渲染对象。

## 文件层和运行时层分开

| 层 | 对象 | 负责什么 |
|---|---|---|
| 文件层 | `.scene.yaml` | 保存可编辑、可复制的 YAML |
| 文档层 | `SceneDocument` / `SceneNodeDocument` | 保存反序列化后的字段和 URI |
| 装配层 | `SceneRuntime` | 根据文档创建 runtime 对象 |
| 运行时层 | `Scene` / `SceneNode` / components | 被 editor、queue、renderer 查询和修改 |

场景系统主要从装配层开始。文件字段的具体语义见 [资产系统：场景文件也是资产](../concepts/assets/scene-assets.md)。

## SceneRuntime 的装配顺序

`SceneRuntime` 当前按这个顺序建立运行时场景：

| 步骤 | 当前行为 |
|---|---|
| 创建 `Scene` | 使用文档里的 scene name，并清理默认 seed light |
| 映射 root | 把文档 root 的 identity、transform、visibility 写到 runtime root |
| 递归节点 | 遍历 `SceneNodeDocument::children` |
| 创建 camera 节点 | 有 `camera` 字段时创建 `CameraComponent` 并注册到 scene camera 列表 |
| 创建 renderable 节点 | 有 `mesh.uri` 时加载 mesh，并配置 material |
| attach light | 有 `light` 字段时创建 concrete light 并绑定到节点 |
| 绑定 gameplay camera | 用 `gameplayCameraPath` 查找 runtime path |
| 准备 editor camera | editor camera 作为 runtime 辅助相机存在，不按普通场景节点保存 |

这条链路让我们可以把“可保存的文档”和“运行时辅助对象”分开。scene 文件保存 project 内容，debug draw 和 editor camera 这种运行时状态不会混进普通节点树。

## 一个节点的 YAML 如何落地

```yaml
- nodeName: helmet_node                    # -> SceneNode::getNodeName()
  name: helmet                             # -> SceneNode::getName()
  transform:                               # -> SceneNode::setLocalTransform(...)
    translation: [0.0, 0.0, 0.0]
    rotation: [0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  mesh:
    uri: assets/models/damaged_helmet/DamagedHelmet.gltf # -> MeshComponent
  material:
    uri: assets/materials/blinnphong_textured.material   # -> MaterialComponent
```

这里的 URI 解析和 loader 属于资产系统；把 mesh/material 放到 `SceneNode` 上，属于场景系统。

## 继续阅读

- [Node 节点](node.md)
- [Component 组件](component.md)
- [资产系统：场景文件也是资产](../concepts/assets/scene-assets.md)
