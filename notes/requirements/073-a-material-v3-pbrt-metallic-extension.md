# REQ-073-a: Material v3 PBRT Source Contract And Metallic Extension

> 2026-06-12 更新：本 REQ 承接 `REQ-071` / `REQ-072` 的 Material v2 与 Helmet/BMW 验证工作。目标是在 PBRT-style SurfaceMaterial 合同上增加 metallic / factor × texture 表达，并把材质结构定义从 C++ per-type schema 硬切到 `.material` 中显式声明的 `bsdf.source`。`bsdf.source` 指向可反射的 material contract source；C++ 只负责通用反射、校验、依赖注册和按反射 layout 打包，不再为每个材质 type 手写 schema/packing 类。

## 背景

当前材质系统处在 Material v2 pure envelope、RenderPathGraph 和 GPU PBR 近似路径之间：

| 当前事实 | 影响 |
|---|---|
| `MaterialResourceParser` 解析 PBRT 风格 `bsdf.parameters` envelope | BMW M6 的 PBRT 材质语义可以进入 runtime，但实时 PBR shader 仍需要固定 GPU record |
| `SceneGpuMaterialRecord` / shader 内联 `lxSceneMaterialRecord` 仍是共享 PBR 上传布局 | 它不能作为 Material v3 完成态；Material v3 需要由 material source contract 反射出 storage ABI |
| `MaterialSurfaceSchema` 当前集中列出 PBRT type | 这是旧 C++ schema 真相，本 REQ 要从默认路径硬切掉 |
| `PipelineKey` 已由 object signature、material signature、target signature 组合 | material signature 可以承载 `bsdf.source` 反射出的结构事实，避免按材质实例或贴图存在性拆 pipeline |
| Helmet glTF metallic-roughness 与 BMW PBRT converter 来源不同 | 如果按来源或 workflow 拆材质合同，会增加 pipeline/draw batch 数量，并扩大 shader 变体 |

本 REQ 的设计选择是：材质文件和 `MaterialInstance` 仍以 PBRT-style SurfaceMaterial 参数为唯一运行时真相；材质文件必须显式声明 `bsdf.source`，由该 source 定义材质结构、shader ABI 和访问器接口。C++ 不维护 `matte/metal/uber/substrate` 的 per-type schema/packing 类，也不根据 `bsdf.type` 猜默认 source。

## 目标

1. 扩展 Material v3 PBRT-style 参数合同，增加 metallic 常量和贴图输入，补齐 glTF MR 需要的字段。
2. 为 `.material` 增加必填 `bsdf.source`，指向 material contract source。
3. 定义 source-reflected material contract：参数 schema、texture 参数、channel selector、默认纹理语义、material storage ABI、shader variant identity 和 Material Accessor ABI 都由 source 反射得到。
4. 定义 Material Accessor ABI：Forward / Deferred / OfflineRT pass shader 只调用统一访问器，不写 material type/source runtime 分支。
5. 定义 `MaterialSignature`：由 `bsdf.source` URI、source reflection hash、variant include/define、material storage ABI、pass shader、render state 等结构事实决定。
6. 保留 PBRT 原有参数作为材质运行时真相，不用新的 source workflow 材质类或 view 复制一套相似数据。
7. 所有支持贴图的参数统一使用 `factor * sample(defaultableTexture)`。
8. 缺失贴图通过全局默认纹理处理，不改变材质合同、shader variant 或 pipeline key。
9. converter 文档明确 glTF MR 与 PBRT source 参数如何写入这套扩展后的 PBRT-style 材质文件，并显式写入 `bsdf.source`。

## 非目标

- 不实现完整 PBRT `glass`、`fourier`、multi-level `mix` 或 conductor `eta/k` 的物理精确实时模型。
- 不引入 `workflow` / `workflow-tag` 运行时字段。
- 不引入按 source workflow 区分的 realtime PBR material class、view 或并行参数表。
- 不为常量参数和贴图参数拆 shader variant。
- 不实现 BC7 或其他纹理压缩；纹理压缩由 `REQ-074-a` 单独处理。
- 不恢复旧 `.material` root `parameters` / `resources` 或旧 `MaterialUBO` 作为 runtime truth。
- 不新增 `src/core/asset/material_types/` 这类 per-type C++ schema/packing 目录。

