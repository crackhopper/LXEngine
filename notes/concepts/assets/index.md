# 资产如何进入引擎

这篇文档讨论的不是“文件系统长什么样”这么窄的问题，而是更前面的一层：我们怎样把磁盘上的模型、纹理、材质信息和骨骼资源带进运行时，并把它们接到 scene 与渲染路径里。

对一个虽然小、但希望完整的渲染引擎来说，资产系统就是最靠前的一层入口。它回答的是三个连续的问题：

- 资源在磁盘上以什么形式存在
- loader 会把它们变成什么运行时对象
- 这些对象最后如何进入 `SceneNode`、材质和 pipeline 链路

## 从文件到运行时对象

当前项目里还没有一个统一的 `AssetManager`。所谓“资产系统”，更准确地说，是一组已经可以被引擎加载和消费的资源类型，以及围绕它们建立起来的 loader 约定。

现在最核心的资产有四类：

- 网格对象：`Mesh`、`VertexBuffer`、`IndexBuffer`
- 纹理资源：image + sampler 的运行时包装
- 材质相关资源：`MaterialTemplate`、`MaterialInstance` 以及 loader 产物
- 骨骼资源：`Skeleton` 和 `SkeletonData`

这些对象本身不是场景，也不会自己参与 draw。它们更像“被 scene 引用的原材料”。

## 这一层解决什么问题

资产系统解决的是“怎么把磁盘上的内容稳定地带进引擎”这个问题。

如果没有这一层，我们只能在代码里手写顶点数组、手动创建纹理、手动拼材质；这样做可以支撑最小 demo，但不足以支撑真正的引擎使用。

有了这层之后，我们可以稳定地做几件事：

- 准备 `assets/` 目录里的模型、纹理和测试资源
- 用 loader 把它们转换成运行时对象
- 把这些对象交给 [场景对象](../scene/index.md) 或 [材质系统](../material/index.md)
- 再由 [材质系统里的 Pipeline 说明](../material/what-is-pipeline.md) 决定它们如何参与 pipeline 身份与构建

## 网格对象在这里扮演什么角色

如果只是想把一份几何数据变成可渲染输入，最终接触到的通常还是 `Mesh`。

在这个项目里，`Mesh` 不是场景节点，而是一个很轻的几何资源对象。它主要做三件事：

- 组合 `VertexBuffer` 和 `IndexBuffer`
- 提供顶点布局、索引数、拓扑等查询接口
- 通过 `getPipelineSignature(pass)` 把几何结构贡献给 pipeline identity

最直接的使用方式是：

```cpp
auto vb = VertexBuffer<VertexPos>::create({
    {{0.0f, 0.0f, 0.0f}},
    {{1.0f, 0.0f, 0.0f}},
    {{0.0f, 1.0f, 0.0f}},
});
auto ib = IndexBuffer::create({0, 1, 2});
auto mesh = Mesh::create(vb, ib);

auto node = SceneNode::create("triangle", mesh, material, nullptr);
```

这里有一个很实用的边界：

- `Mesh` 负责几何输入
- `MaterialInstance` 负责 shader / pass / 参数
- `SceneNode` 负责把它们组织成真正可参与渲染的对象

## 材质、纹理和模型在入口上怎样汇合

如果走的是更接近真实场景的路径，通常会先加载纹理，再用具体 loader 创建材质实例。

例如当前比较成熟的入口是 `loadBlinnPhongMaterial()`。它会把 shader、pass、默认参数和纹理入口收敛成一个可直接挂到对象上的 `MaterialInstance`。

OBJ / GLTF loader 也已经存在，因此我们不一定要自己手写顶点数组。当前这条模型资产路径的状态是：

- OBJ 路径已经能稳定生成运行时 mesh
- GLTF 路径已经能承载 PBR 相关元数据
- 但“把 glTF 里的材质语义完整桥接成引擎内材质实例”还没有完全收口

## 现在这套系统做到哪了

可以把现状理解成三层：

- 已实现：`assets/` 目录约定与测试资产基线（REQ-010）、OBJ / GLTF mesh loader、texture loading、`Skeleton` 资源、显式 runtime asset root helper（旧 `cdToWhereAssetsExist()` 保留为 fallback）
- 部分实现：GLTF 已经不只是几何输入，还带上了 PBR 材质元数据
- 尚未实现：统一的材质模板加载契约，以及 IBL 资源作为正式资产接入 scene

对应需求：

- [`REQ-010`](../../requirements/finished/010-test-assets-and-layout.md)：测试资产与 `assets/` 目录约定
- [`REQ-011`](../../requirements/finished/011-gltf-pbr-loader.md)：GLTF + PBR 元数据加载
- [`REQ-025`](../../requirements/025-custom-material-template-and-loader.md)：自定义材质模板与模板 loader 契约
- [`REQ-028`](../../requirements/028-ibl-environment-lighting.md)：IBL 环境光资源接入

## 往实现层再走一步

往下看时，当前实现大致是这样分的：

- mesh / texture / skeleton 这些运行时资源在 `core` 层定义稳定类型
- loader 主要在 `infra` 层，把磁盘格式转换成 `core` 层对象
- scene 和 material 再去消费这些对象，形成真正的 draw 输入

继续展开时，可以参考：

- [`../../subsystems/geometry.md`](../../subsystems/geometry.md)
- [`../../subsystems/material-system.md`](../../subsystems/material-system.md)
- [`../../subsystems/skeleton.md`](../../subsystems/skeleton.md)
