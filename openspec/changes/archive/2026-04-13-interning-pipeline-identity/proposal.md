## Why

现有 `PipelineKey::build(ShaderProgramSet, Mesh, RenderState, SkeletonSharedPtr)` 把每个资源的 `getPipelineHash() → size_t` 混成一个 `blinnphong_0||ml:0x3a2f1b7c|rs:0x7c1de4a0|sk:0x0` 格式的字符串，再喂给 `StringID`。这套方案解决了"有稳定 key 可用"的问题但留下三个短板：

1. **调试只能看 16 进制 hash**，看不到 layout、variants、state 具体是什么
2. **`Mesh::getPipelineHash()` 把 layout + topology 粗粒度混在一起**，任何子项变化都重建整张 pipeline，无法做局部命中分析
3. **缺失 pass 参数**：引入 Forward / Shadow / Deferred 多 pass 后，同一个 mesh+material 在不同 pass 下需要不同 pipeline，现有 identity 无法表达

REQ-006 已经为 `GlobalStringTable` 加上了结构化 `compose`/`decompose`/`toDebugString`。本变更用它把 pipeline identity 改造成一棵**结构化 StringID 树**：叶子是人类可读名字，中间节点带 `TypeTag`，最终 `PipelineKey.id` 可通过 `toDebugString()` 完整还原成 `PipelineKey(ObjectRender(MeshRender(VertexLayout(...), tri), Skn1), MaterialRender(RenderPassEntry(ShaderProgram(...), RenderState(...))))` 这种形态。

## What Changes

- **新增 `getRenderSignature(...)` API**：`VertexLayoutItem`、`VertexLayout`、`RenderState`、`ShaderProgramSet`、`Skeleton`、`RenderPassEntry` 提供无参版；`Mesh`、`MaterialTemplate::getRenderPassSignature`、`IMaterial`、`IRenderable` 提供带 `StringID pass` 版本
- **新增 pass 常量**：`src/core/scene/pass.hpp` — `Pass_Forward` / `Pass_Deferred` / `Pass_Shadow`
- **新增自由函数** `topologySignature(PrimitiveTopology)`；`DataType` / `VertexInputRate` / `CullMode` / `CompareOp` / `BlendFactor` 的 `toString(...)` 辅助
- **`PipelineKey::build` 签名重写**为 `build(StringID objectSig, StringID materialSig)`，用两级 `compose(TypeTag::PipelineKey, {objSig, matSig})` 构造
- **`MaterialTemplate::m_passes` 从 `unordered_map<std::string, ...>` 迁移到 `unordered_map<StringID, ..., StringID::Hash>`**；`setPass` / `getEntry` 签名改用 `StringID`
- **`IRenderable::getRenderSignature(StringID pass)` 新增为纯虚**；`RenderableSubMesh` 实现
- **`Scene::buildRenderingItem(StringID pass)`** 新增 `pass` 参数；`RenderingItem.pass` 新增字段
- **BREAKING — 显式删除 `getPipelineHash()` 系列**：
  - `Mesh::getPipelineHash()`（`getLayoutHash()` 保留作内部 hash 用）
  - `Skeleton::getPipelineHash()` 和 `kSkeletonPipelineHashTag`
  - `RenderState::getPipelineHash()`、`RenderPassEntry::getPipelineHash()`、`ShaderProgramSet::getPipelineHash()`
  - `pipeline_key.cpp` 中 `variantSegment()` 辅助函数
  - 旧 `PipelineKey::build(ShaderProgramSet, Mesh, RenderState, SkeletonSharedPtr)` 重载
- **归档文档打 Superseded banner**：`docs/requirements/finished/001-skeleton-to-resources.md` 和 `.../002-pipeline-key.md` 顶部加一行 "Superseded by REQ-007"；**不改正文**
- **调用点迁移**：`blinnphong_material_loader`、`test_material_instance` 的 `setPass("Forward", ...)` 改用 `setPass(Pass_Forward, ...)`；`vk_renderer.cpp` 与三个 `test_vulkan_*.cpp` 的 `scene->buildRenderingItem()` 改为传 `Pass_Forward`

## Capabilities

### New Capabilities

- `render-signature`: 定义资源类（VertexLayout / Mesh / ShaderProgramSet / RenderState / Skeleton / RenderPassEntry / MaterialTemplate / MaterialInstance / RenderableSubMesh / Scene）的 `getRenderSignature(...)` 与 `Pass_*` 常量的契约；pipeline identity 由此结构化 compose 产出

### Modified Capabilities

- `pipeline-key`: `PipelineKey::build` 签名改为 `(StringID objectSig, StringID materialSig)`，实现走 `compose(TypeTag::PipelineKey, ...)`；`RenderingItem` 新增 `pass` 字段；`Scene::buildRenderingItem` 接受 `StringID pass`
- `resource-pipeline-hash`: 明确废弃 `getPipelineHash()` 作为 pipeline identity 贡献者的角色（从规范中移除），但保留 `getHash()` / `getLayoutHash()` 作为 `unordered_map` 键用途
- `skeleton-resource`: 移除 `Skeleton::getPipelineHash()` 与 `kSkeletonPipelineHashTag`，换成 `Skeleton::getRenderSignature()`

## Impact

- **硬依赖**：REQ-005（MaterialInstance 是 IMaterial 实现，已完成）+ REQ-006（GlobalStringTable compose/decompose/TypeTag，已完成 change `extend-string-table-compose`）
- **受影响代码**：
  - `src/core/scene/pass.hpp` — 新
  - `src/core/resources/vertex_buffer.hpp` / `index_buffer.{hpp,cpp}` / `mesh.hpp` / `skeleton.hpp` / `shader.hpp` / `material.{hpp,cpp}` / `pipeline_key.{hpp,cpp}`
  - `src/core/scene/object.hpp` / `scene.{hpp,cpp}`
  - `src/infra/loaders/blinnphong_material_loader.cpp`
  - `src/backend/vulkan/vk_renderer.cpp`
  - `src/test/integration/test_material_instance.cpp`、`test_vulkan_pipeline.cpp`、`test_vulkan_resource_manager.cpp`、`test_vulkan_command_buffer.cpp`
  - **新**测试：`src/test/integration/test_pipeline_identity.cpp`
- **归档文档标注**：`docs/requirements/finished/001-*.md`、`.../002-*.md` — 仅加 Superseded banner
- **backend 无改动**：`vk_resource_manager::getOrCreateRenderPipeline` 继续按 `item.pipelineKey` 查找
- **运行时行为**：`toDebugString(pipelineKey.id)` 可用于日志和 `test_pipeline_identity` 断言；pipeline cache 命中逻辑保持位精确等价
