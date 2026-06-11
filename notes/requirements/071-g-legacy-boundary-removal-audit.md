# REQ-071-g: Legacy Boundary Removal Audit

> 2026-06-11 新增：本 REQ 是 `REQ-071` 需求族的硬切清理步。目标不是继续标记、隔离或兼容旧路径，而是删除 `REQ-071-a` 到 `REQ-071-c` 迁移后仍能绕回旧材质、旧 pass 构建、旧 material tag、旧 `MaterialUBO` 和非 bindless fallback 的入口。

## 背景

`REQ-071-a` 到 `REQ-071-c` 已经把默认材质资产、RenderPathGraph/RenderFeature 合同、FrameGraph source/target DAG、SceneResourceTable typed upload view 和 strict bindless validation 推进了一轮。当前代码中已经出现新的默认资产：

- `assets/materials/pbr.material`
- `assets/materials/pbr_gold.material`
- `assets/render_paths/forward_main.render-path.yaml`
- `assets/effects/tone_mapping.render-feature.yaml`

同时，代码里仍存在多个旧系统入口：

| 遗留入口 | 当前问题 |
|---|---|
| 旧 material loader / material-local technique | `GenericMaterialLoader` 仍保留 `defaultTechnique` / `techniques` / root `parameters` / root `resources` 解析路径 |
| 内建 pass graph / pass enum 构建 | `VulkanRealtimeRenderer` 仍有 `makeDefaultForwardRenderPathGraph()`，且 Deferred、Bloom、PostProcess、DebugOverlay 仍由代码手写 `FramePass` |
| `materialTag` 切材质 | scene profile、offline loader、editor session 和 PBRT converter 仍能通过 `materialTag` 在同一 object 上切换材质 |
| `MaterialUBO` / 旧 PBR shader truth | Forward/Deferred PBR shader 仍声明 `MaterialUBO`，字段仍是 `baseColorFactor`、`metallicFactor`、`roughnessFactor` |
| 非 bindless fallback | `BindlessSubmissionDecisionKind::LegacyPerItem` 仍存在，renderer 在非 strict 情况下仍能逐 item submit |
| per-draw push constant data | `RenderWorkItem::raster.drawData`、`PerDrawData`、command buffer push constant 路径仍是默认 render work 模型的一部分 |

这些入口会让后续 `REQ-071-d` 的 GPUResourceTable、bindless descriptor table 和 indirect draw 工作继续被旧系统绕开。`071-g` 的作用是先把边界切干净，再继续做 bindless/indirect 的真实实现。

## 目标

1. 默认 runtime / validation / realtime / offline 路径只能使用 Material v2 PBRT envelope、SceneResourceTable typed handles、RenderPathGraph DAG 和 bindless/indirect-ready render work。
2. 删除旧 material-local technique loader，不再让 `.material` 表达 shader/pass/renderState/variant。
3. 删除 renderer 内建 pass graph 和 pass enum 默认构建，不再用代码推导 pass 依赖。
4. 删除 `materialTag` 材质切换模型，RenderPath 差异只能由 RenderPathGraph、RenderClass、BSDF filter 和具体 `MaterialInstance` identity 表达。
5. 删除 production shader 和 runtime material upload 中的 `MaterialUBO` / 旧 PBR 参数真相。
6. 删除非 bindless fallback 和 per-draw push constant render path；不满足 bindless/indirect 条件时 fail-fast。
7. 增加静态和运行时边界测试，证明默认路径不会触达旧系统。

## 非目标

- 不实现新的 BSDF 物理精确求值。
- 不实现完整 GPUResourceTable / descriptor indexing / indirect draw backend；这些属于 `REQ-071-d`。
- 不实现 scene package；这属于 `REQ-071-e`。
- 不保留 legacy/debug/custom material 兼容路径。确实仍需要的功能必须迁移到新合同，而不是挂开关保留旧入口。

## 需求

### R1: 删除旧 material loader 合同

