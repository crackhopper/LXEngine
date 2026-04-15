# 网格对象

这篇文档面向引擎使用者，解释网格对象在场景中的职责、常见使用方式，以及它与材质、场景对象之间的关系。

## 你会在什么场景接触它

你通常会在把“顶点/索引数据变成可渲染对象”时接触 `Mesh`：

- 手工创建 demo 几何体时，先建 `VertexBuffer` / `IndexBuffer`，再 `Mesh::create(...)`
- mesh loader 输出资源时，把解析后的顶点和索引包装成 `Mesh`
- 创建 `SceneNode` 或 `RenderableSubMesh` 时，把 mesh 作为几何输入传进去

当前项目里，mesh 只是“几何资源”，不是场景节点本身。

## 它负责什么

`Mesh` 的职责很集中：

- 组合一个 `VertexBufferPtr` 和一个 `IndexBuffer`
- 提供顶点数、索引数、顶点布局、primitive topology 这些查询接口
- 通过 `getRenderSignature(pass)` 把几何结构贡献给 pipeline identity
- 可选保存 `BoundingBox`

它不负责：

- 材质参数
- 物体变换
- 是否参加某个 pass 的最终决定

这些分别属于 `MaterialInstance`、`ObjectPC` / scene object、以及 `SceneNode` 的 validated cache。

## 常见使用方式

最直接的用法是：

1. 用某种顶点类型创建 `VertexBuffer<T>`。
2. 创建 `IndexBuffer`。
3. 调用 `Mesh::create(vb, ib)`。
4. 把 mesh 交给 `SceneNode::create(...)`。

当前实现里，`Mesh::getRenderSignature(pass)` 虽然保留了 pass 参数，但实际上只看两件事：

- `vertexBuffer->getLayout().getRenderSignature()`
- `indexBuffer->getTopology()`

所以，只要顶点布局或拓扑变化，pipeline identity 就会变化；单纯改顶点数据内容本身，不会改变这个 signature。

## 与其他概念的关系

- 和 `VertexBuffer` / `IndexBuffer`：`Mesh` 是它们的轻量组合层。
- 和 `Material`：material 决定 shader 与 render state，mesh 决定顶点输入布局与拓扑；两者共同参与 pipeline 构建。
- 和 `SceneNode`：`SceneNode` 会拿 mesh 的 `VertexLayout` 去对照 shader reflection 做顶点输入校验。
- 和 `RenderQueue` / backend：后续并不会重新读一个“高层 mesh 对象接口”来建 pipeline，而是直接从 `RenderingItem` 里的 vertex/index buffer 取布局和 topology。

如果你关心更底层的几何抽象和 signature 组成，继续看 [`../subsystems/geometry.md`](../subsystems/geometry.md)。

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

项目里的真实例子可以看 [test_render_triangle.cpp](/home/lx/proj/renderer-demo/src/test/test_render_triangle.cpp:41) 和 [mesh.hpp](/home/lx/proj/renderer-demo/src/core/asset/mesh.hpp:13)。
