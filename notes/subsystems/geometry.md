# Geometry (Mesh + Vertex / Index Buffer)

Geometry 决定“顶点长什么样”和“索引怎么画”。它对 pipeline 的影响通过 `RenderInputDesc.pipelineBuildDesc.vertexLayout` 和 `topology` 表达。

## 核心对象

| 对象 | 当前职责 |
|---|---|
| `Mesh` / `MeshBuffer` | 组合 vertex/index storage，暴露几何切片 |
| `IVertexBuffer` / `VertexBuffer<V>` | 提供顶点字节、stride、layout 和 pipeline signature |
| `IndexBuffer` | 提供索引字节和 primitive topology |
| `VertexLayout` | 描述 shader vertex input contract |
| `RenderDrawInput` | 持有当前 pass 要提交的 mesh/material/object 和 draw command |

## 数据流

```text
mesh loader / builtin mesh
  -> Mesh + VertexBuffer + IndexBuffer
  -> SceneNode validated pass data
  -> RenderWorkCompiler::buildInputs()
  -> RenderDrawInput
  -> RenderWorkCompiler::prepare()
  -> PipelineBuildDesc::graphics(...)
```

`PipelineBuildDesc` 不再从旧 work item 反推。compiler 在 prepare 阶段拿到 vertex layout、topology、shader stages、render state、attachments 和 target 后，直接构造 graphics desc。

## 关键约束

- layout item 的 `location`、`name`、`type`、`inputRate`、`offset`、stride 都影响结构匹配。
- topology 是 pipeline 结构事实。
- object/mesh 本身不再是 pipeline key 独立轴；它们必须满足 RenderPathGraph 的 geometry contract。
- 常用顶点类型定义在 `src/core/rhi/vertex_buffer.hpp`。
- loader 输出要同时满足 shader reflection 和 RenderPathGraph geometry contract。

## 从哪里改

| 想改什么 | 入口 |
|---|---|
| 新顶点类型 | `src/core/rhi/vertex_buffer.hpp` |
| mesh abstraction | `src/core/asset/mesh.*` |
| loader 输出 | `src/infra/mesh_loader/` |
| pass geometry contract 校验 | `src/core/frame_graph/render_work_compiler.*` |

## 关联文档

- [Mesh 源码分析](../source_analysis/src/core/asset/mesh.md)
- [RenderWorkCompiler](../concepts-design/rendering-pipeline/render-work-compiler.md)