默认 material loader SHALL 只接受 `schema: lxe.material.v2`。

必须删除：

- `GenericMaterialLoader` 对非 v2 `.material` 的生产路径。
- `.material` 中的 `shader`、`defaultTechnique`、`techniques`、`passes`、`renderState`、`variants`、`variantRules` 支持。
- `.material` root `parameters` / root `resources` 支持。
- 旧 PBR 字段 `baseColorFactor`、`metallicFactor`、`roughnessFactor`、`ao` 作为 runtime material truth 的所有 loader 逻辑。
- 通过 `albedoMap`、`normalMap`、`metallicRoughnessMap`、`aoMap`、`emissiveMap` 作为 material root resource truth 的路径。

解析旧字段时不需要专门兼容或迁移。v2 parser 只需按 schema fail-fast：未知 root field 或错误 envelope shape 即 fatal。

### R2: 删除 renderer 内建 RenderPathGraph

renderer SHALL 从资产或 SceneResourceTable 中取得 active RenderPathGraph，不能在 backend 代码中内建默认 graph。

必须删除：

- `VulkanRealtimeRenderer` 中的 `makeDefaultForwardRenderPathGraph()`。
- `VulkanRealtimeRenderer::initScene()` 中手写 `FramePass{Pass_Forward ...}`、`FramePass{Pass_Deferred ...}`、`FramePass{Pass_PostProcess ...}`、`FramePass{Pass_DebugOverlay ...}` 的默认路径。
- Deferred、Bloom、PostProcess、DebugOverlay 通过代码直接插入 FrameGraph 的默认行为。
- 按 `deferredMode` 隐式推导 GBuffer / Forward / PostProcess 结构。
- 按 pass enum 名称重建 queue、camera resource、fullscreen item 或 target 的默认路径。

RenderPathGraph 中没有声明的 pass 不应执行。graph 中缺 shader、source、target、renderState 或 producer 时，FrameGraph / graph validation SHALL fail-fast。

### R3: 删除旧 pass enum 作为默认渲染身份

`Pass_Forward`、`Pass_Deferred`、`Pass_PostProcess`、`Pass_DebugOverlay` 等 hardcoded pass `StringID` SHALL 不再作为默认生产渲染流程的 source of truth。

要求：

- render work pass identity 来自 RenderPathGraph pass `id`。
- pass target/source 来自 graph `sources` / `targets`。
- `Scene::getSceneLevelResources(pass, target)` 这类按 pass enum 注入 UBO/light 的旧模型必须迁移到 fixed system ABI / SceneResourceTable upload view。
- light、debug draw、post-process builder 如果仍需要 pass identity，也必须由 graph pass id 绑定，而不是引用 hardcoded enum。

允许在测试 fixture 中构造 `StringID("Forward")` 这类字面量验证 DAG 行为，但 production renderer 不得依赖 `Pass_*` 常量构建默认图。

### R4: 删除 `materialTag` 材质切换模型

默认 scene、profile、editor、offline、converter SHALL 不再支持 `materialTag`。

必须删除：

- scene YAML / render profile / output profile 中的 `materialTag` 读取和写出。
- `Scene::setActiveMaterialTagForRenderables`。
- `MaterialComponent` 中 active tag 和 tagged material map。
- editor session / command 中切换 material tag 的命令与 JSON 字段。
- offline scene loader 按 `materialTag` 查找 `materials[]` entry 的逻辑。
- PBRT converter 输出 `materialTag` profile 或 per-node tagged materials 的逻辑。

完成后：

- 一个 renderable 引用一个具体 `MaterialInstance` handle。
- realtime/offline/Forward/Deferred/OfflineRT 的差异由 RenderPathGraph 选择 pass/shader，由 pass filters 选择 RenderClass / BSDF，不由 tag 切换另一套材质。
- scene 保存时不输出 `materialTag`。

### R5: 删除 `MaterialUBO` 和旧 PBR shader 参数

