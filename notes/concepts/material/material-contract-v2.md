# Material Contract v2：材质定义、Contract 解析与 Pipeline 配套

材质 contract 像一张“零件规格表”：`.material` 写这一块表面需要哪些零件，`.contract.glsl` 写每个零件的类型、打包布局和 shader 入口，`RenderPathGraph` 再决定在哪条生产线上使用它。当前实现已经按这条边界运行。

## 当前材质定义只描述 Surface

当前 `.material` 文件只允许这些根字段：`schema`、`bsdf`、`renderClass`、`tags`、`metadata`。这个 allowlist 在 `MaterialResourceParser` 中直接校验，见 `src/infra/material_loader/material_resource_parser.cpp:107`。

```yaml
schema: lxe.material.v2
renderClass: surface.opaque
tags: [demo]
metadata:
  source: hand-authored
bsdf:
  type: standard-pbr
  source: assets://shaders/glsl/common/materials/standard_pbr.contract.glsl
  parameters:
    baseColor: { kind: rgb, value: [0.8, 0.7, 0.4] }
    metallic: { kind: float, value: 1.0 }
    roughness: { kind: float, value: 0.35 }
    baseColorTexture:
      kind: texture
      valueType: rgb
      uri: assets://models/damaged_helmet/Default_albedo.jpg
```

`bsdf.type` 选择材质类型；`bsdf.source` 指向 contract shader；`bsdf.parameters` 是 typed envelope。RenderPathGraph 负责 pass、shader、render state 和 targets。

## Envelope 字段和形状

Envelope 像每个参数的小表单。当前运行时只接收 `kind`、`value`、`uri`、`valueType` 四个字段；converter provenance 之类的来源信息不允许混进运行时 envelope，校验见 `src/infra/material_loader/material_resource_parser.cpp:78`。

| `kind` | 数据形态 | 示例 | 约束 |
|---|---|---|---|
| `float` | inline value | `roughness: { kind: float, value: 0.4 }` | 必须有 float `value` |
| `rgb` | inline 3 元数组 | `Kd: { kind: rgb, value: [0.8, 0.7, 0.6] }` | 必须有三个 float |
| `spectrum` | inline RGB 或资源 URI | `eta: { kind: spectrum, uri: spectra/copper_eta.spd }` | 可 inline，也可引用资源 |
| `texture` | resource URI | `normalTexture: { kind: texture, valueType: rgb, uri: textures/n.png }` | 必须有 `uri` 和 `valueType` |
| `integer` | inline value | `samples: { kind: integer, value: 16 }` | 当前用于 contract 扩展 |
| `bool` | inline value | `enabled: { kind: bool, value: true }` | 当前用于 contract 扩展 |
| `string` | inline value | `alphaMode: { kind: string, value: OPAQUE }` | `alphaMode` 会被 packer 转成 flags |
| `materialRef` | `.material` URI | `namedmaterial1: { kind: materialRef, uri: materials/a.material }` | 用于组合材质 contract |
| `bsdfTable` | BSDF table URI | `bsdffile: { kind: bsdfTable, uri: bsdf/fabric.bsdf }` | 用于查表型 BSDF contract |

`validateEnvelopeShape(...)` 还会拒绝“既有 inline value 又有 uri”的混合写法；texture 没有 `valueType` 也会失败，见 `src/core/asset/material_parameter_envelope.cpp:16`。

## Contract Shader 的 Metadata 块

`.contract.glsl` 的前半段是机器可读 metadata，后半段是 shader 可 include 的实现。`standard_pbr.contract.glsl` 的开头给出了完整例子：

```glsl
// LX_MATERIAL_CONTRACT_BEGIN
// type: standard-pbr
// status: supported
// reflectionHash: standard-pbr-source-contract-v1
// storageAbiHash: standard-pbr-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: baseColor optional rgb
// parameter: baseColorTexture optional texture
// storageField: baseColor vec4 parameter baseColor value default=1,1,1,1
// storageField: baseColorTexture textureSlot parameter baseColorTexture texture defaultTexture=white
// bsdfFunction: evaluate lxEvaluateBsdf
// bsdfFunction: sample lxSampleBsdf
// LX_MATERIAL_CONTRACT_END
```

