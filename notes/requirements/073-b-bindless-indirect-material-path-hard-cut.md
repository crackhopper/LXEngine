# REQ-073-b: Bindless Indirect Material Path Hard Cut

> 2026-06-12 更新：本 REQ 紧跟 `REQ-073-a`，目标是验证 Material v3 source-reflected contract/signature 数据经过 realtime material storage 真实进入 bindless texture/material/object/mesh tables 和 indirect draw 路径，并在验证通过后完成 realtime 旧路径 hard cut、架构 clean 和 smoke gate。它接管原 `REQ-071-d` 中的 bindless / indirect / non-bindless fallback audit 范围，但不处理 package、pipeline cache blob、editor loading UI 或 OfflineRT 配置化入口；OfflineRT 由 `REQ-073-d` / `REQ-073-e` 处理。

## 背景

`REQ-073-a` 扩展 Material v3 PBRT-style 参数合同，并定义必填 `bsdf.source`、source-reflected material contract、MaterialSignature、默认纹理、Material Accessor ABI 和 source shader variant。仅改材质字段还不够；必须证明 Helmet/BMW validation path 使用的是新的 source-local material storage、global bindless tables 和 indirect draw，而不是旧 per-material descriptor、旧 `MaterialUBO` 或 per-item submission fallback。

当前代码已有若干基础：

- `SceneGpuMaterialRecord` 和 `SceneMaterials` SSBO。
- `RenderWorkQueue::compileIndirectBatches()`。
- bindless validation diagnostics。
- RenderPathGraph asset/parser 路径。
- `assets/render_paths/` 已存在 RenderPathGraph assets，但 GLSL shader 目录仍叫 `assets/shaders/glsl/techniques/`，shader URI 也仍使用 `techniques/...`。

但当前代码也仍存在风险：

- shader 仍用 invalid texture index + `hasSceneTexture` 分支，而不是默认纹理统一采样。
- render queue / backend 仍可能在 batch 不完整时回到 per-item submission。
- mesh 仍可能以每对象/per-mesh buffer 形式阻止同 type 材质进入同一 indirect batch。
- 部分旧 `MaterialUBO` / material-local parameter path 仍存在于 editor、asset bridge 或 tests。

## 目标

1. 验证 Material v3 source-reflected material records 上传到对应 source-local material SSBO。
2. 验证 texture / sampler / material / object / draw / mesh resource 通过全局 bindless-ready table 进入 shader。
3. 清理 shader 目录和 shader URI 术语：`techniques` 改为 `render_paths`。
4. 验证 RenderPath filter / drawcall build 按 material source shader variant 展开。
5. 验证 supported raster passes 使用 indirect batch，并按 source-reflected MaterialSignature 合批。
6. 用 BMW M6 验证 drawcall / pipeline / material source 统计正确。
7. 验证 missing bindless / missing material index / missing texture slot fail-fast。
8. 在本 REQ 结束时执行 realtime 架构 clean、旧代码硬切和 Helmet/BMW smoke 测试。

## 需求

### R1: Material V3 Source-reflected Upload View

`SceneResourceTableUploadView` SHALL 导出 `REQ-073-a` 定义的 Material v3 source-reflected material records。

要求：

- material record 包含 factor、texture index、channel selector 和 flags。
- material record 包含或可追踪到 `bsdf.source` / source reflection hash / material signature / shader variant key。
- texture index 来自全局 texture table 或默认纹理 resource。
- material record 不保存路径字符串、backend object pointer 或旧 `MaterialUBO` bytes。
- glTF Helmet 和 BMW PBRT converter 输出如果选择同一 `bsdf.source`，必须走同一 upload record layout。
- 不同 material source 可以使用不同 SSBO record layout；同一个 RenderWorkItem / pipeline 只绑定当前 source variant 对应的 layout。
- draw record 中的 material index 对当前 source storage 是 source-local index，不能混用不同 source 的 material table index。

### R2: Global Bindless Tables

上传路径 SHALL 按资源类型维护全局 table。