## 需求

### R1: Extended PBRT-style Material Parameters

Material v3 SHALL 在 PBRT-style `bsdf.parameters` envelope 上增加 PBR realtime path 所需参数。它们是材质参数合同的一部分，不是 GPU record 独有字段。

最低字段：

| 字段 | 说明 |
|---|---|
| `Kd` | PBRT diffuse/base color 参数；glTF `baseColor` 写入这里 |
| `metallic` | metallic scalar multiplier；glTF `metallicFactor` 写入这里 |
| `roughness` | isotropic roughness 输入；需要 PBRT `uroughness/vroughness` 时由 converter 同步写入 |
| `uroughness` / `vroughness` | PBRT anisotropic roughness 参数，继续保留 |
| `ao` | ambient occlusion multiplier，作为 PBR realtime extension |
| `emissive` | emissive RGB multiplier，作为 PBR realtime extension |
| `normalScale` | normal map strength，缺省为 `1.0` |
| `normalmap` | PBRT-style normal map texture 参数 |
| texture / channel metadata | 每个支持贴图的参数可以记录 stable texture URI/slot 和 packed channel selector |

规则：

- PBRT 原有参数如 `Ks`、`Kr`、`Kt`、`eta`、`k`、`opacity`、`bsdffile` 等继续保留，不被 metallic-roughness 表达替代。
- glTF MR loader / converter SHALL 把 `baseColor`、`metallic`、`roughness`、normal、AO、emissive 写入上述 Material v3 参数。
- PBRT converter SHALL 显式写入 realtime PBR path 需要的 `metallic` 等 extension 参数；runtime 不按 source workflow 隐式推断。
- GPU record / storage 只是 `MaterialInstance` 参数的上传布局，不是第二套材质数据模型。

### R2: `bsdf.source` Material Contract Source

每个 Material v3 `.material` SHALL 在 `bsdf` 下声明必填 `source` 字段。

示例形状：

```yaml
schema: material.v3
bsdf:
  type: matte
  source: assets/shaders/glsl/common/materials/matte.contract.glsl
  parameters:
    Kd:
      type: rgb
      value: [1.0, 1.0, 1.0]
    metallic:
      type: float
      value: 0.0
```

规则：

- `bsdf.source` SHALL 是可解析 resource URI。
- runtime SHALL NOT 根据 `bsdf.type` 推断默认 source。
- source SHALL 声明自己的 material type；它必须与 `bsdf.type` 一致。
- source SHALL 可被 material contract reflection 解析。
- source 缺失、不可反射、不是 material contract、声明 unsupported，material 加载失败。
- `glass` / `fourier` / `mix` 如没有 supported contract source，migrated runtime/validation 加载期失败；不能进入渲染队列，也不能输出 diagnostic debug 色。

### R3: Source-reflected Material Contract

material contract source SHALL 声明并可反射以下合同：

- declared material type。
- support status / capabilities。
- 参数 schema：字段名、required/optional、允许 envelope kind、默认值。
- texture-capable 参数的 factor/texture/channel 规则。
- 默认纹理语义：white、black、flatNormal 等。
- material storage / SSBO record layout。
- shader variant identity、include path、reflection hash。
- Material Accessor ABI 入口和返回结构。

C++ SHALL 只保存通用 `MaterialContractReflection` 数据模型，不为每个 type 写 schema/packing 逻辑。现有 `MaterialSurfaceSchema` SHALL 从 Material v3 正向路径删除；若仍出现，只能作为 legacy rejection/audit 目标。

### R4: Material Accessor ABI

每个可渲染 material contract source SHALL 实现统一 Material Accessor ABI。

最低接口语义：

```glsl
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex,
                                        vec2 uv,
                                        vec3 geometricNormal,
                                        mat3 tangentFrame);
```

`LxMaterialSurface` 至少包含：

