# REQ-073-d: RenderPath Shader URI Migration And Terminology Hard Cut

> 2026-06-13 拆分：本 REQ 从原 `REQ-073-c` 拆出，负责把默认 shader URI 和术语从旧 `techniques/...` 硬切到 `render_paths/...`。`REQ-073-c` 先建立 material source shader variant / final reflection / pipeline identity，本 REQ 在这个基础上迁移默认 asset、resolver、测试和 rejection diagnostics。
>
> 2026-06-14 范围修正：本 REQ 同时承接已经由 `REQ-073-a/b/c` 建好的 realtime material source 正向路径硬切。旧 runtime material pass 注入、旧 resolver alias、旧 Forward/Deferred shader tree 和旧正向测试不再保留；更宽的 draw/descriptor fallback 清理仍由 `REQ-073-j` 处理。

## 背景

当前仓库已经有 `assets/render_paths/*.render-path.yaml`，但 shader 源和部分测试、工具、默认路径仍引用 `assets/shaders/glsl/techniques/...`。这会造成两个问题：

- `techniques` 是旧 material-local 术语，容易让实现继续把 pass shader 当成材质内部 technique。
- 后续 realtime hard cut 如果同时承担 shader source variant、URI 迁移和旧 fallback 删除，会很难判断失败原因。

因此，我们先在 `REQ-073-c` 稳定 final shader variant，再在本 REQ 单独完成 URI / 术语硬切。完成后，后续 `REQ-073-e` 只面对已经稳定的 shader/pipeline identity 和 RenderPath 术语。

## 目标

1. 默认 RenderPathGraph / shader asset 使用 `render_paths/...` URI。
2. shader resolver 支持 `render_paths/...` 到 `assets/shaders/glsl/render_paths/...` 的解析。
3. 默认 realtime shader 源迁移到 `assets/shaders/glsl/render_paths/Forward/` 和 `assets/shaders/glsl/render_paths/Deferred/`。
4. 旧 `techniques/...` 不再作为 realtime 正向代码、asset、shader source tree 或普通测试 fixture 出现；rejection/audit 测试只能构造旧 token 并断言失败。
5. 文档、asset、parser diagnostic 和测试名使用 RenderPath / pass shader 术语，不再把 pass shader 称为 material-local technique。
6. 默认场景资源加载完成后，显式进入 pipeline preparation 阶段；当前实现预构建 pipeline，后续 cache load 也必须放在这个阶段并验证 identity。

## 承接自 073-a / 073-b / 073-c 的未完成项

| 来源 | 本 REQ 承接内容 | 为什么属于 073-d |
|---|---|---|
| `REQ-073-a` 未完成项 | `techniques/...` 到 `render_paths/...` 的默认 URI 迁移 | URI 迁移是默认 asset / resolver / 测试硬切，不能放在材质合同层 |
| `REQ-073-b` 未完成项 | shader source tree、runtime path 和 positive tests 不再依赖 `techniques/...` | 073-b 只修复 build/runtime source 同步，未迁移默认 URI |
| `REQ-073-c` 后续 | source-variant shader 的 base URI 使用 RenderPath 术语 | final shader identity 稳定后，URI 迁移才能避免留下旧 resolver fallback |

## 非目标

- 不实现 material source shader variant；由 `REQ-073-c` 处理。
- 不要求 raster work item 全部进入 indirect batch；由 `REQ-073-e` 处理。
- 不删除更宽的 realtime 旧 draw/descriptor fallback；由 `REQ-073-j` 处理。但 `REQ-073-a/b/c` 正向路径相关的旧 runtime pass 注入、旧 shader URI fallback 和旧正向测试由本 REQ 删除。
- 不处理 OfflineRT 默认配置入口硬切；由 `REQ-074-h` / `REQ-074-i` 处理。
- 不实现 package、BC7 或 pipeline cache blob。

## 需求

### R1: Shader Source Tree Migration

