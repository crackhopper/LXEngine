# REQ-073-c: Material Source Shader Variant And RenderPath Pipeline Identity

> 2026-06-14 重新收束：本 REQ 不只处理 `bsdf.source` 的 shader variant 边界，还负责把 pipeline identity 算法硬切到 MaterialTypeVariant + RenderPathNodeSignature，建立 shader 侧 BSDF 接口 ABI，并让 Helmet/glTF metallic-roughness 材质通过显式转换资产进入新路径 smoke。

## 背景

`REQ-073-a` 已定义 material contract source 和 Material Accessor ABI；`REQ-073-b` 已让 source-reflected material record 进入 bindless-ready upload view。下一步必须让 RenderPath shader 在编译/反射时拿到具体 material type 的 contract source，而不是在 shader runtime 里写 `if materialType` 或 `switch source`。

本轮设计讨论进一步确认了几条边界：

- material `type` 是 shader variant 和 accessor ABI 的语义入口；同一个 `type` 不能映射到多个 source。
- `source` 用于校验、定位和 include，不作为绕过 `type` 的第二套材质分类。
- glTF metallic-roughness 工作流不是 PBRT `uber` 的一个参数补丁；它应当是独立 `standard-pbr` material type。
- pass shader 不应依赖一个固定 `LxMaterialSurface` 摘要来拼 BRDF 公式；每个 material source shader 应实现统一 BSDF 函数 ABI。
- pipeline key 不应继续把 object vertex layout、material pass definition、render target 作为分散轴一股脑组合；目标算法应收敛为 MaterialTypeVariant + RenderPathNodeSignature。
- RenderPathNode 是 pipeline 入口：pass id、shader、render state、source/target、geometry/topology contract、rendering mode 和传统 render pass attachment 约束都属于 node signature。
- Helmet smoke 必须使用转换后显式 Material v2 资产和新 shader variant path，不能为了非黑图走旧 fallback、debug material、空 source 或旧 material bridge。

`techniques/...` 到 `render_paths/...` 的 URI/术语硬切仍由 `REQ-073-d` 处理。本 REQ 可以继续接收当前 asset 中的旧 URI 字符串，但不得新增 legacy resolver/fallback，也不得把旧 URI 隐式映射到新 URI。

## 目标

1. material `type` 唯一决定 material source shader、shader variant include、storage/accessor ABI。
2. 新增 `standard-pbr` material type，承接 glTF metallic-roughness 工作流和 Helmet 材质转换。
3. source variant resolver 在场景资源解析完成后、RenderWorkQueue / PipelineBuildDesc 构建前统一编译并反射 final shader variant。
4. Forward / Deferred / OfflineRT shader 调用统一 BSDF 函数 ABI，不写 material type/source runtime branch。
5. `PipelineKey` 算法硬切为 `MaterialTypeVariant + RenderPathNodeSignature`。
6. RenderPathNode 显式声明 pipeline contract，包括 `rendering: dynamic | traditional`、geometry/topology contract、source/target 和传统 attachment contract。
7. shader build target 明确区分 base source 和 material source variant，`BuildTest` 不能裸编译需要 source variant 的 shader。
8. 材质转换工具产出可用的 Helmet `standard-pbr` `.material` 和 scene YAML。
9. 转换后的 Helmet 场景能通过 realtime smoke，输出非黑图，并证明没有走旧路径或 workaround。

## 承接自 073-a / 073-b 的未完成项