| 字段 | 用途 |
|---|---|
| `baseColor` | Forward direct / Deferred albedo / OfflineRT direct |
| `alpha` | 透明度或后续 alpha path |
| `metallic` | PBR F0 / GBuffer material |
| `roughness` | direct / IBL / GBuffer |
| `normal` | normal map 后的 shading normal |
| `ao` | ambient / GBuffer |
| `emissive` | emissive contribution |

规则：

- Forward、Deferred、OfflineRT pass shader SHALL 只调用 Material Accessor ABI。
- 下游 pass shader SHALL NOT 写 `if materialType == ...`、`switch source` 或解析多个 material storage layout。
- source 未实现访问器、返回结构缺字段、required binding 不存在，加载/编译/反射失败。
- 不同 source 可以有不同内部 storage ABI，但对 pass shader 暴露的 accessor ABI 必须稳定。

### R5: Material Source Shader Variant

`bsdf.source` SHALL 作为 shader variant 输入，而不是 runtime branch 变量。

规则：

- shader variant resolver SHALL 从 `MaterialInstance` 的 source URI / source reflection hash 生成 stable source variant key。
- variant key SHALL 进入 shader compile key、shader reflection key、`MaterialSignature` 和最终 `PipelineKey`。
- shader compiler SHALL 在编译时 include 对应 material contract source，并导入 source variant define 或等价 metadata。
- render path shader 可以是同一个 base shader URI；不同 source variant 选择不同 material contract include。
- shader reflection / pipeline validation SHALL 使用 variant 后的最终 shader，而不是 base shader 估算 bindings。
- 常量 vs 贴图、texture id、packed channel、material URI 不得进入 shader variant key。

### R6: Factor Multiplied By Texture

所有支持贴图的参数 SHALL 使用统一组合规则：

```text
Kd = Kd.value * sample(Kd.texture)
metallic = metallic.value * sampleChannel(metallic.texture)
roughness = roughness.value * sampleChannel(roughness.texture)
ao = ao.value * sampleChannel(ao.texture)
emissive = emissive.value * sample(emissive.texture)
```

规则：

- 缺失贴图不创建新的 layout。
- 缺失贴图不创建新的 shader variant。
- 缺失贴图不需要材质专属 descriptor fallback。
- texture index 指向默认纹理时，shader 仍执行同一访问路径。
- 如果同一 packed texture 同时提供 metallic/roughness/AO，材质参数 metadata SHALL 明确记录通道选择。

### R7: Default Texture Set

Resource table / GPU upload 路径 SHALL 注册稳定的内置默认纹理。

最低集合：

| 默认纹理 | 用途 | 典型值 |
|---|---|---|
| `white` | baseColor、metallic、roughness、AO 等乘法贴图 fallback | `(1, 1, 1, 1)` |
| `black` | emissive 或需要零贡献的 fallback | `(0, 0, 0, 1)` |
| `flatNormal` | normal map fallback | `(0.5, 0.5, 1.0, 1.0)` |

默认纹理 SHALL 由 resource table 或当前 GPU 上传路径的固定内置资源提供，不能由每个材质临时创建 placeholder。默认资源必须有稳定 resource identity。全局 bindless slot 去重由 `REQ-073-b` 验证；package/cache 记录由 `REQ-074-c` 之后处理。

### R8: No Workflow Field In Runtime Material

Material v3 第一阶段 SHALL NOT 增加运行时 `workflow` 字段。

规则：

- glTF metallic-roughness、PBRT converter、手写 material 文件的来源差异属于 converter 文档、diagnostics 或 authoring metadata。
- runtime render path 消费 Material v3 参数上传后的 source-reflected material storage，但该 storage 不构成第二套材质语义。
- `PipelineKey` 不应因为材质来源是 glTF 或 PBRT 而变化。
- 不引入 `MaterialLayoutSignature` 来区分 `original-pbrt` / `metallic-roughness` 来源；结构签名由 `bsdf.source`、pass shader、render state、target 等事实决定。

### R9: Source Inputs Write The Same Material Contract