默认 realtime shader source SHALL 迁移到 `assets/shaders/glsl/render_paths/...`。

最低迁移：

| 旧路径 | 新路径 |
|---|---|
| `assets/shaders/glsl/techniques/Forward/*` | `assets/shaders/glsl/render_paths/Forward/*` |
| `assets/shaders/glsl/techniques/Deferred/*` | `assets/shaders/glsl/render_paths/Deferred/*` |

规则：

- 迁移后的正向 build target 不得继续从旧路径编译 default realtime shader。
- 如果旧路径暂时作为 negative fixture 保留，目录、文件名或测试必须明确标注 legacy rejection。
- 不允许复制两份都作为正向成功路径。

### R2: RenderPathGraph Asset URI Migration

默认 RenderPathGraph asset SHALL 使用 `render_paths/...` shader URI。

要求：

- `assets/render_paths/forward_main.render-path.yaml` 等默认 asset 的 shader 字段使用 `render_paths/...`。
- ordinary positive tests 使用 `render_paths/...`。
- migrated validation profile 下，`techniques/...` URI 失败并输出迁移 diagnostic。

### R3: Resolver Hard Cut

shader resolver SHALL 支持 `render_paths/...`，并禁止把 `techniques/...` 作为默认 fallback。

要求：

- `render_paths/Forward/pbr` 解析到 `assets/shaders/glsl/render_paths/Forward/pbr.*`。
- resolver diagnostic 必须区分 missing shader、legacy URI、unsupported stage。
- 旧 `techniques/...` 只能通过 named legacy rejection path 触发失败，不得静默重定向。

### R4: RenderPath Terminology Boundary

文档、asset、parser diagnostic 和测试名 SHALL 使用 RenderPath / pass shader 术语。

允许保留：

- 历史需求文档中的旧术语。
- legacy rejection / negative audit fixture。
- 明确标注为旧路径的兼容测试。

禁止：

- default asset 中继续出现 material-local technique / defaultTechnique。
- ordinary positive test 继续把 pass shader 称为 technique。
- `src/test` 普通正向 fixture 继续使用 `techniques/...` 证明 current path。

### R5: URI Migration Diagnostics

migrated validation profile SHALL 输出可审计 diagnostics：

- rejected legacy shader URI。
- expected `render_paths/...` URI。
- RenderPathGraph asset URI。
- pass id；结构化 pass stage diagnostic 由 `REQ-074-h` 的 stage-specific RenderPass contract 承接。
- resolver search path。

无法解析或遇到 legacy URI 时必须停止渲染准备，不能隐藏为 fallback shader。

### R6: Runtime Hard Cut For Completed Material Source Path

默认 realtime runtime SHALL NOT 在场景加载后补造旧 Forward PBR material pass。

要求：

- 删除 old Forward PBR material-pass injection helper。
- material-source draw 必须来自 RenderPathGraph + material source variant resolution。
- strict material-source validation 要求 final shader reflection、`MaterialTypeVariant`、`RenderPathNodeSignature`、`PipelineKey` 和 shader 真实声明的 typed scene indices。
- `SceneMaterialRefs` 只在 shader 反射声明该 binding 时要求 typed source ref index，不能把“没有 SceneMaterials”当成旧 fallback 推断。

### R7: Explicit Pipeline Preparation

加载场景资源、解析 RenderPathGraph、完成 material source variants、构建 FrameGraph、同步 upload plan 后，renderer SHALL 明确调用一次 pipeline preparation。

要求：

- 当前阶段收集 `FrameGraph::collectAllPipelineBuildDescs()` 并调用 `resourceManager().preloadPipelines(...)`。
- 后续 package/cache load 进入同一 preparation 阶段；cache hit 仍需验证 material type variant、RenderPathNode signature、shader/reflection identity、render state、rendering mode 和 attachment contract。

## 测试

### T1: Default Asset URI Migration