`MaterialContractReflector` 不是完整 GLSL AST。它读取 `LX_MATERIAL_CONTRACT_BEGIN/END` 内的注释 metadata，解析 `type`、`status`、hash、parameter、storageField、BSDF function，然后再检查源码里是否真的定义了 ABI 入口，见 `src/infra/material_loader/material_contract_reflector.cpp:742`。

| Metadata | 作用 | 解析结果 |
|---|---|---|
| `type` | contract 声明的 BSDF 类型 | `MaterialContractReflection::declaredType` |
| `status` | 当前是否可加载 | `supported` 才能被 `.material` 接收 |
| `reflectionHash` | 参数/contract 版本 | 参与 source signature |
| `storageAbiHash` | packed record ABI 版本 | 参与 source signature |
| `accessorAbiHash` | `LxMaterialSurface` accessor ABI 版本 | 参与 source signature |
| `parameter` | 参数名、required/optional、允许的 kind | parser 校验 YAML envelope |
| `storageField` | 参数怎样打包进 GPU record | `MaterialContractPacker` 使用 |
| `bsdfFunction` | `lxEvaluateBsdf` / `lxSampleBsdf` 入口 | reflector 校验函数签名 |

ABI 入口现在固定为：

```glsl
LxMaterialSurface lxLoadMaterialSurface(
    uint materialIndex,
    vec2 uv,
    vec3 geometricNormal,
    mat3 tangentFrame);

LxBsdfEvaluateOutput lxEvaluateBsdf(LxBsdfEvaluateInput bsdfInput);
LxBsdfSampleOutput lxSampleBsdf(LxBsdfSampleInput bsdfInput);
```

`lxLoadMaterialSurface` 把每种 contract 映射到统一的 `LxMaterialSurface`：`baseColor`、`alpha`、`metallic`、`roughness`、`normal`、`ao`、`emissive`。Forward 和 Deferred shader 只消费这个统一 surface，不直接理解每个 PBRT 参数名。

## Parser 如何把文件变成 Instance

`MaterialResourceParser::parse(...)` 的执行顺序是：

| 步骤 | 代码事实 | 失败条件 |
|---|---|---|
| 读 YAML root | root 必须是 map，`schema` 必须是 `lxe.material.v2`，见 `src/infra/material_loader/material_resource_parser.cpp:437` | schema 错、root 不是 map |
| 校验 root allowlist | 只允许 `schema/bsdf/renderClass/tags/metadata`，见 `src/infra/material_loader/material_resource_parser.cpp:107` | 未知 root field |
| 读取 `bsdf.type/source` | `bsdf.source` 必须是非空 scalar，见 `src/infra/material_loader/material_resource_parser.cpp:474` | 没写 source、source 为空 |
| 加载并反射 contract | `loadAndReflectMaterialContractSource(...)` 读取 `assets://` 或文件路径 | 读不到文件、metadata 不完整、ABI 函数缺失 |
| 对齐 type/status | `contract.declaredType` 必须等于 `bsdf.type`，`status` 必须 supported，见 `src/infra/material_loader/material_resource_parser.cpp:503` | type/source 不一致、contract 不可加载 |
| 校验参数 | YAML 中未知参数会诊断；required 参数缺失会诊断；kind 必须在 contract allowlist，见 `src/infra/material_loader/material_resource_parser.cpp:545` | 参数名或 kind 不匹配 |
| 注册资源依赖 | texture/spectrum/materialRef/bsdfTable URI 解析成 `SceneResourceTable` dependency，见 `src/infra/material_loader/material_resource_parser.cpp:580` | URI shape 错、mix 子材质 header 不合法 |
| 创建 instance | 写入 BSDF type、source URI/signature、reflection hash、renderClass/tags/metadata/envelope | 有任何 diagnostics 时不返回 instance |

这条链路刻意 fail-fast。Parser 不会因为缺参数就偷偷补默认材质；参数必须由 contract metadata 和 envelope 共同定义。

## 当前内置材质类型

| 类型 | 状态 | contract 文件 | 当前用途 |
|---|---|---|---|
| `standard-pbr` | supported | `assets/shaders/glsl/common/materials/standard_pbr.contract.glsl` | glTF/PBR 主路径；会读取 packed source record、texture slot 和 `SceneTextures` |
| `matte` | supported | `assets/shaders/glsl/common/materials/matte.contract.glsl` | PBRT matte envelope |
| `uber` | supported | `assets/shaders/glsl/common/materials/uber.contract.glsl` | 通用 PBRT-style envelope；当前 `assets/materials/pbr.material` 使用它 |
| `metal` | supported | `assets/shaders/glsl/common/materials/metal.contract.glsl` | 金属 envelope；当前 `assets/materials/pbr_gold.material` 使用它 |
| `substrate` | supported | `assets/shaders/glsl/common/materials/substrate.contract.glsl` | 层状 diffuse/specular envelope |