production shader SHALL 不再声明 `MaterialUBO`。

必须删除或迁移：

- `assets/shaders/glsl/techniques/Forward/pbr.frag` 中的 `MaterialUBO`。
- `assets/shaders/glsl/techniques/Forward/pbr_clearcoat.frag` 中的 `MaterialUBO`。
- `assets/shaders/glsl/techniques/Deferred/pbr_gbuffer.frag` 中的 `MaterialUBO`。
- `assets/shaders/glsl/techniques/Deferred/pbr_clearcoat_gbuffer.frag` 中的 `MaterialUBO`。
- shader 字段 `baseColorFactor`、`metallicFactor`、`roughnessFactor` 作为材质真相。
- `SceneGpuMaterialRecord` / upload view 中从 `MaterialUBO`、`SurfaceParams` 或旧 texture binding 读取 fallback 的逻辑。
- `MaterialInstance` 中面向默认材质系统的 `ParameterBuffer` / `setParameter(binding, member, ...)` 路径。

完成后：

- shader 读取 fixed system ABI 的 object/material indices 和 bindless material storage。
- CPU 侧唯一 material truth 是 PBRT envelope。
- GPU material record 是 envelope 派生缓存，不是第二套可编辑参数模型。

### R6: 删除非 bindless fallback

默认 renderer SHALL 不再从 bindless/indirect path fallback 到逐 item descriptor submit。

必须删除：

- `BindlessSubmissionDecisionKind::LegacyPerItem`。
- `decideBindlessSubmission()` 中返回 legacy per-item 的分支。
- `VulkanRealtimeRenderer::drawPassQueue()` 在 bindless batch 未覆盖时逐 item `cmd.executeWorkItem(item)` 的默认行为。
- `RenderWorkQueue::compileIndirectBatches()` 跳过 `drawData` 后仍允许该 pass 渲染的行为。
- 默认 render path 中的 per-material descriptor list fallback。

完成后：

- queue 为空可以 no-op。
- queue 非空且无法形成完整 bindless indirect batch时，renderer 必须 fatal 或输出 unsupported diagnostic。
- strict validation 不再是可选安全网，而是默认 migrated path 行为。

### R7: 删除 per-draw push constant render path

默认 render work SHALL 不再依赖 `RenderWorkItem::raster.drawData`。

必须删除或迁移：

- `SceneNode` / render queue build 中把 object transform 写入 `PerDrawData` 的路径。
- `RenderUploadPlan` 对 push constants 的收集。
- Vulkan command buffer 根据 `raster.drawData` 写 push constants。
- `extractModelMatrix(item.raster.drawData)` 这类从 per-draw data 回读 object transform 的 backend 逻辑。

完成后：

- object transform、material index、mesh descriptor index、draw data 都来自 SceneResourceTable upload view / fixed system ABI / draw record。
- draw item 只携带 handle 或 typed index，不携带 per-draw CPU byte blob。

### R8: 默认 validation assets 迁移掉 `materialTag`

当前仍包含 `materialTag` 的 validation scene SHALL 迁移到新模型：

- `assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml`
- `data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml`

PBRT converter SHALL 停止生成 `materialTag`。如果同一 PBRT shape 需要不同 render path 行为，必须通过同一个 Material v2 envelope、RenderClass、BSDF type 和 RenderPathGraph pass filters 表达。

### R9: 静态 legacy 边界审计

新增 `test_071g_legacy_boundary_removal`。

该测试 SHALL 扫描以下 production/runtime 路径：

- `src/core`
- `src/infra`
- `src/backend`
- `src/demos/lxe_editor`
- `assets/materials`
- `assets/shaders`
- `assets/scenes`
- `assets/render_paths`
- `assets/effects`
- `data/scenes`
- `src/tools/lxe_pbrt_scene_convert`

禁止出现以下默认路径遗留符号：

