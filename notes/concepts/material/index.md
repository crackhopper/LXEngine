# 材质系统

这篇文档面向引擎使用者，解释材质模板、材质模板 Pass 定义、材质实例，以及它们如何和 shader、scene、pipeline 身份配合工作。

## 你会在什么场景接触它

你在这个项目里接触材质，通常不是“手写一个材质类”，而是：

- 用 loader 创建一个 `MaterialInstance`，例如 `loadBlinnPhongMaterial()`。
- 自己组装 `MaterialTemplate` 和 `MaterialPassDefinition`，定义某个 pass 用什么 shader 和 render state。
- 运行时继续改 UBO 参数、纹理或 pass enable 状态。

## 它负责什么

当前材质系统拆成两层：

- `MaterialTemplate`：定义有哪些 pass、每个 pass 用什么 `MaterialPassDefinition`。
- `MaterialInstance`：保存运行时参数、descriptor 资源，以及哪些 pass 当前启用。

从使用者视角看，材质系统主要负责：

- 决定一个 renderable 能参加哪些 pass。
- 提供 shader 需要的材质级 descriptor 资源，例如 `MaterialUBO`、纹理采样器。
- 通过 `getRenderSignature(pass)` 参与 pipeline identity。

它不负责：

- mesh 顶点布局合法性，这属于 [场景对象](../scene/index.md) 的结构校验。
- scene-level `CameraUBO` / `LightUBO` 的拥有权。
- Vulkan pipeline 的最终构建和缓存，这属于 [渲染管线](../pipeline/index.md)。

## 当前实现状态

- 已实现：`MaterialTemplate`、`MaterialPassDefinition`、`MaterialInstance` 三层模型已经存在。
- 已实现：instance 级 pass enable/disable 和场景传播已经落地，见 [`REQ-022`](../../requirements/022-material-pass-selection.md)。
- 部分实现：自定义材质模板已经可以直接通过 C++ API 组装，但“统一的模板 loader 契约”还没有收敛成使用者入口。
- 尚未实现：统一的自定义材质模板加载契约，见 [`REQ-025`](../../requirements/025-custom-material-template-and-loader.md)。

## 常见使用方式

最常见的路径是：

1. loader 创建 `MaterialTemplate` 或直接返回 `MaterialInstance`。
2. 用 `setVec3` / `setFloat` / `setInt` / `setTexture` 写入运行时参数。
3. 调用 `updateUBO()` 把 dirty 标记推到 GPU 资源包装层。
4. 把材质交给 `SceneNode`。

如果你改的是 `setPassEnabled(pass, enabled)`，这属于结构性变化，会影响所有共享该实例的节点；普通参数写入不会触发 scene 级重验证。关于这些变化如何进入 pipeline 身份和构建，继续看 [渲染管线](../pipeline/index.md)。

## 与其他概念的关系

- 和 [资产系统](../assets/index.md)：material loader 是资产进入运行时的一条关键入口。
- 和 [几何系统](../geometry/index.md)：geometry 提供 vertex input，material 提供 shader/pass/render state；两者共同组成 `PipelineKey`。
- 和 [场景对象](../scene/index.md)：`SceneNode` 持有 `MaterialInstance`，并用它做 pass 级结构校验。
- 和 [光源系统](../light/index.md) / [相机系统](../camera/index.md)：scene-level 资源由 scene 追加，材质不直接拥有它们。

## 示例代码

```cpp
auto material = LX_infra::loadBlinnPhongMaterial();
material->setVec3(StringID("baseColor"), {0.8f, 0.2f, 0.1f});
material->setFloat(StringID("shininess"), 24.0f);
material->setInt(StringID("enableNormal"), 0);
material->updateUBO();

auto node = SceneNode::create("mesh_01", mesh, material, nullptr);
```

如果你想看更偏实现的分层细节，直接读 [`../../subsystems/material-system.md`](../../subsystems/material-system.md)。