解析默认 Forward / Deferred RenderPathGraph asset，断言 shader URI 使用 `render_paths/...`。

### T2: Resolver New Path

构造 `render_paths/Forward/pbr` 和 `render_paths/Deferred/pbr_gbuffer`，断言 resolver 找到新目录下的 shader source / SPIR-V 输出。

### T3: Legacy URI Rejection

构造 `techniques/...` shader URI fixture，断言 migrated validation profile 拒绝它，并输出 expected `render_paths/...` diagnostic。

### T4: Positive Test Audit

rg/audit ordinary positive tests、default assets 和 runtime default path，断言 `techniques/...` 只出现在 legacy negative test、历史文档或 rejection diagnostic。

### T5: Shader Build Tree

运行 shader build target，断言 default realtime shader 从 `render_paths/...` 编译；旧 `techniques/...` 不再是 default build 成功条件。

### T6: Runtime And Pipeline Preparation Audit

审计 renderer runtime：

- 不再包含旧 Forward material pass 注入 helper。
- `initScene` 在 FrameGraph compile、sampled resource attachment、upload plan sync、garbage collection 后调用 pipeline preparation。
- pipeline preparation 收集 FrameGraph pipeline build desc 并 preload pipelines。

### T7: Strict Material Source Validation

构造 migrated raster draw：

- 缺失 final material source identity 时 strict validation 失败。
- shader 声明 `SceneMaterialRefs` 但缺失 typed source ref index 时失败。
- shader 未声明 `SceneMaterialRefs` 时不再靠 absence of `SceneMaterials` 推断旧 fallback。

## 修改范围

- `assets/render_paths/*.render-path.yaml`
- `assets/shaders/glsl/render_paths/Forward/`
- `assets/shaders/glsl/render_paths/Deferred/`
- `assets/shaders/CMakeLists.txt`
- shader resolver / runtime path helpers
- render resource parser tests and legacy URI audits
- default realtime runtime path references

## 边界与约束

- 不让 `techniques/...` 成为 resolver fallback。
- 不保留两套正向 shader source tree。
- 不把 OfflineRT 旧 provider / config hard cut 塞入本 REQ；OfflineRT 默认入口由 `REQ-074-h` / `REQ-074-i` 处理。
- 不在本 REQ 删除 realtime material fallback；这里仅完成 URI 和术语硬切。

## 依赖

- `REQ-073-c`: Material source shader variant boundary。

## 后续工作

- `REQ-073-e`: Indirect material batching and diagnostics。
- `REQ-073-j`: Realtime material path hard cut and smoke。
- `REQ-074-h`: OfflineRT RenderPathGraph compute path。

## 实施状态

已实施（2026-06-14）。

主要实现：

- 新增共享 RenderPath shader URI resolver，`render_paths/...` 为 realtime 正向 namespace，`techniques/...` 失败并输出迁移 diagnostic。
- Forward / Deferred realtime shader source tree 迁移到 `assets/shaders/glsl/render_paths/...`，旧 `assets/shaders/glsl/techniques/Forward` 和 `Deferred` 删除；`OfflineRT` 保持给 `REQ-074-h/i`。
- 默认 RenderPathGraph asset、shader build、parser/source-variant tests 迁移到 `render_paths/...`。
- 删除旧 runtime Forward PBR material pass 注入。
- renderer 增加 `preparePipelinesForLoadedScene()`，在加载场景资源和 FrameGraph/upload 同步完成后显式进行 pipeline preparation。
- strict material-source validation 增加 final shader / material type variant / RenderPath node signature / pipeline key 检查，并删除旧 `SceneMaterialRefs` fallback 推断。
- 删除旧 material-local technique 负例和 071g 中对 `defaultTechnique` 的白名单；073-d 审计测试不再在 `src`/`assets` 保留旧实时路径/旧字段字面量。

范围修正：