| 来源 | 本 REQ 承接内容 | 为什么属于 073-c |
|---|---|---|
| `REQ-073-a` R5 / T6 / T7 | final shader variant、variant 后 shader reflection、compile/reflection key 和 `PipelineKey` 接入 | 这些事实必须在 RenderPath shader resolver 和 pipeline identity 层形成，不能由材质合同层或 base shader reflection 代替 |
| `REQ-073-a` T7 / T10 | 同 material type / 不同材质参数值不拆 pipeline 的 pipeline-key 验证 | pipeline key 是否只包含 material type variant 和 RenderPathNode signature，需要在最终 shader variant 后验证 |
| `REQ-073-b` 未完成项 | `BuildTest` / `CompileShaders` 对 `pbr.frag`、`pbr_gbuffer.frag`、`offline_pbr_direct_ray.comp` 的裸编译失败 | 这些 shader 已经声明需要 `LX_MATERIAL_CONTRACT_SOURCE`；正确修复是建立 variant-aware shader build target，而不是移除 shader 中的 fail-fast |
| `REQ-073-b` Helmet 验证前置 | Helmet 低分辨率非黑 realtime smoke | 073-b 只证明 upload view/bindless-ready record；本 REQ 要证明转换后的 material type + shader variant + RenderPathNode pipeline identity 能进入实际渲染 |

## 非目标

- 不实现 source-local material storage；已由 `REQ-073-b` 处理。
- 不迁移 `techniques/...` 到 `render_paths/...`；由 `REQ-073-d` 处理。
- 不要求 raster work item 全部进入 indirect batch；由 `REQ-073-e` 处理。
- 不彻底删除 realtime 旧 draw/descriptor fallback；由 `REQ-073-f` 处理。但本 REQ 的 Helmet smoke 不得通过旧 fallback 成功。
- 不处理 OfflineRT 配置入口硬切；由 `REQ-073-g` / `REQ-073-h` 处理。
- 不实现完整 PBRT/glTF material 物理等价，只要求 `standard-pbr` metallic-roughness 工作流在 realtime smoke 中可用。
- 不实现 package、BC7 或 pipeline cache blob。

## 需求

### R1: Material Type Owns Source Variant

material `type` SHALL 是 shader variant 和 material source contract 的语义入口。

要求：

- `.material` 中的 `bsdf.type` 必须与 `bsdf.source` 反射出的 `type` 一致。
- 同一个 `type` 在同一场景/资源表中只能对应一个 source URI、reflection hash 和 source signature。
- 同一个 `type` 映射到多个 source SHALL fail-fast，不能选第一个、最后一个或静默合并。
- `source` 用于 include、反射校验、diagnostic 和热重载失效判断，不作为一套绕过 `type` 的第二分类。
- material 参数值、texture id、texture 是否存在、material URI、material name 不得进入 material type variant identity。

### R2: `standard-pbr` Material Type

glTF metallic-roughness 工作流 SHALL 使用独立 material type。

要求：

- 新增 `standard-pbr` contract source，例如 `assets://shaders/glsl/common/materials/standard_pbr.contract.glsl`。
- `standard-pbr` 至少表达 base color、metallic、roughness、normal、occlusion、emissive、alpha。
- glTF Helmet 转换输出 `type: standard-pbr`，不得继续把 metallic-roughness 伪装成 PBRT `uber`。
- PBRT `uber`、PBRT `metal`、glTF `standard-pbr` 是不同 type；它们可以返回同一套 shader BSDF ABI 类型，但不能共享 material type identity。

### R3: Shader-side BSDF Interface ABI

每个可参与光照计算的 material source shader SHALL 实现统一 BSDF 函数接口。

最低接口：

| 函数 | 用途 | 最低使用者 |
|---|---|---|
| `lxEvaluateBsdf(...)` | 给定入射/出射方向，返回固定方向对上的 BSDF 值 `f(wi, wo)` | Forward、Deferred lighting、OfflineRT direct |
| `lxSampleBsdf(...)` | 给定一侧方向和随机数，返回采样出的另一侧方向、该方向对上的 BSDF 值和采样 PDF | OfflineRT direct |

要求：

