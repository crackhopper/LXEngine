# 02 YAML 与 Shader Contract

`.material` 和 shader contract 像一份订单和规格书：订单上写 `roughness`，规格书必须真的声明这个参数、允许它的 kind，并说明它怎样打包进 shader 能读取的 surface。任何一侧写错，loader 都应该失败。

## YAML 先指向 contract source

当前 `.material` 必须写 `bsdf.source`。Parser 会先加载这份 `.contract.glsl`，再知道哪些参数合法：

```yaml
schema: lxe.material.v2
renderClass: surface.opaque
bsdf:
  type: standard-pbr
  source: assets://shaders/glsl/common/materials/standard_pbr.contract.glsl
  parameters:
    baseColor: { kind: rgb, value: [0.8, 0.7, 0.4] }
    metallic: { kind: float, value: 1.0 }
    roughness: { kind: float, value: 0.35 }
    normalTexture:
      kind: texture
      valueType: rgb
      uri: assets://textures/helmet_normal.png
```

这里的 `normalTexture` 名字来自 `standard_pbr.contract.glsl`，不是随手起的别名。YAML 参数名必须和 contract metadata 中的 `parameter:` 行一致。

## Contract metadata 决定 YAML 是否有效

`standard_pbr.contract.glsl` 顶部有这样的 metadata：

```glsl
// LX_MATERIAL_CONTRACT_BEGIN
// type: standard-pbr
// status: supported
// parameter: baseColor optional rgb
// parameter: baseColorTexture optional texture
// parameter: roughness optional float
// parameter: normalTexture optional texture
// storageField: normalTexture textureSlot parameter normalTexture texture defaultTexture=flatNormal
// bsdfFunction: evaluate lxEvaluateBsdf
// bsdfFunction: sample lxSampleBsdf
// LX_MATERIAL_CONTRACT_END
```

`MaterialContractReflection` 会告诉引擎：

| 信息 | 例子 | 用途 |
|---|---|---|
| material type | `standard-pbr` | 必须等于 `.material` 的 `bsdf.type` |
| 参数名 | `baseColor` / `roughness` / `normalTexture` | YAML 只能写这些名字 |
| 参数 kind | `rgb` / `float` / `texture` | envelope kind 必须匹配 |
| storage field | `normalTexture textureSlot ...` | packer 知道怎样写 GPU record |
| accessor ABI | `lxLoadMaterialSurface` | pass shader 通过统一入口读取 surface |
| BSDF ABI | `lxEvaluateBsdf` / `lxSampleBsdf` | Forward/Deferred/OfflineRT shader 可调用 |

## Shader 仍然重要，但不在 material 里选择

pass shader 写在 RenderPathGraph：

```yaml
passes:
  - id: Forward
    shader: techniques/Forward/pbr
    sources:
      - material.bsdf
      - scene.camera
      - scene.lights
```

`techniques/Forward/pbr.frag` 会通过 `LX_MATERIAL_CONTRACT_SOURCE` include material contract。也就是说，`.material` 选择 contract source，RenderPathGraph 选择 pass shader，resolver 把两者编译成最终 shader variant。

## 常见错法

| 写法 | 为什么错 |
|---|---|
| `.material` 根上写 `shader: pbr` | shader 属于 RenderPathGraph pass |
| `.material` 根上写 `parameters:` | v2 参数必须在 `bsdf.parameters` 下面 |
| texture envelope 只写 `uri` | texture 必须写 `valueType: rgb` 或 `valueType: float` |
| `Kd: { kind: rgb, value: ..., uri: ... }` | inline value 和 resource URI 不能同时存在 |
| `bsdf.type: matte` 但 source 指向 `metal.contract.glsl` | contract declared type 与 material type 不一致 |

## 我们已经学会了什么

YAML 与 shader contract 是一份合同的两面。`.material` 写 surface envelope，`.contract.glsl` 写参数和 ABI，RenderPathGraph 选择 pass shader；名字、kind、type、ABI 不一致时必须失败。

## 下一步

进入 [03 从现有 Contract 开始](03-start-from-existing-contract.md)。
