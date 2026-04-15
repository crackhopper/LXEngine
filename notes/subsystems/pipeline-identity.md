# Pipeline Identity

> Pipeline 身份不是临时拼出来的字符串，而是结构化的 `StringID` 树。目标是稳定地区分“哪些 draw 可以共用同一条 pipeline”。
>
> 权威 spec: `openspec/specs/pipeline-key/spec.md` + `openspec/specs/render-signature/spec.md` + `openspec/specs/pipeline-build-desc/spec.md`

## 它解决什么问题

- 明确 pipeline cache 的 key 到底由什么组成。
- 把 backend 需要的构建输入整理成 `PipelineBuildDesc`。
- 保证相同 draw 条件得到相同 key，不同条件得到不同 key。

## 核心对象

- `PipelineKey`：最终身份，只包一个 `StringID`。
- `PipelineBuildDesc`：backend 建 pipeline 的完整输入包。
- `getRenderSignature(...)`：每个资源贡献自己那一层身份。

## 典型数据流

1. geometry 产出 object-side signature。
2. material 产出 material-side signature。
3. `PipelineKey::build(objectSig, materialSig)`。
4. `PipelineBuildDesc::fromRenderingItem(item)` 从 `RenderingItem` 派生 backend 输入。
5. `PipelineCache` 用这个 key 做缓存。

## 关键约束

- object 和 material 分开 compose，再合成 `PipelineKey`。
- pass 参数要沿着 render signature 链一路传下去。
- `RenderableSubMesh` 的 object-side signature 当前由 `mesh->getRenderSignature(pass)` 和可选的 skeleton signature 组成，最终 compose 成 `TypeTag::ObjectRender`。
- `MaterialInstance::getRenderSignature(pass)` 当前只把 `MaterialTemplate::getRenderPassSignature(pass)` 再包一层 `TypeTag::MaterialRender`。
- skeleton 的“启用/禁用”是单独的一维身份；当前有 skeleton 时就是固定叶子 `Skn1`，没有 skeleton 时是空 `StringID`。
- `PipelineBuildDesc` 不重新推导 identity，它直接使用 `item.pipelineKey`。

## 当前实现边界

- `Mesh::getRenderSignature(pass)` 目前保留 `pass` 参数但并不使用；当前只由 vertex layout signature 和 topology signature 组成。
- `PipelineBuildDesc::fromRenderingItem(...)` 当前抽取的字段是 `key`、`stages`、`bindings`、`vertexLayout`、`renderState`、`topology`、`pushConstant`。其中 `pushConstant` 不是从 shader 反射出来的，而是固定的 engine-wide 约定。
- 这条固定约定当前是 `PushConstantRange{ offset = 0, size = 128, stageFlagsMask = Vertex | Fragment }`。
- `PipelineBuildDesc::fromRenderingItem(...)` 依赖 `item.shaderInfo`、`item.vertexBuffer`、`item.indexBuffer`、`item.material` 都非空；不满足时会触发断言。
- 文档里提到的 shader variants 排序逻辑只在 `ShaderProgramSet` 这一层仍然存在，但当前 `PipelineKey` 的主路径并不直接从 variants 列表组 key，而是走已经组合好的 render signature。

## 从哪里改

- 想让新资源影响 pipeline：实现或修改它的 `getRenderSignature(...)`。
- 想调整 backend 构建输入：看 `PipelineBuildDesc::fromRenderingItem(...)`。
- 想排查 cache miss：先看 `toDebugString(item.pipelineKey.id)`。

## 关联文档

- `notes/subsystems/string-interning.md`
- `notes/subsystems/pipeline-cache.md`
- `notes/subsystems/geometry.md`