```text
defaultTechnique
MaterialUBO
baseColorFactor
metallicFactor
roughnessFactor
materialTag
setActiveMaterialTag
activeMaterialTag
BindlessSubmissionDecisionKind::LegacyPerItem
LegacyPerItem
raster.drawData
PerDrawData
makeDefaultForwardRenderPathGraph
```

测试允许范围只包括：

- `notes/requirements/`、`notes/requirements/finished/`、`docs/` 等文档。
- `src/test/integration/` 中专门用于验证旧输入被拒绝的负向 fixture。
- third-party / generated / external 目录。

如果 production code 需要错误消息表达“旧字段已删除”，错误消息不得保留旧字段常量；应使用 schema/field-path 级别的 unknown-field diagnostic。

### R10: 运行时 default path 审计

新增 runtime audit 测试，构建最小 Material v2 scene 并走默认 validation path。

测试 SHALL 证明：

- active RenderPathGraph 来自 asset/resource，不是 backend 内建函数。
- FrameGraph pass 列表来自 RenderPathGraph pass `id`。
- scene/material load 没有调用 legacy material-local technique loader。
- render work item 不含 `raster.drawData`。
- render work item 不含 material-local `MaterialUBO` descriptor。
- bindless validation 对默认 migrated pass 默认启用。
- 不能形成完整 indirect batch 时测试失败，而不是逐 item submit。
- scene/profile 中没有 `materialTag`。

## 测试

### T1: Static Legacy Boundary Audit

运行 `test_071g_legacy_boundary_removal`：

- production/runtime 路径不包含 R9 禁止符号。
- 负向 fixture 的旧字段只允许存在于测试目录。
- 默认 assets 不包含旧 material fields 或 materialTag。

### T2: Default RenderPathGraph Asset Audit

验证：

- `assets/render_paths/forward_main.render-path.yaml` 是默认 Forward graph。
- renderer 不再包含 `makeDefaultForwardRenderPathGraph()`。
- 修改 graph 中 pass 顺序后，FrameGraph compile 仍按 source/target DAG 排序。
- 删除 graph shader/source/target/renderState 任一字段会 fail-fast。

### T3: No MaterialTag Scene/Profile Audit

验证：

- scene document load/save 不再支持 `materialTag`。
- PBRT converter 输出不包含 `materialTag`。
- offline loader 不再有按 tag 选择材质的分支。
- BMW/helmet validation scene 不包含 `materialTag`。

### T4: No MaterialUBO Shader Audit

验证：

- production shader reflection 中没有 `MaterialUBO` binding。
- Forward/Deferred PBR shader 不声明旧 PBR字段。
- material upload view 只从 PBRT envelope 生成 material record。

### T5: No Bindless Fallback Runtime Audit

构造不能完整 indirect batching 的默认 validation queue：

- renderer 返回 fatal/unsupported diagnostic。
- 不执行逐 item descriptor submit。
- `BindlessSubmissionDecisionKind::LegacyPerItem` 不存在。

### T6: No PerDrawData Default Render Work

构造一个默认 Material v2 object：

- render queue item 不含 `raster.drawData`。
- upload plan 不收集 push constants。
- command buffer 不从 render item 写 per-draw push constants。

## 修改范围

- `src/infra/material_loader/`：删除旧 material-local technique loader 路径。
- `src/infra/resource_parsers/`：保留 Material v2 / RenderPathGraph / RenderFeature parser，删除 legacy field bridge。
- `src/backend/vulkan/vulkan_realtime_renderer.cpp`：删除内建 default graph、hardcoded pass insertion、per-item fallback。
- `src/backend/vulkan/details/commands/`：删除默认 per-draw push constant执行路径。
- `src/core/frame_graph/`：删除 `LegacyPerItem` decision、per-draw fallback、pass enum 默认依赖。
- `src/core/scene/`：删除 material tag component model、PerDrawData 默认路径、MaterialUBO upload fallback。
- `src/core/asset/`：删除 `MaterialInstance` 默认 ParameterBuffer material truth；Material v2 只保留 envelope 和 typed resource handles。
- `src/infra/scene_io/`、`src/infra/offline/`、`src/demos/lxe_editor/`：删除 materialTag profile/editor/offline path。
- `src/tools/lxe_pbrt_scene_convert/`：停止生成 materialTag。
- `assets/shaders/`：删除 MaterialUBO 和旧 PBR字段。
- `assets/scenes/`、`data/scenes/`：迁移掉 materialTag。
- `src/test/integration/`：新增静态和运行时边界审计测试。