## 几类材质文件示例

`standard-pbr` 适合带贴图资产：

```yaml
schema: lxe.material.v2
bsdf:
  type: standard-pbr
  source: assets://shaders/glsl/common/materials/standard_pbr.contract.glsl
  parameters:
    baseColor: { kind: rgb, value: [1.0, 1.0, 1.0] }
    metallic: { kind: float, value: 1.0 }
    roughness: { kind: float, value: 1.0 }
    alphaMode: { kind: string, value: OPAQUE }
    baseColorTexture: { kind: texture, valueType: rgb, uri: ../../../models/damaged_helmet/Default_albedo.jpg }
    metallicRoughnessTexture: { kind: texture, valueType: rgb, uri: ../../../models/damaged_helmet/Default_metalRoughness.jpg }
    normalTexture: { kind: texture, valueType: rgb, uri: ../../../models/damaged_helmet/Default_normal.jpg }
```

`matte` 适合哑光表面：

```yaml
schema: lxe.material.v2
bsdf:
  type: matte
  source: assets://shaders/glsl/common/materials/matte.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [0.8, 0.7, 0.6] }
    sigma: { kind: float, value: 0.0 }
```

`metal` 可以用 spectrum URI 或 inline spectrum：

```yaml
schema: lxe.material.v2
bsdf:
  type: metal
  source: assets://shaders/glsl/common/materials/metal.contract.glsl
  parameters:
    eta: { kind: spectrum, uri: spectra/copper_eta.spd }
    k: { kind: spectrum, uri: spectra/copper_k.spd }
    uroughness: { kind: float, value: 0.25 }
    vroughness: { kind: float, value: 0.25 }
```

`substrate` 适合“底色 + 高光层”的 PBRT-style 参数：

```yaml
schema: lxe.material.v2
bsdf:
  type: substrate
  source: assets://shaders/glsl/common/materials/substrate.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [0.4, 0.03, 0.03] }
    Ks: { kind: rgb, value: [0.3, 0.3, 0.3] }
    uroughness: { kind: float, value: 0.0005 }
    vroughness: { kind: float, value: 0.00051 }
```

## Shader 如何消费 Contract

Render pass shader 不在 `.material` 中选择。Forward PBR pass shader `assets/shaders/glsl/render_paths/Forward/pbr.frag` 明确要求 resolver 注入 material contract source：

```glsl
#if defined(LX_MATERIAL_CONTRACT_SOURCE)
#include LX_MATERIAL_CONTRACT_SOURCE
#else
#error LX_MATERIAL_CONTRACT_SOURCE must be defined by the material shader variant
#endif
```

这段代码在 `assets/shaders/glsl/render_paths/Forward/pbr.frag:7`。因此裸编译 `pbr.frag` 会失败；必须由 `MaterialSourceVariantResolver` 为每个 material type/source 生成 specialized variant。

进入 `main()` 后，shader 先调用 contract 提供的 accessor：

```glsl
LxMaterialSurface surface =
    lxLoadMaterialSurface(vMaterialRefIndex, vUV, geometricNormal, tangentFrame);
```

然后把统一 surface 转成 direct lighting / BSDF 输入，调用 `lxEvaluateBsdf(...)`，见 `assets/shaders/glsl/render_paths/Forward/pbr.frag`。Deferred GBuffer shader 走同样的 include/accessor 模式，只是把结果写入 GBuffer attachment。

## RenderPathGraph 怎样配套

`.material` 只说 surface，RenderPathGraph 说“在哪个 pass 消费 surface”。Forward 主 graph 的 `Forward` pass 这样声明：

```yaml
- id: Forward
  stage: raster
  dispatch: draw
  shader: render_paths/Forward/pbr
  input:
    kind: scene-renderables
    material:
      type: [matte, uber, metal, substrate, standard-pbr]
      required: true
    geometry:
      vertex: position-only
      topology: triangle-list
  sources:
    - geometry.vertex
    - geometry.index
    - material.bsdf
    - scene.camera
    - scene.lights
  targets: [hdr.color, depth.main]
```

