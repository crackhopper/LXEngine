# Mesh：几何接口形状如何进入渲染签名

本页的主体内容由 `scripts/extract_source_analysis.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/asset/mesh.hpp](../../../../../src/core/asset/mesh.hpp)
出发，但真正想回答的问题不是“Mesh 里有哪些字段”，而是：
为什么项目把几何对象做得这么薄，却仍然能让 pipeline、scene 校验和 backend
都拿到自己需要的结构事实。

可以先带着一个问题阅读：`Mesh` 为什么没有直接保存材质、draw state，
却还能参与 `PipelineKey`？答案是，这里真正进入渲染签名的不是“几何内容本身”，
而是顶点输入布局和图元拓扑这两类几何接口形状。

源码入口：[mesh.hpp](../../../../src/core/asset/mesh.hpp)

关联源码：

- [vertex_buffer.hpp](../../../../src/core/rhi/vertex_buffer.hpp)
- [index_buffer.hpp](../../../../src/core/rhi/index_buffer.hpp)

## mesh.hpp

源码位置：[mesh.hpp](../../../../src/core/asset/mesh.hpp)

### Mesh：把几何资源收束成渲染路径可复用的薄边界

`Mesh` 故意不是“顶点数组 + 索引数组 + 一堆绘制状态”的大对象，而是一个很薄的
聚合边界：只把 `IVertexBuffer`、`IndexBuffer` 和包围盒绑定在一起。

这条边界回答的是“一个可绘制几何体最少需要什么结构事实”：

- 顶点布局是什么
- 图元怎样组装
- CPU / backend 都要面对的原始 buffer 在哪里

材质、shader variant、pass enable 这些都不属于 `Mesh`。这样 mesh 才能被多个材质、
多个 scene node 复用，而不会把几何身份和材质身份混成一个缓存键。

### 几何签名：mesh 只输出 pipeline 真正关心的结构信息

`getRenderSignature()` 只组合两类东西：

- 顶点输入布局
- 索引拓扑

它们不看顶点个数、索引范围、包围盒，也不看具体字节内容。原因是 pipeline 身份只关心
“这个 draw 需要怎样的 vertex input / primitive assembly 约束”，而不关心这次画了多少个点。

因此 `MeshRender` 更像“几何接口形状”，不是几何数据内容的内容哈希。

## vertex_buffer.hpp

源码位置：[vertex_buffer.hpp](../../../../src/core/rhi/vertex_buffer.hpp)

### VertexLayout：把 shader 关心的顶点输入契约显式带出来

`VertexLayout` 不是单纯给 backend 上传用的元数据；它承担的是“mesh 如何向 shader
暴露顶点输入接口”的结构契约。这里明确记录：

- attribute 名字和 location
- 数据类型
- stride / offset
- vertex 还是 instance 频率

后面的 `Mesh::getRenderSignature()`、`PipelineBuildDesc`、`SceneNode` 校验都会消费它，
因为这些流程真正关心的是顶点输入形状，而不是 `VertexPosUv` 这类 C++ 顶点类型名。

### IVertexBuffer：上传契约之外，再补一层“布局可见性”

`IGpuResource` 已经足够表达“这是一块要上传到 GPU 的字节”，但对 vertex buffer 来说
还差一件关键事实：shader 该如何解释这些字节。

所以 `IVertexBuffer` 在通用资源契约之上补了 `getLayout()`。
这让同一份顶点数据既能走统一的资源上传路径，又能在 pipeline 构建时把顶点输入布局带出来。

## index_buffer.hpp

源码位置：[index_buffer.hpp](../../../../src/core/rhi/index_buffer.hpp)

### PrimitiveTopology：索引 buffer 不只是字节，还携带装配语义

索引本身只是整数序列，但一旦进入渲染路径，backend 还必须知道这些整数要按什么方式
组装成图元。因此 `PrimitiveTopology` 被放在 `IndexBuffer` 一侧，而不是藏进 draw call 临时参数：

- 它和索引数据一起定义了“几何如何被解释”
- 它直接参与 `Mesh` 的 render signature
- 改拓扑会改变 pipeline 需求，即使索引字节完全不变

### topologySignature：把拓扑收束成可组合的结构叶子

`Mesh::getRenderSignature()` 需要把“顶点布局 + 图元拓扑”一起收束进 `StringID` 组合树。
`topologySignature()` 的角色就是把枚举值变成稳定的叶子签名，让更外层不用关心底层 enum 编码。

### IndexBuffer：索引数据与 pipeline 装配约束的共同载体

`IndexBuffer` 在这里承担两类职责：

- 作为 `IGpuResource` 暴露原始索引字节，供 backend 上传和绑定
- 作为几何结构的一部分，暴露 topology

所以它不是“纯数据容器”。只要 topology 变化，即使索引值没变，pipeline 侧看到的几何接口
也已经变了。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 推荐阅读顺序

这页最适合按下面顺序读：

1. 先看 `Mesh`，建立“几何对象本身为什么这么薄”的边界感
2. 再看 `VertexLayout` / `IVertexBuffer`，理解顶点输入契约如何从字节数据里被显式带出来
3. 最后看 `PrimitiveTopology` / `IndexBuffer`，补齐图元装配这一半的结构事实

按这个顺序读，会更容易看清 `MeshRender` 真正在表达什么：不是一份网格内容摘要，而是一份几何接口签名。

## 它和子系统文档的关系

如果你想继续往外层追，可以接着看：

- `notes/subsystems/geometry.md`：解释更完整的几何资源语义，以及不同顶点类型怎样进入项目
- `notes/subsystems/pipeline-identity.md`：解释 `MeshRender` 最终怎样进入 `PipelineKey`

这一页只贴着源码解释“几何接口形状”本身，不展开 mesh loader、scene 组织或 backend buffer 上传的全流程。
