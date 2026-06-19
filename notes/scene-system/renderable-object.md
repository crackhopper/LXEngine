# 可渲染对象：网格加材质才进入 draw

可渲染对象像一位准备上场的演员：`MeshComponent` 给它形体，`MaterialComponent` 给它表面和 pass 规则，`SceneNode` 给它位置、名字和可见性。三者合在一起，节点才可能被 `RenderWorkCompiler` 接收为 `RenderDrawInput`。

## Mesh 和 Material 的边界

| 部分 | 负责什么 | 不负责什么 |
|---|---|---|
| `MeshComponent` | vertex buffer、index buffer、mesh pipeline signature | shader、UBO、纹理 |
| `MaterialComponent` | `MaterialInstance`、surface envelope、资源依赖 | 节点 transform、几何数据、pass/shader 选择 |
| `SceneNode` | transform、visibility、validated pass cache、per-draw data | 文件加载和材质内部 pipeline 解释 |

材质内部的 pass、pipeline identity、系统资源绑定比较复杂，放在 [材质系统](../concepts/material/index.md) 里讲。这里我们只关心它如何和 mesh 一起让节点成为 renderable。

## 结构变化时重建 pass 缓存

`SceneNode` 会在 mesh/material/skeleton 变化后重建 `ValidatedRenderablePassData`：

| 缓存字段 | 来源 |
|---|---|
| `pass` | active RenderPathGraph 中匹配 render class / BSDF type 的 pass |
| `shaderInfo` | RenderPathGraph pass shader |
| `vertexBuffer` / `indexBuffer` | mesh |
| `descriptorResources` | material/feature/scene/skeleton 等资源 |
| `materialTypeVariant` | material contract / source variant |
| `renderPathNodeSignature` | graph pass 的 shader/renderState/attachment/geometry contract |
| `pipelineKey` | `MaterialTypeVariant + RenderPathNodeSignature` |

这样 `RenderWorkCompiler` 处理节点时，不需要重新判断“这个对象能不能画”。它只需要针对当前 pass 和 camera/visibility 过滤，取出已经验证过的 pass 数据，再生成 `RenderDrawInput` 和 `RenderInputDesc`。

## 一个 scene YAML 如何表达可渲染对象

```yaml
- nodeName: crate_node
  name: Crate
  transform:
    translation: [0.0, 0.0, 0.0]
  mesh:
    uri: assets/models/builtin/props/crate/model.obj       # -> MeshComponent
  material:
    uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material # -> MaterialComponent
  materialOverrides:
    baseColor: { kind: rgb, value: [0.8, 0.7, 0.4] }       # -> material envelope override
    roughness: { kind: float, value: 0.35 }
```

YAML 字段本身属于资产系统；节点怎样用这些字段形成 draw input，属于场景系统。

## 继续阅读

- [Component 组件](component.md)
- [材质系统](../concepts/material/index.md)
- [源码分析：Scene](../source_analysis/src/core/scene/scene.md)