- Forward 至少要求 final material source variant 提供 `evaluate` 所需接口。
- OfflineRT direct 至少要求 final material source variant 提供 `evaluate/sample` 所需接口。
- 本 REQ 不要求独立 `lxPdfBsdf` 入口；固定方向的 BSDF 值由 `lxEvaluateBsdf` 返回，采样方向、该方向 BSDF 值和 PDF 由 `lxSampleBsdf` 一起返回。
- pass shader 只 include 当前 material source variant，然后调用统一函数名。
- pass shader 不写 `#if MATERIAL_TYPE_UBER` / `#elif MATERIAL_TYPE_STANDARD_PBR` 这类按材质类型分支。
- `LxMaterialSurface` 可作为可选表面摘要访问器保留，用于 GBuffer/debug/显示属性，但不能成为 Forward/OfflineRT 主计算路径的唯一抽象。
- 某个 pass 需要的 BSDF 函数缺失时，编译或 ABI 校验 SHALL fail-fast。

### R4: Source Variant Resolver

source variant resolver SHALL 在场景资源解析完成后、RenderWorkQueue / PipelineBuildDesc 构建前统一解析、编译和反射本场景需要的 final shader variants。

variant key 至少包含：

| 字段 | 说明 |
|---|---|
| resolved base shader identity | RenderPathGraph pass 中声明并按当前 URI 规则解析出的 shader 身份；不做 legacy URI 迁移 |
| material type | `.material` 的 `bsdf.type` |
| material source URI | `.material` 的 `bsdf.source` |
| source reflection hash | contract source 反射 hash |
| source signature | material storage / accessor / BSDF ABI 结构签名 |
| RenderPathNode signature | pass/node 的 pipeline 结构身份 |

规则：

- 同一个 base shader 可以产生多个 material type variant。
- 不同 material type variant 可以产生不同 shader module、reflection payload 和 pipeline key。
- 同 type / 同 RenderPathNode / 不同 material 参数值 SHALL 共享 final shader variant。
- 缺 source、source 不支持当前 pass、source 未实现 accessor/BSDF ABI 时必须 fail-fast。
- RenderPathNode 声明 `material.bsdf` 与 base shader 是否包含 `LX_MATERIAL_CONTRACT_SOURCE` 必须一致：声明了 material source 但 shader 没有 include，或 shader 需要 source 但 node 没声明，都 SHALL fail-fast。
- 场景材质尚未加载完成时，resolver 可以 no-op，但不得注册 unresolved variant shader；场景加载完成后再统一解析、编译和反射。

### R5: PipelineKey Algorithm

pipeline identity SHALL 使用以下算法：

```text
PipelineKey = compose(
  MaterialTypeVariant,
  RenderPathNodeSignature
)
```

要求：

- `ObjectRender` 不再作为 pipeline key 轴。
- Render target 不再作为独立 pipeline key 轴；如果 attachment 格式影响 pipeline，它必须通过 RenderPathNodeSignature 体现。
- material pass definition、render state、shader URI 不得挂在 material identity 下；它们属于 RenderPathNodeSignature。
- material values、texture id、texture presence 不得进入 PipelineKey。
- object mesh 只参与 geometry/topology compatibility validation，不通过完整 vertex layout 拆 PSO。
- 同 material type / 同 RenderPathNode / 不同 material 参数值或 texture presence SHALL 共享 PipelineKey。
- 不同 material type 或不同 RenderPathNodeSignature SHALL 产生不同 PipelineKey。

### R6: RenderPathNode Pipeline Contract

RenderPathNode SHALL 显式声明 pipeline contract，不能由 material 或 object 隐式推断。

最低字段：

| 字段 | 说明 |
|---|---|
| pass id | node 在 RenderPathGraph 内的身份 |
| shader | base shader URI |
| stage / dispatch | raster / compute，draw / fullscreen / compute |
| renderState | cull/depth/blend 等固定功能状态 |
| sources / targets | pass 读写资源声明 |
| geometry | draw pass 的 vertex/topology contract |
| rendering | `dynamic` 或 `traditional` |
| attachments | traditional 模式下的 target name、format、samples、layers 等 |

规则：

