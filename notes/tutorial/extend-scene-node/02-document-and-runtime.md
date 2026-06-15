# 当前节点如何保存和重建：从记录单搬回舞台

Scene document 像舞台记录单，runtime scene 像真正摆好的舞台。保存时，editor 把舞台状态写回记录单；加载时，`SceneRuntime` 按记录单重新搭建节点。

## 当前数据流

| 阶段 | 对象 | 说明 |
|---|---|---|
| 保存格式 | `SceneNodeDocument` | 记录 nodeName、transform、meshUri、materialUri、camera、light 等字段 |
| 运行时构建 | `SceneRuntime` | 递归读取 document 并创建 `SceneNode` |
| 渲染能力 | component 组合 | mesh/material/skeleton 等 component 决定是否可渲染 |
| 编辑器操作 | command + scene tree | 通过 path 找节点并修改文档与 runtime |

这个流向说明了为什么自定义节点不能只改一个地方。只要它需要保存、加载和编辑，就必须同时考虑文档层和运行时层。

## 一个概念 YAML 片段

下面的片段帮助我们理解当前 document 形状。实际字段以当前保存出来的 `.scene.yaml` 和 `scene_document.hpp` 为准。

```yaml
nodes:
  - nodeName: primitive_cube_0          # -> SceneNodeDocument.nodeName
    name: Cube                         # -> editor path segment
    transform:                         # -> SceneNode local transform
      translation: [0.0, 0.5, 0.0]
      rotation: [0.0, 0.0, 0.0]
      scale: [1.0, 1.0, 1.0]
    meshUri: builtin://lxe_editor/primitives/cube
    materialUri: assets://materials/rtr_experiment_template.material
```

这里 `meshUri` 和 `materialUri` 是“这是什么道具外形和涂装”。camera 和 light 则是节点上的其他能力。

## Runtime 构建时做什么

| 逻辑 | 目的 |
|---|---|
| 递归创建节点 | 还原父子层级 |
| 应用 identity 和 transform | 保持 path、name、位置 |
| 根据 `meshUri` 创建 renderable | 让节点参与渲染 |
| 应用 camera / light state | 让节点成为相机或光源 |
| 建立 path 查找表 | 支持选择、命令和 API |

新增节点语义也必须进入这条流：document 能描述它，runtime 能构建它，editor 能操作它。

## 我们已经学会了什么

我们知道 scene node 的持久化不是“保存一个 C++ 对象”，而是保存一份文档负载，再从文档重建 runtime 节点。

## 下一步

进入 [03 新增节点 kind 的当前触点](03-current-custom-node-touchpoints.md)，看当前手工扩展的真实成本。