glTF loader、PBRT converter 和手写 `.material` SHALL 写入同一套 Material v3 参数合同，并显式写 `bsdf.source`。

最低建议：

| Source input | 写入规则 |
|---|---|
| glTF `baseColorFactor/baseColorTexture` | `Kd.value` / `Kd.texture` |
| glTF `metallicFactor` / metallic-roughness texture B | `metallic.value` / `metallic.texture` with channel selector |
| glTF `roughnessFactor` / metallic-roughness texture G | `roughness.value` / `roughness.texture` with channel selector；需要 PBRT anisotropic 参数时同步写入 `uroughness/vroughness` |
| glTF normal / AO / emissive | `normalmap`、`ao`、`emissive` extension parameters |
| PBRT `matte` / `uber` / `substrate` | 保留 PBRT 原始参数，并由 converter 显式写入 realtime PBR extension 参数，例如 `metallic=0` |
| PBRT `metal` | 保留 `eta/k` 等 PBRT 参数，并由 converter 显式写入 realtime PBR extension 参数，例如 `metallic=1` |
| PBRT `glass` / `fourier` / `mix` | 保留原始 PBRT 参数；当前没有 supported contract source 时加载期失败 |

converter report SHALL 记录哪些 realtime extension 参数是从 source 直接转换、显式默认或 diagnostic approximation 得到。运行时 parser 不应根据 PBRT/glTF 来源自行推断 workflow 或自动填补隐式材质语义。

### R10: Material Signature, Pipeline Identity, And Batching

`MaterialSignature` SHALL 表达 material source contract 这类结构事实，并参与 `PipelineKey`。

规则：

- `MaterialSignature` 由 `bsdf.source` URI、source reflection hash、Material Accessor ABI、shader variant include/define、material storage ABI、pass shader、render state、target 等结构事实决定。
- `PipelineKey` 继续由 object signature、material signature、target signature 组合。
- 不同 source 可以生成不同 pipeline。
- 同一 source 的不同材质实例必须共享同一个 material signature，前提是 pass shader/render state 等结构事实相同。
- `Kd`、`metallic`、`roughness` 等参数的 value-level 差异不改变 pipeline key。
- texture 存在性、texture id、packed channel、material URI、material handle、material name 不改变 pipeline key。
- render state、vertex layout、target、pass、shader 程序等结构事实仍然可以影响 pipeline key。
- 不按 material StringID 拆 pipeline；需要分组时按 source signature 等真正结构签名分组。

### R11: Contract Invariants

同一 source signature SHALL 唯一决定 material record layout。

如果同一 source signature 反射出不一致 layout，这是 engine invariant violation，不是用户资产错误，也不是可兼容输入。实现 SHALL 报告内部一致性诊断并停止，诊断包含 source URI、reflection hash 和冲突 layout 来源。不得 fallback、拆第二个 layout、或静默选择其中一个 layout。

### R12: Validation Assets

Helmet 和 BMW M6 validation SHALL 使用 Material v3 source-reflected material contract。

要求：

- Helmet glTF metallic-roughness 材质正确填充 `bsdf.source`、`Kd`、`metallic`、`roughness`、`normalmap`、`ao`、`emissive`。
- BMW PBRT converter 输出的材质保留 PBRT 参数，并显式填充 `bsdf.source` 和 realtime PBR path 需要的 extension 参数。
- 两者不依赖旧 `MaterialUBO` 作为材质参数真相。
- 缺失贴图使用默认纹理，不导致 descriptor 缺失或 shader fallback。

## 测试

### T1: Missing Or Invalid Source Fails

覆盖：

- 缺 `bsdf.source`。
- source 不存在。
- source 不是 material contract。
- source reflection 失败。
- source declared type 与 `bsdf.type` 不一致。
- unsupported source。

全部必须加载失败并输出 material URI、`bsdf.type`、`bsdf.source` 和失败字段。

### T2: Contract Parameter Validation

覆盖：

- 未知 parameter 失败。
- 缺 required parameter 失败。
- envelope kind 不匹配失败。
- illegal channel selector 失败。
- source 未实现 Material Accessor ABI 失败。

