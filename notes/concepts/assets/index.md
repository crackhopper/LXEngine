# 资产系统

这篇文档面向引擎使用者，解释“哪些资源可以被引擎加载”，以及这些资源如何进入运行时并最终参与渲染。

## 你会在什么场景接触它

你通常会在三种场景下直接碰到资产系统：

- 准备测试或 demo 资源时，关心仓库里的 `assets/` 应该怎么组织。
- 从文件加载模型、纹理或材质信息时，关心 loader 会产出什么运行时对象。
- 把加载结果交给 `SceneNode`、`MaterialInstance` 或 shader 时，关心资源所有权和后续入口。

当前项目里，资产系统还不是一个统一的 `AssetManager`。它更像一组“运行时可加载资源 + 对应 loader 约定”的集合。

## 它负责什么

这里的资产系统主要负责两件事：

- 定义哪些内容算“引擎当前能消费的资源”，例如 mesh、texture、material、skeleton。
- 提供把磁盘内容转换成运行时对象的入口，例如 mesh loader、texture loader、material loader。

它不负责：

- 场景组织，这属于 [场景对象](../scene/index.md)。
- pipeline 身份和构建，这属于 [渲染管线](../pipeline/index.md)。
- backend 资源上传和 Vulkan 生命周期，这属于设计层文档。

## 当前实现状态

- 已实现：`assets/` 目录约定和测试资产基线，见 [`REQ-010`](../../requirements/010-test-assets-and-layout.md)。
- 已实现：OBJ / GLTF mesh loader、texture loading、`MaterialInstance` 运行时资源、`Skeleton` 资源入口已经存在。
- 部分实现：GLTF 路径已经能承载 PBR 材质元数据，但距离“统一把 glTF 资产桥接成引擎内材质实例”还差一层收敛，见 [`REQ-011`](../../requirements/011-gltf-pbr-loader.md) 和 [`REQ-019`](../../requirements/019-demo-scene-viewer.md)。
- 尚未实现：统一的“自定义材质模板加载契约”，当前仍以具体 loader 为主，见 [`REQ-025`](../../requirements/025-custom-material-template-and-loader.md)。
- 尚未实现：IBL 环境贴图作为正式 scene-level 资产进入运行时，见 [`REQ-028`](../../requirements/028-ibl-environment-lighting.md)。

## 与其他概念的关系

- 和 [几何系统](../geometry/index.md)：mesh loader 的结果最终落到 `Mesh`、`VertexBuffer`、`IndexBuffer`。
- 和 [材质系统](../material/index.md)：材质资产最终要么变成 `MaterialTemplate`，要么变成 `MaterialInstance`。
- 和 [场景对象](../scene/index.md)：资产不会自己参与渲染，必须被场景对象引用后才进入 draw 路径。
- 和 [渲染管线](../pipeline/index.md)：资产只提供输入，真正决定 pipeline 身份的是这些输入在 pass 维度上的组合方式。

## 继续阅读

- 更底层的几何输入结构：[`../../subsystems/geometry.md`](../../subsystems/geometry.md)
- 当前材质实现：[`../../subsystems/material-system.md`](../../subsystems/material-system.md)
- 骨骼资源：[`../../subsystems/skeleton.md`](../../subsystems/skeleton.md)