- R5 中原要求的结构化 pass stage diagnostic 暂不作为 073-d 完成条件。当前 resolver 接口只接收 graph URI、pass id 和 shader URI；RenderPass stage 仍停留在 parser / RenderPassNode 层，resolver 无法可靠输出 stage。该项转交 `REQ-074-h`，随 stage-specific RenderPass contract 一起实现 resolver API 的 stage 传递和负向诊断测试。

实现提交：

- `a4542fc7` Add 073-d render path hard cut tests
- `ef001e41` Add RenderPath shader URI resolver
- `e22dd451` Migrate realtime shaders to render_paths
- `1fbcd1c7` Migrate RenderPath graph tests to render_paths
- `bf261079` Remove legacy realtime material pass injection
- `82153218` Name post-load pipeline preparation phase
- `be25bc21` Harden material source preparation validation
- `29e3cde0` Delete legacy render path audit fixtures

验证记录：

- `ninja -C build test_073d_render_path_hard_cut test_bindless_validation_contract test_material_v2_resource_dependencies test_default_material_asset_audit test_render_resource_parsers test_render_path_graph_pass_contract test_material_source_variant_pipeline test_shader_compiler test_vulkan_shader test_pipeline_build_info test_pipeline_cache test_gltf_scene_asset_loader` 通过。
- focused tests 通过：`test_073d_render_path_hard_cut`、`test_bindless_validation_contract`、`test_material_v2_resource_dependencies`、`test_default_material_asset_audit`、`test_render_resource_parsers`、`test_render_path_graph_pass_contract`、`test_material_source_variant_pipeline`、`test_shader_compiler`、`test_pipeline_build_info`、`test_gltf_scene_asset_loader`。
- `xvfb-run -a ./build/src/test/test_pipeline_cache` 通过。
- `xvfb-run -a ./build/src/test/test_vulkan_shader` 通过。
- `rg -n "defaultTechnique|techniques/Forward|techniques/Deferred|techniques:" src assets` 无输出。
- `find assets/shaders/glsl/techniques -maxdepth 1 -type d -print` 只剩 `assets/shaders/glsl/techniques` 和 `assets/shaders/glsl/techniques/OfflineRT`。
- `ctest --output-on-failure -L auto -LE requires_video_device` 重新构建后 69 项中 67 项通过；剩余失败为 `test_071g_legacy_boundary_removal`（071-g 全局旧材质 token：`MaterialUBO`、glTF factor 字段）和 `test_offline_gpu_scene`（OfflineRT shader/material-source 后续范围）。

## 归档记录

2026-06-14 复核通过。073-d 已完成 realtime `render_paths/...` URI hard cut、旧 Forward/Deferred shader tree 删除、旧 runtime Forward PBR material pass 注入删除、strict material-source validation 和加载场景资源完成后的显式 pipeline preparation。R5 中结构化 pass stage diagnostic 已范围修正并转交 `REQ-074-h`。

本次归档前验证：

- `ninja -C build test_material_source_contract test_material_v2_parser test_material_v2_resource_dependencies test_scene_resource_upload_view_v2 test_bindless_indirect_contract test_scene_resource_table test_material_source_variant_pipeline test_pipeline_identity test_pipeline_build_info test_shader_compiler test_073d_render_path_hard_cut`
- `ctest --test-dir build --output-on-failure -R 'test_(material_source_contract|material_v2_parser|material_v2_resource_dependencies|scene_resource_upload_view_v2|bindless_indirect_contract|scene_resource_table|material_source_variant_pipeline|pipeline_identity|pipeline_build_info|shader_compiler|073d_render_path_hard_cut)$'`
- `rg -n "defaultTechnique|techniques/Forward|techniques/Deferred|techniques:" src assets` 无输出。
- `find assets/shaders/glsl/techniques -maxdepth 1 -type d -print` 只剩 OfflineRT legacy 目录，归属 `REQ-074-h` / `REQ-074-i`。