### T3: Constant And Texture Share Layout

构造两个材质：

- A 只设置 `Kd.value`。
- B 设置 `Kd.value + Kd.texture`。

断言：

- 两者使用相同 source signature。
- 两者导出的 upload layout 相同。
- 两者的 `PipelineKey` 在相同 pass / target / vertex layout 下相同。
- B 的输出颜色为 factor 与 texture sample 相乘。

### T4: Metallic Field Contract

覆盖：

- metallic 缺省为 `0.0`。
- metallic factor 生效。
- metallic texture channel 与 factor 相乘。
- packed metallic-roughness texture 的通道选择被记录并被 shader 使用。

### T5: Default Texture Upload

断言：

- white、black、flatNormal 默认纹理只注册/上传一次。
- 缺失 baseColor/roughness/AO texture 指向 white。
- 缺失 emissive texture 指向 black。
- 缺失 normal texture 指向 flatNormal。
- shader 采样默认纹理时不产生全黑或 NaN。

### T6: Material Accessor ABI

Forward、Deferred、OfflineRT 都只调用统一 Material Accessor ABI。

断言：

- pass shader 不包含 material type/source runtime branch。
- pass shader 不解析多个 material storage layout。
- variant 后 shader reflection 能看到 source 声明的 material storage binding。
- source 返回 `LxMaterialSurface` 的 required 字段。

### T7: Material Signature

构造多个 source 和多个材质实例。

断言：

- 不同 source 或不同 source reflection hash 的 `MaterialSignature` 不同。
- 同一 source 的两个实例，即使 `Kd`、metallic、roughness、texture id 不同，`MaterialSignature` 相同。
- `PipelineKey` 包含 material signature。
- glTF MR 和 PBRT source 写入同一 source 时，不因 source workflow 产生不同 signature。
- shader compile key / reflection key 包含 source variant identity。

### T8: Contract Invariant Violation

测试注入同一 source signature 但冲突 record layout 的 fake reflection。

断言：

- 实现报告 engine invariant violation。
- 不生成第二种 layout。
- 不 fallback 到旧共享 material record。

### T9: Helmet And BMW PBR Approximation

运行低分辨率 validation：

- Helmet glTF MR 使用 Material v3 source-reflected 参数合同。
- BMW PBRT converter 材质保留 PBRT 参数并填充 metallic 等 extension 参数。
- 输出非全黑。
- rg/audit 证明 validation path 不读取旧 `MaterialUBO` 参数作为通过条件。

### T10: No Variant For Texture Presence

新增或增强 pipeline cache / render work 测试，证明“同一 pass 中常量 `Kd` 和贴图 `Kd`”不会创建不同 shader variant 或不同 pipeline desc。

## 修改范围

- `src/core/asset/material_instance.*`
- `src/core/asset/material_parameter_envelope.*`
- `src/core/asset/material_contract_*` 或等价通用 reflection/contract 模型
- `src/core/scene/scene_resource_table*`
- `src/core/scene/scene_gpu_records.*`
- `src/infra/material_loader/`
- `src/infra/offline/` 与 PBRT/BMW converter 相关文件
- `src/infra/shader_compiler/` 与 shader variant / reflection 相关文件
- `assets/shaders/glsl/common/material_surface.glsl` 或等价稳定 accessor ABI 头
- `assets/shaders/glsl/common/materials/*.contract.glsl`
- Forward / Deferred / OfflineRT PBR shader
- Helmet / BMW validation assets and tests
- 删除或 legacy rejection：`MaterialSurfaceSchema` 正向路径、旧共享 `SceneGpuMaterialRecord` 默认成功路径、旧 `MaterialUBO` material truth

## 边界与约束

- 本 REQ 只扩展 PBRT-style Material v3 参数合同，使其能覆盖 realtime PBR shader 需要的 metallic/factor/texture 输入；不解决完整 PBRT BSDF。
- 本 REQ 引入 source-reflected `MaterialSignature` 作为 pipeline 结构事实；它不是 workflow 字段，也不是材质实例数据副本。
- 默认纹理必须是 resource table / GPU upload 层的共享资源，不允许 material-local placeholder。
- 运行时不使用 workflow 字段选择 shader。
- 如果某 PBRT 材质无法被当前 realtime PBR path 准确表达，converter 必须输出 diagnostic，并保留原始 PBRT 参数；没有 supported `bsdf.source` 时加载失败，不得静默丢字段或伪装为准确材质。

