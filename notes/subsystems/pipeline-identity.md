# Pipeline Identity

> Pipeline 身份不是临时拼出来的字符串，而是结构化的 `StringID` 树。现在的关键变化是：object-side identity 已不再包含 skeleton 这一维，skinning 差异改由材质 pass 的 shader variants 表达。
>
> 权威 spec: `openspec/specs/pipeline-key/spec.md` + `openspec/specs/pipeline-signature/spec.md` + `openspec/specs/pipeline-build-desc/spec.md`

## 它解决什么问题

- 明确 pipeline cache 的 key 到底由什么组成。
- 把 backend 需要的构建输入整理成 `PipelineBuildDesc`。
- 保证相同 draw 条件得到相同 key，不同条件得到不同 key。

## 核心对象

- `PipelineKey`：最终身份，只包一个 `StringID`。
- `PipelineBuildDesc`：backend 建 pipeline 的完整输入包。
- `getPipelineSignature(...)`：每个资源贡献自己那一层身份。

## 典型数据流

1. geometry 产出 object-side signature。
2. material pass 产出 material-side signature。
3. `PipelineKey::build(objectSig, materialSig)`。
4. `PipelineBuildDesc::fromRenderingItem(item)` 从 `RenderingItem` 派生 backend 输入。
5. `PipelineCache` 用这个 key 做缓存。

## 关键约束

- object 和 material 分开 compose，再合成 `PipelineKey`。
- pass 参数要沿着 pipeline signature 链一路传下去。
- `SceneNode::getPipelineSignature(pass)` 现在只由 `mesh->getPipelineSignature(pass)` 组成 object-side signature。
- `MaterialInstance::getPipelineSignature(pass)` 只把 `MaterialTemplate::getPipelinePassSignature(pass)` 再包一层 `TypeTag::MaterialRender`。
- skeleton 本身不再提供 pipeline identity token；是否需要骨骼由 shader variant 与 vertex input / descriptor 合同决定。
- `PipelineBuildDesc` 不重新推导 identity，它直接使用 `item.pipelineKey`。

## 当前实现边界

- `Mesh::getPipelineSignature(pass)` 目前保留 `pass` 参数但并不使用；当前直接组合 vertex buffer / index buffer 的 pipeline signature。
- `PipelineBuildDesc::fromRenderingItem(...)` 当前抽取的字段是 `key`、`stages`、`bindings`、`vertexLayout`、`renderState`、`topology`、`pushConstant`。
- 当前 push constant 约定仍是 backend 的固定范围，但 shader ABI 已收敛成 model-only；`PerDrawLayout` 只是 `PerDrawLayoutBase` 的别名。
- shader variants 排序逻辑仍存在于 `ShaderProgramSet`，因此同一组 variants 的插入顺序不会影响最终 identity。

## 从哪里改

- 想让新资源影响 pipeline：实现或修改它的 `getPipelineSignature(...)`。
- 想让 skinning 再次切 key：不要去动 skeleton，自查 material pass variants。
- 想调整 backend 构建输入：看 `PipelineBuildDesc::fromRenderingItem(...)`。

## 关联文档

- `notes/subsystems/string-interning.md`
- `notes/subsystems/material-system.md`
- `notes/subsystems/geometry.md`