最低表：

| 表 | 内容 |
|---|---|
| texture table | baseColor、metallic/roughness、AO、emissive、normal、default textures |
| sampler table | sampler state |
| material table | per-source Material v3 SSBO records |
| object table | transform、mesh/material indices、visibility |
| draw table | object/material/mesh/draw offsets |
| mesh/geometry table | global position/index/attribute stream ranges |

资源身份来自 `SceneResourceTable` canonical URI / typed handle；GPU path 只保存 slot/index。渲染前，当前场景的 material、object、draw、mesh/geometry、texture、sampler 数据 SHALL 已整体上传到 GPU bindless-ready tables。material storage SHALL 按 material source shader variant 分区，保证 shader 看到的 SSBO layout 与编译 variant 一致。

### R3: Indirect Draw Coverage

支持的 Forward / Deferred geometry passes SHALL 使用 indirect batch。

规则：

- validation profile 下，如果 raster work item 无法进入 indirect bindless batch，必须 fail-fast 或输出 unsupported diagnostic。
- 不允许 silently call legacy per-item descriptor submission。
- debug overlay、postprocess 等特殊 pass 如果暂时不支持 indirect，必须明确标记，不参与 material validation pass。

### R3.1: RenderWorkQueue Material Source Batching

RenderWorkQueue SHALL 把同一 pass / target / object signature / material signature 的 raster work 合并为 indirect batch，而不是按 material instance 或 mesh object 拆 drawcall。

要求：

- `RenderWorkItem` 只保存 material index、draw record index、mesh/draw offsets 等索引；材质参数和 object transform 不通过 per-item descriptor 传入。
- 同一 material source 的不同实例通过该 source 的 material SSBO 中不同 source-local material index 访问。
- 同一场景中可合并的模型网格 SHALL 合并到全局 geometry/index buffers，或在 upload view 中表现为同一组可 indirect draw 的 global buffers + per-draw offsets。
- `compileIndirectBatches()` 的 batch 数量应等于该 pass 中唯一 pipeline/global geometry descriptor 组合的数量，而不是材质实例数或 mesh object 数。
- 如果某 mesh 因 vertex layout、topology 或不兼容 buffer layout 无法进入同一 indirect batch，diagnostics 必须说明拆分原因。
- MaterialSignature 来自 `REQ-073-a` 的 source-reflected material signature；material URI、material handle、texture presence、texture id、source workflow 和参数值不能拆 batch。

### R3.2: RenderPath Material Source Shader Variants

RenderPath graph 中一个 geometry pass 的 shader URI SHALL 表示 base render-path shader；实际构建 drawcall 时，材质 `bsdf.source` 的 reflection hash / source signature 决定 shader variant。

要求：

- RenderPath filter 物体时 SHALL 同时考虑 pass/visibility/mesh compatibility 和 material source capability。
- 一个 RenderPath geometry pass 可以展开为多个 material source shader variants，例如 `Forward/pbr + matte.contract.glsl`、`Forward/pbr + metal.contract.glsl`。
- 每个 variant SHALL 编译导入 `REQ-073-a` 定义的 source variant metadata，并 include 对应 material contract source。
- shader reflection、descriptor binding、material SSBO layout、PipelineBuildDesc、PipelineKey 都必须来自 variant 后的最终 shader。
- 不允许在 shader runtime 内用 `bsdf.type` / `bsdf.source` 分支选择公式或解析不同 SSBO layout；pass shader 只能调用 Material Accessor ABI。
- 某 pass 如果不支持某 material source capability，必须在 RenderPath validation 或 draw build 阶段输出 unsupported diagnostic；不能把 object 静默丢掉。

### R4: RenderPath Terminology

本文和实现 SHALL 使用 RenderPath / RenderPathGraph，不再使用 material-local technique 作为主概念。

规则：

