# 可渲染对象：网格加材质才进入 draw

可渲染对象像一位准备上场的演员：`MeshComponent` 给它形体，`MaterialComponent` 给它表面和 pass 规则，`SceneNode` 给它位置、名字和可见性。三者合在一起，节点才可能进入 render queue。

## Mesh 和 Material 的边界

| 部分 | 负责什么 | 不负责什么 |
|---|---|---|
| `MeshComponent` | vertex buffer、index buffer、mesh pipeline signature | shader、UBO、纹理 |
| `MaterialComponent` | `MaterialInstance`、pass、shader、descriptor resources | 节点 transform、几何数据 |
| `SceneNode` | transform、visibility、validated pass cache、per-draw data | 文件加载和材质内部 pipeline 解释 |

材质内部的 pass、pipeline identity、系统资源绑定比较复杂，放在 [材质系统](../concepts/material/index.md) 里讲。这里我们只关心它如何和 mesh 一起让节点成为 renderable。

## 结构变化时重建 pass 缓存

`SceneNode` 会在 mesh/material/skeleton 变化后重建 `ValidatedRenderablePassData`：

| 缓存字段 | 来源 |
|---|---|
| `pass` | `MaterialInstance` 当前 enabled passes |
| `shaderInfo` | material pass shader |
| `vertexBuffer` / `indexBuffer` | mesh |
| `descriptorResources` | material resources + skeleton 等 renderable resources |
| `objectSignature` | mesh/material/skeleton 等对象签名 |
| `pipelineKey` | pass 下的 pipeline identity 输入 |

这样 render queue 拿到节点时，不需要重新判断“这个对象能不能画”。它只需要针对当前 pass 和 camera/visibility 过滤，取出已经验证过的 pass 数据。

## 一个 scene YAML 如何表达可渲染对象

```yaml
- nodeName: crate_node
  name: Crate
  transform:
    translation: [0.0, 0.0, 0.0]
  mesh:
    uri: assets/models/builtin/props/crate/model.obj       # -> MeshComponent
  material:
    uri: assets/materials/blinnphong_textured.material     # -> MaterialComponent
  materialOverrides:
    MaterialUBO.baseColor: [0.8, 0.7, 0.4]                 # -> MaterialInstance parameter write
```

YAML 字段本身属于资产系统；节点怎样用这些字段形成 draw 输入，属于场景系统。

## 继续阅读

- [Component 组件](component.md)
- [材质系统](../concepts/material/index.md)
- [源码分析：RenderingItem](../source_analysis/src/core/scene/scene.md)