`input.material.type` 决定这个 pass 是否接受某个 material type；`sources` 中出现 `material.bsdf` 表示 shader 需要材质 envelope/storage。`RenderPassNode` 还必须声明 `stage`、`dispatch`、`input`、`rendering.attachments` 和 `renderState`。

`MaterialSourceVariantResolver` 再做三件关键事：

1. 遍历 `SceneResourceTable` 中的 material instance，按 `bsdf.type + source URI + reflection hash + source signature` 分组。
2. 检查 pass shader 是否包含 `LX_MATERIAL_CONTRACT_SOURCE`；包含则 pass 必须声明 `material.bsdf`，不包含则不能声明 `material.bsdf`，见 `src/infra/resource_parsers/material_source_variant_resolver.cpp:344`。
3. 对 graph input 命中的每种 material source 编译 shader variant，并注册到 `SceneResourceTable`，见 `src/infra/resource_parsers/material_source_variant_resolver.cpp:386`。

这一步解释了为什么“新增材质类型”通常要同时改 contract、material 文件和 graph input：缺任何一环，pass 都不会获得正确的 shader variant。

## Pipeline Identity 的边界

Pipeline key 不看单个参数值。当前 key 只组合两部分：

```text
PipelineKey::build(materialTypeVariant, renderPathNodeSignature)
```

实现见 `src/core/pipeline/pipeline_key.cpp:5`。

| 输入 | 来源 | 包含什么 |
|---|---|---|
| `materialTypeVariant` | `MaterialInstance + ShaderProgramSet` | BSDF type、source contract、resolved shader variant |
| `renderPathNodeSignature` | `RenderPassNode` | pass id、shader URI、stage/dispatch、render state、rendering mode、geometry、source/target、attachment contract |

`getRenderPathNodeSignature(...)` 明确把 pass、shader、stage、dispatch、renderState、geometry、source、target、attachment 纳入签名，见 `src/core/asset/render_effect.cpp:43`。这也是为什么普通 `roughness` 改值不会重建 pipeline，而改 graph 的 shader/render state/attachment 会改变 pipeline identity。

## 自定义材质的当前路径

当前我们有两条稳定路径：

| 目标 | 改哪些文件 | 是否需要 C++ |
|---|---|---|
| 新增一份材质资产 | 新建 `assets/materials/*.material`，复用 supported contract | 不需要 |
| 新增一种 BSDF contract | 新建 `assets/shaders/glsl/common/materials/<type>.contract.glsl`，写 metadata 和 ABI 函数；把 graph `input.material.type` 加上 `<type>`；必要时新增或复用 pass shader | 通常不需要，除非新增 envelope kind、资源类型或 renderer 注入资源 |

新增 contract 时最小检查清单：

1. metadata 块必须有 `type/status/reflectionHash/storageAbiHash/accessorAbiHash/parameter`。
2. `status` 先写 `supported` 之前，要确认 `lxLoadMaterialSurface`、`lxEvaluateBsdf`、`lxSampleBsdf` 符合当前签名。
3. `storageField` 只能引用已声明 parameter，texture field 要写 `defaultTexture=white/black/flatNormal`。
4. 新 `.material` 的 `bsdf.type` 必须和 contract `type` 一致。
5. Graph pass 的 `input.material.type` 必须包含新 type；pass shader 如果 include contract source，`sources` 必须包含 `material.bsdf`。
6. 跑 `ninja test_render_resource_parsers test_material_source_variant_pipeline`，再用 `lxe_editor` 验证 scene 中 material URI、graph input 和 shader variant 都接通。

## 我们已经学会了什么

Material Contract v2 把三件事拆开：`.material` 定义 surface envelope，`.contract.glsl` 定义参数和 shader ABI，`RenderPathGraph` 定义 pass/shader/render state。Parser 负责 fail-fast 解析与资源依赖注册；resolver 负责把 material source 注入 pass shader variant；pipeline key 只看 material type/source variant 和 RenderPathNode signature。

## 下一步

- [从 .material 到 MaterialInstance](file-to-instance.md)
- [Shader 在材质中的角色](shader.md)
- [创建与排错自定义材质](custom-template.md)