- pipeline preload / render work grouping 以 RenderPathGraph pass、shader、target、vertex layout、source-reflected material signature 为结构事实。
- 不从 `.material` 读取 defaultTechnique / techniques。
- GLSL shader 目录 SHALL 从 `assets/shaders/glsl/techniques/` 迁移到 `assets/shaders/glsl/render_paths/`。
- RenderPathGraph YAML 中的 shader URI SHALL 使用 `render_paths/...`，不再使用 `techniques/...`。
- shader compiler、resource parser、runtime resolver、tests、docs 和 fallback path 都要同步新 URI。
- 旧 `assets/shaders/glsl/techniques/` 目录不得作为默认编译输入保留；如保留兼容入口，只能作为 named legacy rejection/audit，不参与 smoke。

### R5: Hard Audit Seeds

新增或强化负向 audit：

- migrated validation path 触达 `MaterialUBO` 参数 truth 时失败。
- migrated validation path 触达 legacy per-material descriptor path 时失败。
- migrated validation path 触达 non-bindless fallback submission 时失败。
- material texture missing 但没有默认 texture slot 时失败。
- material source 支持路径绕过 `REQ-073-a` 的 source-reflected contract / Material Accessor ABI 时失败。
- `RenderWorkQueue` 因 material instance id 或 material URI 拆分 pipeline/batch 时失败。
- shader URI 使用 `techniques/...` 或解析到 `assets/shaders/glsl/techniques/` 时失败。
- shader variant 未导入 source variant metadata 或未 include 对应 material contract source 时失败。

### R6: Architecture Clean And Hard Cut

本 REQ 完成时 SHALL 直接做一次架构 clean 和旧代码硬切。

要求：

- Helmet/BMW/default validation path 不再能触达旧 `MaterialUBO` 作为材质真相。
- 旧 per-material descriptor submission 和 non-bindless per-item draw path 从默认路径删除，或隔离为显式 debug-only 且默认关闭。
- 旧 material-local technique / defaultTechnique / techniques 正向路径删除或改为 legacy rejection。
- 旧 `assets/shaders/glsl/techniques/` 目录、`techniques/...` shader URI、相关 shader resolver fallback 从默认路径删除或改为 legacy rejection。
- ordinary positive tests 不再用旧 material truth、旧 descriptor path 或旧 technique 字段作为可通过 fixture。
- 保留的旧 token 只允许出现在命名清晰的 negative audit、历史需求文档或已标注 legacy diagnostics 中。

### R7: Smoke Gate

hard cut 后 SHALL 立即运行 smoke gate。

最低覆盖：

- Helmet realtime Forward smoke：非全黑，走 Material v3 + bindless + indirect path。
- BMW M6 realtime Forward/geometry smoke：非全黑，输出 material source / pipeline / drawcall stats。
- BMW M6 converter + load smoke：`bsdf.source`、material signature 和 GPU table upload diagnostics 正常。
- smoke 失败时不得继续进入 `REQ-074-a`。

## 测试

### T1: Material Record Upload

构造常量材质、贴图材质、默认纹理材质，断言 upload view 的 source-reflected material records 符合 Material v3 上传合同。不同 source 的 material storage layout 可以不同；同 source 的常量/贴图实例 layout 相同。

### T2: Bindless Slot Dedup

两个材质引用同一 texture，断言 texture table slot 相同；默认 white / flatNormal 只注册一次。

### T3: Indirect Batch Required

运行 supported Forward pass，断言所有 material raster work items 被 indirect batch 覆盖；否则测试失败并输出具体 item。

### T4: Legacy Fallback Audit

rg/audit + behavior test 证明 Helmet/BMW validation path 不读取旧 `MaterialUBO`、不走旧 per-material descriptor、不走 non-bindless per-item fallback。

### T5: BMW M6 Indirect Drawcall Count

使用 BMW M6 converted scene 运行 Forward/geometry validation。

断言：