- `rendering` 必须显式出现；不允许根据 target 名字、后端能力或 shader 类型隐式推断。
- `dynamic` 模式不创建传统 `VkRenderPass` / framebuffer 兼容对象，但 Vulkan graphics pipeline 创建仍需要通过 `VkPipelineRenderingCreateInfo` 获得 color/depth/stencil attachment formats；这些格式来自 RenderPathNode target contract，并进入 RenderPathNodeSignature。
- `traditional` 模式必须显式声明 attachment target、format、sample count 和 layer count，这些进入 RenderPathNodeSignature。
- backend 不支持 node 声明的 rendering mode 时必须 fail-fast，不能自动切换到另一种模式。
- `geometry.vertex`、`geometry.index`、`material.bsdf`、`scene.camera` 等内置资源名 SHALL 有文档或集中表列出；parser 不得接受未建模字段后忽略。
- object 的 vertex/topology 与 node geometry contract 不兼容时 SHALL fail-fast，不得靠新 pipeline key 分裂规避。

### R7: Reflection Uses Final Variant Shader

shader reflection / pipeline validation SHALL 使用 variant 后最终 shader。

要求：

- descriptor set / binding / push constant / storage layout 来自最终编译 shader。
- `PipelineBuildDesc::fromRenderWorkItem()` 使用 variant reflection，不使用 base shader 估算。
- reflection cache key 与 shader compile key 对齐。
- PipelineKey 的 material type variant 和 RenderPathNodeSignature 必须与 PipelineBuildDesc 消费的 final shader reflection 对齐。

### R8: No Runtime Source Branch Or Fallback

Forward、Deferred 和 OfflineRT pass shader SHALL 不写 material type/source runtime branch。

禁止：

- `if materialType == ...`
- `switch materialSource`
- 在 pass shader 中解析多个 material storage layout。
- 按材质 type 写大段 `#if/#elif` BRDF 实现。
- 因缺少 source variant 而落回旧共享 `MaterialUBO`、debug material、空 source 或 legacy per-material descriptor。
- DebugOverlay 等工具 pass 必须使用自己的专用 shader/material contract，不能继续借用通用 `assets/materials/pbr.material` 作为正向路径。

### R9: Shader Build Target Boundary

shader build / test target SHALL 明确表达 source-variant shader 的编译时机。

要求：

- 需要 `LX_MATERIAL_CONTRACT_SOURCE` 的 shader source 不得作为普通 base shader 裸编译成功条件。
- `CompileShaders`、`BuildTest` 或等价 build target 要么提供 material source variant context，要么排除 variant-only base source 并由独立 variant target 覆盖。
- variant build target 能覆盖 Forward、Deferred 和 OfflineRT direct。
- 缺 source variant context 的失败必须命名 shader URI、缺失宏或 source、以及应使用的 variant build path。
- 不允许通过删除 shader `#error`、注入空 source、或 fallback 到旧 `MaterialUBO` 让裸编译通过。

### R10: Helmet Material Conversion And Smoke

材质转换工具 SHALL 能把 Helmet/glTF metallic-roughness 材质转换成显式 Material v2 资产。

要求：

- 转换工具是本 REQ 的交付内容，不后置到 smoke hard cut。
- 转换输出至少包含一个 `standard-pbr` `.material` 和一个引用该 material 的 scene YAML。
- scene 可以继续引用 glTF mesh 文件；本 REQ 不要求转换 mesh 格式。
- 转换应保留 glTF baseColor factor/texture、metallic factor、roughness factor、metallic-roughness texture、normal texture、occlusion texture、emissive factor/texture、alpha mode 和 alpha cutoff。
- 转换后的 scene 不得通过 `assets/materials/pbr.material` 这类旧通用 `uber` 近似材质证明成功。
- 转换后的 scene 不得依赖运行时 `source: gltf` 隐式桥接来生成材质。
- realtime smoke 使用转换后的 scene，输出低分辨率非黑图。
- smoke diagnostics 必须证明使用了 material type variant resolver、final shader reflection 和 RenderPathNode pipeline contract。
- 如果无法渲染，准备阶段必须停止并报告缺失字段/路径，不能隐藏问题。
- SceneRuntime 加载 Material v2 后必须显式解析 material texture dependencies 并写入 source material texture slot；缺失 texture/resource path 必须让场景加载失败，不能静默变成黑材质。
- realtime smoke 必须等 active scene 加载完成后才能发起 render profile；无项目、无 active scene、scene open pending 或 active scene 未加载完成时，profile 命令必须 fail-fast。

