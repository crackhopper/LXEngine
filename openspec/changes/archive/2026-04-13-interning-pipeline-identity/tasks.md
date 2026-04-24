## 1. Pass 常量与 enum stringifier

- [x] 1.1 新建 `src/core/scene/pass.hpp`，声明 `inline const StringID Pass_Forward = StringID("Forward")`、`Pass_Deferred = StringID("Deferred")`、`Pass_Shadow = StringID("Shadow")`，注释说明为何不能 `constexpr`
- [x] 1.2 在 `src/core/resources/material.hpp` 增补 `inline const char *toString(CullMode)` / `toString(CompareOp)` / `toString(BlendFactor)`（覆盖所有枚举值，`switch` + `-Wswitch`）
- [x] 1.3 在 `src/core/resources/vertex_buffer.hpp` 增补 `inline const char *toString(DataType)` 与 `toString(VertexInputRate)`
- [x] 1.4 `grep` 代码库确认上述 `toString` 名字没有已有的冲突定义；如有，复用它而非新增

## 2. 叶子 / 中间 getRenderSignature

- [x] 2.1 在 `src/core/resources/vertex_buffer.hpp` 为 `VertexLayoutItem` 新增 `StringID getRenderSignature() const`，格式 `"{location}_{name}_{type}_{inputRate}_{offset}"`，走 `GlobalStringTable::get().Intern(...)`
- [x] 2.2 为 `VertexLayout` 新增 `StringID getRenderSignature() const`，对每个 item 调用 `getRenderSignature()`，附加 `Intern(std::to_string(m_stride))` 作最后一个 field，调用 `compose(TypeTag::VertexLayout, parts)`
- [x] 2.3 在 `src/core/resources/index_buffer.hpp` 添加 inline 自由函数 `StringID topologySignature(PrimitiveTopology t)`，`switch` 返回 `Intern("point")` / `"line"` / `"lineStrip"` / `"tri"` / `"triStrip"` / `"triFan"`
- [x] 2.4 在 `src/core/resources/mesh.hpp` 新增 `StringID getRenderSignature(StringID /*pass*/) const`，返回 `compose(TypeTag::MeshRender, {vertexBuffer->getLayout().getRenderSignature(), topologySignature(indexBuffer->getTopology())})`
- [x] 2.5 在 `src/core/resources/skeleton.hpp` 新增 `StringID getRenderSignature() const`，返回 `GlobalStringTable::get().Intern("Skn1")`
- [x] 2.6 在 `src/core/resources/shader.hpp` 为 `ShaderProgramSet` 新增 `StringID getRenderSignature() const`，实现：收集 `variants` 中 `enabled` 的 `macroName`，`std::sort` 后逐条 `Intern`，连同 `Intern(shaderName)` 组装成 `parts`，调用 `compose(TypeTag::ShaderProgram, parts)`
- [x] 2.7 在 `src/core/resources/material.hpp` 为 `RenderState` 新增 `StringID getRenderSignature() const`，compose 7 个字段的 `toString(...)` 叶子进 `TypeTag::RenderState`
- [x] 2.8 为 `RenderPassEntry` 新增 `StringID getRenderSignature() const`，返回 `compose(TypeTag::RenderPassEntry, {shaderSet.getRenderSignature(), renderState.getRenderSignature()})`

## 3. MaterialTemplate pass 键迁移

- [x] 3.1 `m_passes` 从 `unordered_map<std::string, RenderPassEntry>` 迁移到 `unordered_map<StringID, RenderPassEntry, StringID::Hash>`
- [x] 3.2 `setPass(const std::string &, RenderPassEntry)` 替换为 `setPass(StringID, RenderPassEntry)`
- [x] 3.3 `getEntry(const std::string &)` 替换为 `getEntry(StringID)`
- [x] 3.4 新增 `StringID getRenderPassSignature(StringID pass) const`，查 `m_passes`；命中返回 `entry.getRenderSignature()`，未命中返回 `StringID{}`
- [x] 3.5 更新 `src/core/resources/material.cpp::firstEntry` 及任何其他迭代 `m_passes` 的点，保证不假设 `string` key

## 4. IMaterial / IRenderable 接口扩展

- [x] 4.1 在 `IMaterial` 添加 `virtual StringID getRenderSignature(StringID pass) const = 0`
- [x] 4.2 `MaterialInstance` override，返回 `compose(TypeTag::MaterialRender, {m_template->getRenderPassSignature(pass)})`
- [x] 4.3 在 `IRenderable` 添加 `virtual StringID getRenderSignature(StringID pass) const = 0`
- [x] 4.4 `RenderableSubMesh` override：`meshSig = mesh->getRenderSignature(pass)`; `skelSig = skeleton.has_value() ? skeleton.value()->getRenderSignature() : StringID{}`; 返回 `compose(TypeTag::ObjectRender, {meshSig, skelSig})`

## 5. PipelineKey 重写

- [x] 5.1 `pipeline_key.hpp`：`build(ShaderProgramSet, Mesh, RenderState, SkeletonSharedPtr)` 签名替换为 `static PipelineKey build(StringID objectSig, StringID materialSig)`
- [x] 5.2 删除 `pipeline_key.hpp` 对 `mesh.hpp` / `skeleton.hpp` / `material.hpp` 的依赖 include，只保留 `string_table.hpp`
- [x] 5.3 `pipeline_key.cpp`：新 `build` 实现为一行 `compose(TypeTag::PipelineKey, {objectSig, materialSig})`
- [x] 5.4 删除 `pipeline_key.cpp` 匿名 namespace 下的 `variantSegment()`；移除 `<iomanip>`、`<sstream>` 相关 include（如果不再使用）