- scene material/object/draw/mesh/texture/sampler tables 已整体上传到 GPU bindless-ready tables。
- `RenderWorkQueue` 生成的 `RenderWorkItem` 按 material source shader variant / signature 合并，而不是按 material URI 或 instance 分裂。
- `compileIndirectBatches()` 的 batch 数量等于唯一 `{pass, target, objectSignature, materialSignature, globalVertexBuffer, globalIndexBuffer, descriptorSetShape}` 组合数量。
- indirect command 数量等于实际可见 mesh primitive / draw record 数量。
- 如果 BMW M6 中同一 source 有多个材质实例，它们在同一 compatible batch 中通过不同 source-local material index 绘制。
- validation 输出 drawcall count、pipeline count、material source count 和拆分原因列表。

### T6: RenderPath Shader Directory Migration

运行 shader compiler/resource parser tests，断言：

- `assets/shaders/glsl/render_paths/` 是默认 shader path root。
- RenderPathGraph YAML 使用 `render_paths/...` shader URI。
- `techniques/...` shader URI 在 migrated validation profile 下 fail-fast。
- generated SPIR-V 输出路径、runtime resolver 和 tests 不再依赖 `assets/shaders/glsl/techniques/`。

### T7: Material Source Shader Variant Resolution

构造同一 RenderPath geometry pass 下使用 `matte.contract.glsl` 与 `metal.contract.glsl` 的材质。

断言：

- 两者使用同一 base shader URI，但产生不同 material source shader variant key。
- shader compiler 接收到对应 source variant metadata。
- shader reflection 中的 material SSBO layout 来自对应 material contract source。
- `PipelineKey` 不同，material 参数值或 texture presence 改变时 `PipelineKey` 不变。

### T8: Hard Cut Audit

完成 clean 后运行 rg/audit：

- production 默认路径不再正向使用 `MaterialUBO`。
- production 默认路径不再正向使用 material-local technique。
- production 默认路径不再使用 `techniques/...` shader URI 或 `assets/shaders/glsl/techniques/` shader root。
- production 默认路径不再正向使用 per-material descriptor draw fallback。
- ordinary positive tests 不再依赖旧字段通过。

### T9: Helmet And BMW Smoke

运行 hard cut 后 smoke：

- Helmet realtime 输出非全黑。
- BMW M6 realtime Forward/geometry 输出非全黑。
- smoke diagnostics 证明走 bindless/indirect path。
- drawcall stats 与 `T5` 一致。

## 修改范围

- `src/core/scene/scene_resource_table*`
- `src/core/scene/scene_gpu_records.*`
- `src/core/frame_graph/render_queue.*`
- `src/core/frame_graph/render_validation_contract.*`
- `src/core/frame_graph/render_upload_plan.*`
- `src/backend/vulkan/vulkan_realtime_renderer.*`
- Vulkan command submission / resource binding path
- global geometry/mesh upload path
- shader compiler / shader resource resolver / render resource parser
- RenderPathGraph YAML assets under `assets/render_paths/`
- Forward / Deferred PBR shaders
- `assets/shaders/glsl/render_paths/`
- removal or legacy rejection of `assets/shaders/glsl/techniques/`
- Helmet / BMW validation tests
- legacy bridge audits and ordinary positive tests

## 边界与约束

- 不实现 package 文件格式。
- 不实现 Vulkan pipeline cache blob 序列化。
- 不实现 editor loading UI。
- 不整理 OfflineRT config-driven compute path；由 `REQ-073-d` / `REQ-073-e` 处理。
- 不做 texture compression；由 `REQ-074-a` 处理。
- 不恢复 material-local technique。
- 本 REQ 完成后，默认渲染/validation 路径必须已经 hard cut；后续 package 需求只能在 clean canonical state 上继续。

## 依赖

- `REQ-073-a`: Material v3 PBRT type signatures、metallic extension 和 shader common。
- `REQ-072`: 071 closure audit 和 validation 修复。

## 后续工作

- `REQ-073-d`: OfflineRT RenderPathGraph compute path。
- `REQ-073-e`: OfflineRT config hard cut and smoke。
- `REQ-074-a`: BC7 texture compression pipeline。
- `REQ-074-b`: package canonical state readiness audit。

## 实施状态

未实施。