### R11: Diagnostics

variant resolver、RenderPathNode pipeline contract 和 Helmet smoke SHALL 输出可审计 diagnostics：

- material type。
- material source URI、reflection hash 和 source signature。
- base shader identity。
- RenderPathGraph / RenderPathNode / pass id。
- RenderPathNodeSignature / compile key / reflection key / PipelineKey。
- rendering mode：dynamic 或 traditional。
- geometry/topology contract 和 object compatibility failure。
- final shader descriptor summary。
- unsupported source、missing BSDF ABI、reflection failure、backend rendering mode unsupported 的原因。

无法编译、反射、构建 pipeline 或准备 smoke 时必须停止，不能隐藏为 fallback shader。

## 测试

### T1: Material Type Source Uniqueness

构造同一场景中多个 material：

- 同 `type=standard-pbr` / 同 source / 不同参数值应通过。
- 同 `type=standard-pbr` / 不同 source 应 fail-fast。
- `.material` 的 `type` 与 contract source 反射 `type` 不一致应 fail-fast。
- diagnostics 包含 type、source URI、reflection hash 和冲突 material。

### T2: `standard-pbr` Contract

解析并反射 `standard-pbr` contract source，断言：

- contract `type` 为 `standard-pbr`。
- 至少声明 baseColor、metallic、roughness、normal、occlusion、emissive、alpha 相关参数。
- 支持 texture/rgb/float 等 glTF metallic-roughness 需要的参数 kind。
- source signature 稳定，参数值变化不改变 signature。

### T3: BSDF Shader ABI

编译 Forward / Deferred / OfflineRT direct source variants，断言：

- Forward variant 能调用 `lxEvaluateBsdf`，并获得固定方向对上的 BSDF 值。
- OfflineRT direct variant 能调用 `lxEvaluateBsdf` 和 `lxSampleBsdf`；`lxSampleBsdf` 返回采样方向、该方向 BSDF 值和 PDF。
- 人为移除某个 required BSDF 函数时编译或 ABI 校验失败。
- pass shader 不出现按 material type 的 runtime branch 或宏分支。

### T4: Final Shader Reflection

断言 pipeline desc / descriptor layout 来自 variant 后最终 shader：

- final shader reflection 包含 source 声明的 material storage binding。
- `PipelineBuildDesc::fromRenderWorkItem()` 使用 final shader reflection。
- key 是 `standard-pbr` variant 时，reflection 也来自 `standard-pbr` final shader，不能来自 base shader 或 `uber` source。

### T5: PipelineKey Algorithm

构造 work item / pipeline build desc，断言：

- 同 material type、同 RenderPathNode、不同参数值共享 PipelineKey。
- 同 material type、同 RenderPathNode、texture presence 不拆 PipelineKey。
- 不同 material type 产生不同 PipelineKey。
- RenderPathNode renderState / shader / rendering mode / traditional attachment contract 改变时 PipelineKey 改变。
- object vertex layout 或完整 mesh attribute 差异不直接改变 PipelineKey。

### T6: Geometry Contract Compatibility

构造 RenderPathNode geometry contract 和对象 mesh：

- 默认 draw pass 可声明 position-only vertex contract。
- object mesh 满足 position/topology contract 时不拆 pipeline。
- object mesh 缺 position 或 topology 不兼容时 fail-fast。
- DebugOverlay 等特殊 pass 可以声明不同 geometry/topology contract，并由 validation 检查。

### T7: RenderPathNode Rendering Mode Contract

解析 RenderPathGraph asset，断言：

