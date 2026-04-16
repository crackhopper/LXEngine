# 几何系统

这篇文档面向引擎使用者，解释几何资源在场景中的职责、常见使用方式，以及它们如何参与渲染输入和 pipeline 身份。

## 你会在什么场景接触它

你通常会在把“顶点/索引数据变成可渲染输入”时接触几何系统：

- 手工创建 demo 几何体时，先建 `VertexBuffer` / `IndexBuffer`，再 `Mesh::create(...)`。
- mesh loader 输出运行时资源时，把解析后的顶点和索引包装成 `Mesh`。
- 创建 `SceneNode` 时，把 mesh 作为几何输入交进去。

## 它负责什么

当前几何系统的核心对象仍然是 `Mesh`。它主要负责：

- 组合一个 `VertexBufferPtr` 和一个 `IndexBuffer`。
- 提供顶点数、索引数、顶点布局、primitive topology 这些查询接口。
- 通过 `getRenderSignature(pass)` 把几何结构贡献给 pipeline identity。
- 可选保存 `BoundingBox`。

它不负责：

- 材质参数，这属于 [材质系统](../material/index.md)。
- 物体变换和场景挂接，这属于 [场景对象](../scene/index.md)。
- pipeline 的最终构建与缓存，这属于 [渲染管线](../pipeline/index.md)。

## 当前实现状态

- 已实现：`Mesh`、`VertexBuffer`、`IndexBuffer`、`VertexLayout`、primitive topology 这一套运行时几何输入已经稳定存在。
- 已实现：`SceneNode` 会拿 mesh 的 `VertexLayout` 对照 shader reflection 做结构校验。
- 部分实现：GLTF/OBJ loader 已经能把磁盘几何转成运行时 mesh，但不同格式的材质桥接与更完整资产语义仍在演进，见 [`REQ-011`](../../requirements/011-gltf-pbr-loader.md)。

## 常见使用方式

最直接的用法是：

1. 用某种顶点类型创建 `VertexBuffer<T>`。
2. 创建 `IndexBuffer`。
3. 调用 `Mesh::create(vb, ib)`。
4. 把 mesh 交给 `SceneNode::create(...)`。

当前实现里，`Mesh::getRenderSignature(pass)` 虽然保留了 pass 参数，但实际上只看两件事：

- `vertexBuffer->getLayout().getRenderSignature()`
- `indexBuffer->getTopology()`

所以，只要顶点布局或拓扑变化，pipeline identity 就会变化；单纯改顶点数据内容本身，不会改变这个 signature。关于这条链路，继续看 [渲染管线](../pipeline/index.md)。

## 与其他概念的关系

- 和 [资产系统](../assets/index.md)：geometry loader 的输出会落到 `Mesh`。
- 和 [材质系统](../material/index.md)：material 决定 shader/pass/render state，geometry 决定 vertex input layout 与 topology；两者共同参与 pipeline 身份。
- 和 [场景对象](../scene/index.md)：`SceneNode` 会把 mesh 组合进 renderable，并做 pass 级结构校验。
- 和 [渲染管线](../pipeline/index.md)：几何系统提供 object-side render signature，是 `PipelineKey` 的一半来源。

## 示例代码

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

如果你关心更底层的几何抽象和 signature 组成，继续看 [`../../subsystems/geometry.md`](../../subsystems/geometry.md)。
