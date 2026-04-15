# 材质对象

这篇文档面向引擎使用者，解释材质对象在场景中的职责、常见使用方式，以及它与网格、光照和渲染 pass 之间的关系。

## 你会在什么场景接触它

你在这个项目里接触材质，通常不是“手写一个材质类”，而是：

- 用 loader 创建一个 `MaterialInstance`，例如 `loadBlinnPhongMaterial()`。
- 把它交给 `SceneNode` 或 `RenderableSubMesh`。
- 运行时继续改 UBO 参数、纹理或 pass enable 状态。

也就是说，当前真正面向使用者的“材质对象”是 `MaterialInstance`；`MaterialTemplate` 更像 loader 和运行时内部的结构定义层。

## 它负责什么

当前实现把材质拆成两层：

- `MaterialTemplate`：定义有哪些 pass、每个 pass 用什么 shader、启用了哪些 variants、使用什么 `RenderState`。
- `MaterialInstance`：保存运行时参数，例如 `MaterialUBO` 里的数值、纹理资源、以及哪些 pass 当前启用。

从使用者视角看，材质对象主要负责：

- 决定一个 renderable 能参加哪些 pass。
- 提供 shader 需要的材质级 descriptor 资源，例如 `MaterialUBO`、纹理采样器。
- 通过 `getRenderSignature(pass)` 参与 pipeline identity。

它不负责 mesh 顶点布局合法性，也不负责 skeleton 是否匹配。这些结构校验在 `SceneNode::rebuildValidatedCache()` 里完成。

## 常见使用方式

最常见的路径是：

1. `loadBlinnPhongMaterial(...)` 创建一个 `MaterialInstance`。
2. 用 `setVec3` / `setFloat` / `setInt` / `setTexture` 写入运行时参数。
3. 调用 `updateUBO()` 把 dirty 标记推到 GPU 资源包装层。
4. 把材质交给 `SceneNode`。

如果你改的是 `setPassEnabled(pass, enabled)`，这属于结构性变化：

- 对未定义 pass 调用会直接 `FATAL + terminate`
- 如果多个 `SceneNode` 共享同一个 `MaterialInstance`，`Scene` 会把这次变化传播给所有引用它的节点并重建 validated cache

如果你改的是普通参数，例如 `setFloat("shininess", ...)` 或 `setTexture(...)`，不会触发 scene 级重验证。

## 与其他概念的关系

- 和 `Mesh`：mesh 提供几何结构，material 提供 shader/pass/render state；两者一起组成 `PipelineKey`。
- 和 `SceneNode`：`SceneNode` 持有 `MaterialInstance`，并用它的 enabled passes、shader reflection、descriptor resources 做结构校验。
- 和 `Light` / `Camera`：`MaterialInstance` 自己只提供材质拥有的资源；`CameraUBO`、`LightUBO` 属于 scene-level 资源，由 `Scene` 追加。
- 和 loader：当前最成熟的材质入口是 [blinn_phong_material_loader.cpp](/home/lx/proj/renderer-demo/src/infra/material_loader/blinn_phong_material_loader.cpp:77)，它会规范化 variants、编译 `blinnphong_0`、创建 template，再返回已写入默认参数的 `MaterialInstance`。

如果你想看更偏实现的分层细节，直接读 [`../subsystems/material-system.md`](../subsystems/material-system.md)。

## 示例代码

```cpp
auto material = LX_infra::loadBlinnPhongMaterial();
material->setVec3(StringID("baseColor"), {0.8f, 0.2f, 0.1f});
material->setFloat(StringID("shininess"), 24.0f);
material->setInt(StringID("enableNormal"), 0);
material->updateUBO();

auto node = SceneNode::create("mesh_01", mesh, material, nullptr);
```

如果你要共享同一份材质给多个对象，这是允许的；但要记住，`setPassEnabled(...)` 会影响所有引用这个实例的节点，而普通参数写入只是共享同一份材质值。