- 每个 pass 必须显式声明 `rendering`。
- `dynamic` pass 不需要传统 render pass / framebuffer 对象，但 raster 输出 attachment 的 format/sample/layer 必须由 RenderPathNode target contract 显式给出。
- `traditional` pass 缺 attachment target/format/sample/layer 信息时失败。
- backend 不支持声明的 mode 时失败，不能自动 fallback。
- node definition hash 包含 sources、targets、renderState、geometry、rendering mode 和 traditional attachment contract。

### T8: Built-in RenderPath Resource Vocabulary

验证内置资源名文档或集中表：

- 覆盖 `geometry.vertex`、`geometry.index`、`material.bsdf`、`scene.camera`、`scene.lights`、常见 GBuffer/HDR/Shadow target。
- parser/validator 对未知资源名输出明确 diagnostic。
- 已 allowlist 的资源名必须被转换进 FrameGraph/RenderPathNode contract，不能接受后忽略。

### T9: CompileShaders Variant Boundary

运行 shader build / `BuildTest` 相关目标，断言：

- source-variant shader 不再被普通裸编译目标误判为失败。
- variant build target 能为 Forward、Deferred 和 OfflineRT direct shader 提供 source context。
- 人为移除 source context 时失败信息指向缺失 variant，而不是静默 fallback。

### T10: No Runtime Branch Audit

rg/audit Forward / Deferred / OfflineRT pass shader，断言没有：

- `materialType` / `materialSource` runtime branch。
- 按 type 展开的 BRDF `#if/#elif` 分支。
- 旧 `MaterialUBO` 或 debug material 作为正向路径。
- 空 source include 或旧 shared material fallback。

### T11: Helmet Conversion Output

运行 Helmet/glTF material conversion，断言：

- 生成显式 `standard-pbr` `.material`。
- 生成引用该 material 的 scene YAML。
- 输出 material 包含 glTF baseColor、metallic、roughness、normal、occlusion、emissive/alpha 的可追踪参数或 texture envelope。
- 转换输出不引用 `assets/materials/pbr.material` 作为成功路径。
- 转换输出不依赖 `source: gltf` 运行时桥接。

### T12: Helmet Realtime Smoke

使用转换后的 Helmet scene 运行低分辨率 realtime smoke，断言：

- 输出非黑图，lit pixel / luminance 统计超过阈值。
- diagnostics 显示 `type=standard-pbr`、对应 source URI、variant key、RenderPathNodeSignature 和 PipelineKey。
- smoke 不走 legacy material fallback、debug shader、空 source、旧 per-material descriptor 或旧通用 `uber` 材质。
- 如果失败，错误在渲染准备阶段暴露，并包含缺失字段/资源/ABI。

### T13: Diagnostics

覆盖缺 shader、缺 source、type/source mismatch、duplicate source for type、missing BSDF ABI、reflection failure、unsupported rendering mode、geometry contract mismatch，断言 diagnostics 包含：

- material type 和 source URI。
- RenderPathGraph / pass id。
- compile/reflection/PipelineKey 相关身份。
- 明确失败原因。

## 修改范围

- material contract source / material source reflection
- `standard-pbr` contract source and tests
- shader compiler variant key / include injection
- BSDF shader interface headers and Forward / Deferred / OfflineRT source-variant shader usage
- RenderPathGraph / RenderPassNode parser and validation
- RenderPathNode pipeline signature / pipeline key algorithm
- shader reflection cache and `PipelineBuildDesc`
- CMake / shader build targets for source variants
- Helmet/glTF material conversion tool and generated validation scene
- Helmet realtime smoke and diagnostics

## 边界与约束

- 不写 shader runtime source/type branch。
- 不用 base shader reflection 代替 variant shader reflection。
- 不把 material pass definition、render state 或 shader URI 继续挂在 material identity 下。
- 不让 ObjectRender 继续按完整 vertex layout 拆 PipelineKey。
- 不通过旧 `assets/materials/pbr.material` / `type=uber` 近似材质证明 Helmet metallic-roughness smoke 成功。
- 不通过 `source: gltf` 运行时桥接绕过转换资产。
- 不在本 REQ 全量删除旧 renderer fallback；但本 REQ 的新测试和 Helmet smoke 必须证明没有走旧 fallback。
- 不把 URI 迁移混进本 REQ；`techniques/...` 到 `render_paths/...` 的默认路径硬切由 `REQ-073-d` 承接。