## 边界与约束

- 不允许新增 `legacy`、`compat`、`allowOldMaterial`、`fallback`、`debugLegacy` 之类开关绕过本 REQ。
- 不允许把旧路径移动到另一个默认可达函数中。
- 不允许用“测试还需要”作为生产代码保留旧字段常量的理由；负向 fixture 可以在测试目录自带旧字符串。
- 不要求一次完成 GPUResourceTable 真实上传，但默认 renderer 不能在 GPUResourceTable 未完整时 fallback 到旧 per-item descriptor submit。
- 如果某个 editor/debug 功能依赖旧系统，本 REQ 内必须迁移或删除该功能入口；不能保留默认可达兼容路径。

## 依赖

- `REQ-071-a`：Material v2 PBRT envelope 是唯一材质参数合同。
- `REQ-071-b`：RenderPathGraph / RenderFeature / FrameGraph source-target DAG 合同。
- `REQ-071-c`：SceneResourceTable typed handles、upload view 和 resource identity。
- `REQ-071-d`：后续把硬切后的 CPU/resource/render work 上传到真实 GPUResourceTable。

## 后续工作

- `REQ-071-d` 在本 REQ 之后实现真实 GPUResourceTable、bindless descriptor table、pipeline cache、upload tasks 和 indirect draw backend。
- `REQ-071-e` 在无旧资源身份绕路后实现 package restore。
- `REQ-071-f` 在无旧 validation path 后做 helmet/BMW direct equivalence。

## 实施状态

已实施（2026-06-11）。

本轮按硬切策略删除默认 production/runtime 路径中的旧 material-local technique、`materialTag`、`MaterialUBO` / 旧 PBR shader 参数、内建默认 RenderPathGraph、`LegacyPerItem` 非 bindless fallback、`PerDrawData` / `raster.drawData` per-draw push constant 路径。默认实时路径现在由 Material v2 PBRT envelope、RenderPathGraph、SceneResourceTable typed upload view、typed draw records 和 bindless/indirect validation 组成。

新增 `test_071g_legacy_boundary_removal` 作为静态边界审计，扫描 R9 production/runtime 路径并禁止遗留符号。Material v2 parser 对旧 root 字段走 schema-level unknown root field 诊断，不在生产代码保留旧字段常量。默认 render work 在 shader 需要 `SceneDraws` / `SceneObjects` 时 fail-fast；可形成 typed draw record 的对象继续把 draw record index 写入 indirect `firstInstance`。

验证记录：

- `./build/src/test/test_071g_legacy_boundary_removal`
- `ctest --test-dir build --output-on-failure -R "test_071g_legacy_boundary_removal|test_shader_compiler|test_material_v2_parser|test_material_v2_resource_dependencies|test_scene_resource_table|test_bindless_indirect_contract|test_bindless_validation_contract|test_vulkan_frame_graph|test_gltf_scene_asset_loader"`
- `ctest --test-dir build --output-on-failure -R "test_pipeline_build_info|test_scene_resource_table|test_command_bus_v2|test_inspector_panel|test_scene_tree_panel|test_lxe_editor_layout|test_lxe_editor_interaction"`

全量 `ctest --test-dir build --output-on-failure -L auto -LE requires_video_device` 需要在 clean build 后运行；执行中发现旧增量 build 目录存在 stale object 导致 release-only 失败，clean rebuild 后相关失败集已转绿。