## 依赖

- `REQ-072`: 071 closure audit 和 validation 修复。
- `REQ-049-a`: 旧 PBR IBL material contract 的当前 shader/IBL 背景。

## 后续工作

- `REQ-073-b`: Material storage and bindless upload foundation。
- `REQ-073-c`: RenderPath material source shader variants and URI migration。
- `REQ-073-d`: Indirect material batching and diagnostics。
- `REQ-073-e`: Realtime material path hard cut and smoke。
- `REQ-073-h`: Specialized PBRT BSDF contracts，专门支持完整 PBRT glass/fourier/mix 或 conductor `eta/k` 的 realtime/offline 表达。
- normal-map variant / tangent-free fast path 优化。
- `REQ-074-a`: BC7 texture compression pipeline。

## 实施状态

合同层已完成；GPU/bindless/indirect/realtime smoke 验证由 `REQ-073-b` 到 `REQ-073-e` 分段承接。

截至 2026-06-13，已落地：

- `.material` 的 `bsdf.source` 已成为显式 material contract source；runtime 不再按 `bsdf.type` 推断默认 source。
- `MaterialInstance` 保存 source URI、source reflection hash、source signature，并把 source signature 纳入 material / pipeline 结构签名路径。
- shader compiler 支持 `LX_MATERIAL_CONTRACT_SOURCE` source variant；PBR pass shader 通过 material accessor ABI include contract source。
- Forward / Deferred / OfflineRT PBR shader 已迁移到 `lxLoadMaterialSurface` / `LxMaterialSurface` 访问器，不再读取旧 `lxSceneMaterialRecord` 字段作为 PBR 参数真相。
- RenderPathGraph 解析阶段只解析 shader source descriptor；遇到需要 `LX_MATERIAL_CONTRACT_SOURCE` 的 shader 时登记为 `requiresMaterialSourceVariant`，不在没有 material source 的阶段编译。普通 source-resolved shader 若缺 compiled/reflected payload 仍然失败。
- glTF Helmet 和 PBRT BMW converter 输出路径显式写入并校验 `bsdf.source`。
- `MaterialSurfaceSchema` 已从 Material v3 正向路径删除，仅保留 legacy/audit 测试引用。
- Material Accessor ABI 已有负向 reflection 测试覆盖缺失 accessor、错误返回类型、错误参数、宏/注释/disabled preprocessor 伪装等失败路径。
- 同一 source signature 反射出不一致参数 schema 或 accessor ABI 时已有 invariant validation 测试覆盖；它必须诊断为 engine invariant violation，不能 fallback 或拆第二个 layout。
- `glass`、`fourier`、`mix` 当前没有准确 realtime contract，内置 contract source 标记为 unsupported，Material parser 必须加载期失败。

已移交到后续需求：