## 依赖

- `REQ-073-a`: Material source contract 和 Material Accessor ABI。
- `REQ-073-b`: source-local material storage / upload view foundation。

## 后续工作

- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。
- `REQ-073-e`: Indirect material batching and diagnostics。
- `REQ-073-f`: Realtime material path hard cut and high-confidence smoke。
- `REQ-073-g`: OfflineRT RenderPathGraph compute path。

## 实施状态

已完成。

本 REQ 的实现落地为：

- `standard-pbr` contract source、BSDF evaluate/sample ABI、Forward/Deferred/OfflineRT source variant shader 编译边界。
- RenderPathNode 显式 `rendering`、geometry/topology、source/target/attachment contract 和 node signature。
- `PipelineKey = MaterialTypeVariant + RenderPathNodeSignature`，不再把完整 ObjectRender vertex layout 或独立 RenderTarget 作为 key 轴。
- material type/source uniqueness validation、场景加载后 source variant resolver、final shader reflection metadata。
- glTF Helmet material converter、生成的 `standard-pbr` material/scene，以及 CTest smoke 入口。
- SceneRuntime 的 Material v2 texture dependency 显式加载和 DebugOverlay 专用 material，避免 Helmet smoke 走旧 `assets/materials/pbr.material` 或隐式 glTF bridge。

验证命令：

```bash
cmake --build build --target lxe_editor test_render_resource_parsers test_material_source_variant_pipeline test_shader_compiler test_scene_resource_upload_view_v2 test_realtime_render_profile_commands test_lxe_editor_api_service test_lxe_editor_interaction test_viewport_overlay test_vulkan_frame_graph
./build/src/test/test_render_resource_parsers
./build/src/test/test_material_source_variant_pipeline
./build/src/test/test_shader_compiler
./build/src/test/test_scene_resource_upload_view_v2
./build/src/test/test_realtime_render_profile_commands
./build/src/test/test_lxe_editor_api_service
./build/src/test/test_lxe_editor_interaction
./build/src/test/test_viewport_overlay
python3 src/test/integration/test_lxe_gltf_material_convert.py --source-dir .
cmake --build build --target CompileMaterialSourceShaderVariants test_material_source_contract test_bindless_indirect_contract test_vulkan_resource_manager
./build/src/test/test_material_source_contract
./build/src/test/test_bindless_indirect_contract
xvfb-run -a ./build/src/test/test_vulkan_frame_graph
xvfb-run -a ./build/src/test/test_vulkan_resource_manager
cmake --build build --target CompileShaders test_gltf_scene_asset_loader test_pipeline_identity test_pipeline_build_info
./build/src/test/test_gltf_scene_asset_loader
./build/src/test/test_pipeline_identity
./build/src/test/test_pipeline_build_info
python3 src/tools/lxe_realtime_render/lxe_realtime_render.py --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml --profile preview --xvfb --require-nonblack --require-pipeline-metadata
python3 src/test/integration/test_helmet_standard_pbr_realtime_smoke.py --source-dir . --editor build/src/demos/lxe_editor/lxe_editor
ctest --test-dir build --output-on-failure -R test_helmet_standard_pbr_realtime_smoke
```

Helmet smoke 输出了 192x192 非黑图，`litPixelCount=12349`、`averageLuminance=0.16042135793249698`，metadata 检查确认包含 `standard-pbr`、`standard_pbr.contract.glsl`、`RenderPathNodeSignature`、`PipelineKey` 和 final shader reflection，且不包含旧 `assets/materials/pbr.material`、`source: gltf` 或 fallback token。

剩余的旧路径清理不属于本 REQ 的完成条件，继续由已列出的 `REQ-073-d` 到 `REQ-073-g` 承接；本 REQ 的 Helmet smoke 不依赖这些旧路径。
