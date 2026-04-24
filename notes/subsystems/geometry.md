# Geometry (Mesh + Vertex / Index Buffer)

> Geometry 这一层决定“顶点长什么样”和“索引怎么画”。它对 pipeline 身份的影响很直接，因为 vertex layout 和 topology 都会进入 signature。
>
> 相关 spec: `openspec/specs/pipeline-signature/spec.md` + `openspec/specs/mesh-loading/spec.md`

## 它解决什么问题

- 统一 mesh、vertex buffer、index buffer 的抽象。
- 让 vertex layout 成为 pipeline identity 的一部分。
- 让 backend 能从 `RenderingItem` 里还原构建 pipeline 所需的输入布局。

## 核心对象

- `Mesh`：组合 vertex buffer 和 index buffer。
- `VertexLayoutItem`：单个顶点属性描述。
- `VertexLayout`：完整布局和 stride。
- `IVertexBuffer` / `VertexBuffer<V>`：携带顶点字节和布局契约。
- `IndexBuffer`：索引数据和 primitive topology。

## 典型数据流

1. loader 创建 `VertexBuffer<V>` 和 `IndexBuffer`。
2. `Mesh::create(vb, ib)`。
3. `SceneNode` 持有 mesh（`IRenderable` 当前唯一具体实现）。
4. `mesh->getPipelineSignature(pass)` 贡献 object-side signature。
5. `PipelineBuildDesc::fromRenderingItem(item)` 读回 layout 和 topology。

## 关键约束

- layout item 的 `location` / `name` / `type` / `inputRate` / `offset` 都影响 signature。
- `VertexLayoutItem::size` 参与相等比较，但当前不参与 pipeline signature 组装。
- stride 也参与 signature。
- topology 是叶子字符串，不再额外 compose。
- `Mesh::getPipelineSignature(pass)` 现在基本不使用 pass，但接口要保留这个参数。

## 当前实现边界

- `Mesh::getPipelineSignature(pass)` 当前直接组合 `vertexBuffer->getPipelineSignature()` 和 `indexBuffer->getPipelineSignature()`，最终 compose 成 `TypeTag::MeshRender`。
- `PipelineBuildDesc::fromRenderingItem(...)` 实际读取 layout 的方式不是通过 `Mesh`，而是把 `item.vertexBuffer` 动态转成 `IVertexBuffer`，把 `item.indexBuffer` 动态转成 `IndexBuffer` 后直接取 `getLayout()` 和 `getTopology()`。
- `IndexBuffer` 现在直接承担两件事：暴露索引字节，以及暴露 topology 供 pipeline 装配使用。
- 常用顶点类型目前直接定义在 `vertex_buffer.hpp`，例如 `VertexPos`、`VertexPBR`、`VertexPosNormalUvBone`。

## 从哪里改

- 想加新顶点类型：看 `vertex_buffer.hpp`。
- 想改 pipeline 命中行为：看 layout signature 组成。
- 想改 loader 输出：看 `src/infra/mesh_loader/`。

## 关联文档

- `notes/subsystems/pipeline-identity.md`
- `notes/subsystems/scene.md`
- `openspec/specs/mesh-loading/spec.md`