## 6. Scene::buildRenderingItem(pass)

- [x] 6.1 `scene.hpp`：`RenderingItem` 新增 `StringID pass` 字段
- [x] 6.2 `scene.hpp`：`Scene::buildRenderingItem` 签名改为 `RenderingItem buildRenderingItem(StringID pass)`
- [x] 6.3 `scene.cpp`：实现填 `item.pass = pass`；用 `sub->getRenderSignature(pass)` 与 `sub->material->getRenderSignature(pass)` 构造 `item.pipelineKey`
- [x] 6.4 删除 `scene.cpp` 对旧 `PipelineKey::build(shaderSet, mesh, state, skel)` 的调用及其相关 include

## 7. 调用点迁移

- [x] 7.1 `src/infra/loaders/blinnphong_material_loader.cpp:60` — `tmpl->setPass("Forward", ...)` 改为 `tmpl->setPass(Pass_Forward, ...)`，include `core/scene/pass.hpp`
- [x] 7.2 `src/test/integration/test_material_instance.cpp:73` — 同上
- [x] 7.3 `src/backend/vulkan/vk_renderer.cpp:117` — `scene->buildRenderingItem()` 改为 `scene->buildRenderingItem(Pass_Forward)`，include `core/scene/pass.hpp`
- [x] 7.4 `src/test/integration/test_vulkan_pipeline.cpp:48` — 同上
- [x] 7.5 `src/test/integration/test_vulkan_resource_manager.cpp:66` — 同上
- [x] 7.6 `src/test/integration/test_vulkan_command_buffer.cpp:110` — 同上
- [x] 7.7 `grep -rn 'buildRenderingItem\|PipelineKey::build\|setPass(\"'` 扫一遍确认无遗漏调用点

## 8. 删除废弃 API

- [x] 8.1 `mesh.hpp`：删 `Mesh::getPipelineHash()`（`getLayoutHash()` 保留）
- [x] 8.2 `skeleton.hpp`：删 `Skeleton::getPipelineHash()` 与 `kSkeletonPipelineHashTag`
- [x] 8.3 `material.hpp`：删 `RenderState::getPipelineHash()`、`RenderPassEntry::getPipelineHash()`
- [x] 8.4 `shader.hpp`：删 `ShaderProgramSet::getPipelineHash()`（`getHash()` 保留）
- [x] 8.5 `grep -rn 'getPipelineHash'` 确认无残留引用

## 9. 集成测试

- [x] 9.1 新建 `src/test/integration/test_pipeline_identity.cpp`，`main()`-based（与 `test_string_table.cpp` 同风格）
- [x] 9.2 在 `src/test/CMakeLists.txt` 的 `TEST_INTEGRATION_EXE_LIST` 加入 `test_pipeline_identity`
- [x] 9.3 用例 1：相同 layout + topology + shader + renderState 的两个 `RenderableSubMesh` → 同一 `PipelineKey.id`
- [x] 9.4 用例 2：改一个 variant → 不同 `PipelineKey.id`
- [x] 9.5 用例 3：改 topology (`TriangleList` → `LineList`) → 不同 `PipelineKey.id`
- [x] 9.6 用例 4：加 skeleton → 不同 `PipelineKey.id`
- [x] 9.7 用例 5：同一 template + mesh 配置两套 pass entry（Forward vs Shadow），分别调 `scene->buildRenderingItem(Pass_Forward/Pass_Shadow)` → 两个 `PipelineKey.id` 不同
- [x] 9.8 用例 6：`GlobalStringTable::get().toDebugString(pk.id)` 以 `"PipelineKey("` 开头且包含 `"ObjectRender("`、`"MaterialRender("`（烟雾断言）
- [x] 9.9 `cmake --build ./build --target test_pipeline_identity` → `./build/src/test/test_pipeline_identity`

## 10. 归档 banner

- [x] 10.1 `docs/requirements/finished/001-skeleton-to-resources.md` 顶部加 `> **Superseded by REQ-007** ...` 一行 blockquote，不改正文
- [x] 10.2 `docs/requirements/finished/002-pipeline-key.md` 同上

## 11. 回归与 clang-format

- [x] 11.1 `clang-format -i` 所有本次修改的 `.hpp` / `.cpp` 文件
- [x] 11.2 `cmake --build ./build` 全量 build，要求 0 warning、0 error
- [x] 11.3 `./build/src/test/test_string_table`、`./build/src/test/test_material_instance`、`./build/src/test/test_pipeline_identity` 全部通过
- [x] 11.4 `./build/src/test/test_render_triangle`（启动即退出型；只要它编译过且 main 不崩就算 OK —— 真正的渲染验证靠 `vk_renderer.cpp` 触发的 `buildRenderingItem(Pass_Forward)`）
- [x] 11.5 `openspec validate interning-pipeline-identity --strict`，解决任何告警

## 12. 文档状态更新

- [x] 12.1 `docs/requirements/007-interning-pipeline-identity.md` 的"实施状态"更新为已完成（日期、change 名、测试覆盖说明）
- [x] 12.2 `docs/requirements/006-string-table-compose.md` 的"实施状态"补充一句"REQ-007 落地时已作为 `compose`/`Intern`/`TypeTag` 的首个消费者验证"
- [x] 12.3 把 REQ-006、REQ-007 一起从 `docs/requirements/` 移动到 `docs/requirements/finished/`（按照用户的惯例等下游需求消费后再归档，这次 007 是 006 的下游，两者同时归档）
