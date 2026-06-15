# 03 从现有 Contract 开始

我们先不新增 shader。最稳的自定义材质练习，是复制一份已经 supported 的 `.material`，只改参数和资源 URI。这样我们可以先证明 parser、contract、instance、graph input、editor round-trip 都工作，再考虑新增 BSDF 类型。

## 当前可以直接复用的起点

| 起点 | 适合练什么 | 注意 |
|---|---|---|
| `assets/scenes/generated/materials/damaged_helmet_standard_pbr.material` | `standard-pbr` + 多张 texture | 当前最完整的 texture/storage 示例 |
| `assets/materials/pbr.material` | `uber` envelope | 适合练习 PBRT-style 参数 |
| `assets/materials/pbr_gold.material` | `metal` spectrum envelope | 适合练习 spectrum 参数 |

如果目标是看贴图、normal、metallic/roughness 链路，优先从 `standard-pbr` 开始。

## 复制一份材质资产

```bash
cp assets/scenes/generated/materials/damaged_helmet_standard_pbr.material assets/materials/gooch_demo.material
```

先把 `gooch_demo.material` 保持为 `standard-pbr`，只改几个参数：

```yaml
schema: lxe.material.v2
renderClass: surface.opaque
bsdf:
  type: standard-pbr
  source: assets://shaders/glsl/common/materials/standard_pbr.contract.glsl
  parameters:
    baseColor: { kind: rgb, value: [0.95, 0.78, 0.28] }
    metallic: { kind: float, value: 0.0 }
    roughness: { kind: float, value: 0.55 }
    emissive: { kind: rgb, value: [0.0, 0.0, 0.0] }
    alphaMode: { kind: string, value: OPAQUE }
    alphaCutoff: { kind: float, value: 0.5 }
```

这一版不带 texture，便于先观察常量参数是否进入材质。需要贴图时，再把 `baseColorTexture`、`metallicRoughnessTexture`、`normalTexture` 等参数加回来，并确保 texture envelope 写了 `valueType`。

## 确认 graph 会消费它

Forward 和 Deferred 主 graph 的 surface pass 都包含：

```yaml
input:
  kind: scene-renderables
  material:
    type: [matte, uber, metal, substrate, standard-pbr]
    required: true
sources:
  - material.bsdf
```

因此 `standard-pbr` 材质会被当前 graph 命中。若我们把 `bsdf.type` 改成新类型，例如 `gooch`，也必须把 graph `input.material.type` 加上 `gooch`，否则材质能加载但 pass 不会为它生成 draw input。

## 先跑解析和 variant 测试

```bash
cd build
ninja test_material_v2_parser
ninja test_material_source_contract
ninja test_material_source_variant_pipeline
```

这三个测试分别覆盖 `.material v2` envelope、contract metadata / packer、以及 `LX_MATERIAL_CONTRACT_SOURCE` shader variant。

## 我们已经学会了什么

自定义材质的第一步不是写 shader，而是复用 supported contract 新建 `.material`。只要 schema、contract source、参数名、参数 kind 和 graph input 都对齐，材质就能进入当前渲染链路。

## 下一步

进入 [04 Gooch Contract](04-write-gooch-shader.md)。