| 后续需求 | 移交内容 | 为什么不在 073-a 内完成 |
|---|---|---|
| `REQ-073-b` | source-reflected material storage ABI 的真实 GPU 上传、source-local material index、默认纹理 set、factor × texture material record、bindless-ready texture/material/object/draw/mesh table | 这些内容需要 SceneResourceTable upload view 和 GPU table 数据结构真实存在；在 073-a 的 parser/shader contract 阶段提前做只能得到局部假验证 |
| `REQ-073-c` | RenderPath material source shader variant、final shader reflection、shader URI 从 `techniques/...` 迁移到 `render_paths/...` | shader variant 需要依赖 073-a 的 source contract 和 073-b 的 source-local storage 结构事实，且应在 indirect batching 之前先稳定 pipeline/reflection identity |
| `REQ-073-d` | indirect material batching、index-only work item、batch split diagnostics 和 Helmet/BMW batching stats | indirect 依赖 bindless table 与 final shader/pipeline identity；提前做会混入旧 descriptor fallback，无法判断 batch split 原因 |
| `REQ-073-e` | realtime 旧 material/render fallback hard cut、Helmet/BMW realtime smoke / low-res visual validation | 视觉 smoke 应在数据、shader variant 和 indirect path 都成立后执行；无法渲染时必须 fail-fast 诊断，而不是用旧路径隐藏问题 |
| `REQ-073-f` / `REQ-073-g` | OfflineRT RenderPathGraph compute path 和配置化入口 hard cut | OfflineRT 配置入口与实时 material contract 是相邻但独立的执行路径；本阶段只保证 Offline PBR direct shader 能使用 accessor ABI |
| `REQ-073-h` | 完整 PBRT `glass`、`fourier`、`mix`、conductor `eta/k` 的物理准确表达 | 073-a 的目标是建立统一合同和 metallic realtime extension，不承诺完整 PBRT BSDF 物理模型 |

### 原始 073-a 中未在本 REQ 完成的条目

这些条目不是被丢弃，而是因为需要 073-b 之后的数据、shader、batch 或 smoke 基础，已经拆到后续节点：

| 原始条目 | 当前状态 | 未在 073-a 完成的原因 | 承接节点 |
|---|---|---|---|
| R5 / T6 / T7 中的 final shader variant、variant 后 shader reflection、compile/reflection key 和 `PipelineKey` 完整接入 | 只完成 contract source、accessor ABI、shader compiler 宏注入能力和 `requiresMaterialSourceVariant` 标记 | 073-a 阶段没有 source-local storage / backend table，也不能让 base shader reflection 伪装成最终 variant reflection | `REQ-073-c` |
| `techniques/...` 到 `render_paths/...` 的默认 URI 迁移 | 仍有默认 shader 源位于 `assets/shaders/glsl/techniques/...` | URI 迁移必须和 variant resolver、final shader reflection 一起做，否则会留下可解析但错误的 fallback | `REQ-073-c` |
| T3 / T4 / T5 中“shader 实际采样 factor × texture / 默认纹理后非全黑”的渲染结果验证 | 已完成 source record packing 和默认纹理 slot 基础；尚未做 realtime 默认路径视觉验收 | shader 采样正确性依赖 variant shader + bindless/indirect 默认路径，不能用旧 renderer path 证明 | `REQ-073-e` |
| T7 / T10 中“同 source 不因参数值或贴图存在性拆 pipeline/batch”的 renderer 级验证 | 已有 source signature / material signature 基础测试 | renderer 级 pipeline/batch 是否拆分取决于 RenderWorkQueue 和 geometry pass 默认消费新 table | `REQ-073-c` / `REQ-073-d` |
| T9 Helmet/BMW 低分辨率 realtime 非全黑 validation | Helmet/BMW 资产和 converter 已写入 `bsdf.source`；未做 realtime clean-path smoke | 视觉 smoke 必须在 data foundation、shader variant 和 indirect path 都成立后执行；否则旧 fallback 会隐藏问题 | `REQ-073-e` |
| OfflineRT 默认配置入口硬切 | Offline PBR direct shader 可使用 accessor ABI，但默认入口仍有 provider / hardcoded frame graph bridge | OfflineRT 配置入口是独立执行链，需要 RenderPathGraph compute path 先落地，再删除旧入口 | `REQ-073-f` / `REQ-073-g` |
| 完整 PBRT `glass` / `fourier` / `mix` / conductor `eta/k` 的准确表达 | 当前 contract 明确标记 unsupported 或只保留 PBRT 参数 | 073-a 是 metallic realtime extension，不承诺完整 PBRT BSDF 物理模型 | `REQ-073-h` |

本阶段验证：

- `cmake --build build --target test_render_resource_parsers test_scene_resource_abstraction`
- `./build/src/test/test_render_resource_parsers`
- `./build/src/test/test_scene_resource_abstraction`
- `./build/src/test/test_scene_resource_upload_view_v2`
- `ctest --test-dir build --output-on-failure -L auto -LE requires_video_device`
