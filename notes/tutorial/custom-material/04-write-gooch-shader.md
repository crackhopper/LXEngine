# 04 Gooch Contract：新增一种 BSDF 类型

Gooch 风格材质像美术老师用两支彩笔做明暗：暗面偏冷，亮面偏暖。要把它接进当前 LXEngine，我们不能只写一个 fragment shader；我们要让材质系统知道“gooch 是一种 BSDF contract”，再让 RenderPathGraph 的 pass 接受它。

## 当前新增类型要改三处

| 文件 | 作用 |
|---|---|
| `assets/shaders/glsl/common/materials/gooch.contract.glsl` | 声明 `gooch` 参数、storage ABI、surface/BSDF 函数 |
| `assets/materials/gooch_demo.material` | 使用 `bsdf.type: gooch` 并填写参数 envelope |
| `assets/render_paths/*.render-path.yaml` | 在需要的 pass `filters.bsdf` 中加入 `gooch` |

如果我们要让 Gooch 有完全独立的光照公式，也可以新增 pass shader；但第一步通常先复用 `techniques/Forward/pbr`，让它 include `gooch.contract.glsl`，然后通过 `lxLoadMaterialSurface` / `lxEvaluateBsdf` 产出统一 surface。

## Contract metadata 先写清楚

```glsl
// LX_MATERIAL_CONTRACT_BEGIN
// type: gooch
// status: supported
// reflectionHash: gooch-source-contract-v1
// storageAbiHash: gooch-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: warmColor required rgb
// parameter: coolColor required rgb
// parameter: baseColor optional rgb
// parameter: intensity optional float
// storageField: warmColor vec4 parameter warmColor value default=1,0.85,0.25,1
// storageField: coolColor vec4 parameter coolColor value default=0.15,0.25,1,1
// storageField: baseColor vec4 parameter baseColor value default=1,1,1,1
// storageField: intensity float parameter intensity value default=1
// bsdfFunction: evaluate lxEvaluateBsdf
// bsdfFunction: sample lxSampleBsdf
// LX_MATERIAL_CONTRACT_END
```

这里的 metadata 会被 `MaterialContractReflector` 读取。`parameter` 决定 YAML 可以写哪些字段；`storageField` 决定这些参数怎样打包进 source-local material record；hash 参与 material source signature。

## Shader ABI 与参数读取

当前 pass shader 需要 contract 提供三个入口。Gooch 参数要真正影响画面，contract source 需要声明 source record，并用 `materialIndex` 找到本材质在 `SceneSourceMaterialRecords` 里的记录：

```glsl
#include "../material_surface.glsl"
#include "../material_bsdf.glsl"

struct LxSceneMaterialRefRecord {
  uint sourceStorageIndex;
  uint sourceLocalMaterialIndex;
  uint reserved0;
  uint reserved1;
};

struct LxGoochSourceRecord {
  vec4 warmColor;
  vec4 coolColor;
  vec4 baseColor;
  float intensity;
};

layout(std430, set = 0, binding = 12) readonly buffer SceneMaterialRefs {
  LxSceneMaterialRefRecord materialRefs[];
};

layout(std430, set = 0, binding = 13) readonly buffer SceneSourceMaterialRecords {
  LxGoochSourceRecord sourceMaterials[];
};

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv,
                                        vec3 geometricNormal,
                                        mat3 tangentFrame) {
  LxSceneMaterialRefRecord materialRef = materialRefs[materialIndex];
  LxGoochSourceRecord material =
      sourceMaterials[materialRef.sourceLocalMaterialIndex];

  vec3 n = dot(geometricNormal, geometricNormal) > 0.0
               ? normalize(geometricNormal)
               : vec3(0.0, 0.0, 1.0);
  float t = n.z * 0.5 + 0.5;
  vec3 goochColor =
      mix(material.coolColor.rgb, material.warmColor.rgb, t) *
      material.baseColor.rgb * material.intensity;

  LxMaterialSurface surface;
  surface.baseColor = goochColor;
  surface.alpha = 1.0;
  surface.metallic = 0.0;
  surface.roughness = 0.6;
  surface.normal = n;
  surface.ao = 1.0;
  surface.emissive = vec3(0.0);
  return surface;
}

LxBsdfEvaluateOutput lxEvaluateBsdf(LxBsdfEvaluateInput bsdfInput) {
  return lxEvaluateLambertLikeBsdf(bsdfInput);
}

LxBsdfSampleOutput lxSampleBsdf(LxBsdfSampleInput bsdfInput) {
  return lxSampleCosineHemisphereBsdf(bsdfInput);
}
```

## Gooch 公式放在哪里

Gooch 公式本身很小：

```glsl
float ndl = dot(normalize(N), normalize(L));
float t = ndl * 0.5 + 0.5;
vec3 color = mix(coolColor.rgb, warmColor.rgb, t) * baseColor.rgb;
```

上面的示例把公式放在 `lxLoadMaterialSurface(...)`，这样 Forward 和 Deferred surface shader 都能继续消费统一的 `LxMaterialSurface`。如果你要让 Gooch 拥有完全独立的光照模型，也可以把同一份 storage record 数据组织成专门的 BSDF 输入，再在 `lxEvaluateBsdf(...)` 中实现插值。

## 对应 material YAML

```yaml
schema: lxe.material.v2
renderClass: surface.opaque
bsdf:
  type: gooch
  source: assets://shaders/glsl/common/materials/gooch.contract.glsl
  parameters:
    warmColor: { kind: rgb, value: [1.0, 0.82, 0.25] }
    coolColor: { kind: rgb, value: [0.15, 0.25, 1.0] }
    baseColor: { kind: rgb, value: [0.8, 0.8, 0.75] }
    intensity: { kind: float, value: 1.0 }
```

## Graph filter 必须跟上

如果 `Forward` pass 仍然只接受 `standard-pbr`、`matte`、`uber`、`metal`、`substrate`，`gooch` material 会加载成功但不会被这个 pass 消费：

```yaml
filters:
  renderClass: [surface.opaque]
  bsdf: [matte, uber, metal, substrate, standard-pbr, gooch]
```

pass shader `techniques/Forward/pbr` 已经要求 `LX_MATERIAL_CONTRACT_SOURCE`，所以 `sources` 也必须保留 `material.bsdf`。

## 我们已经学会了什么

新增 Gooch 不只是写 fragment 公式。当前材质系统要求我们新增 contract metadata、实现 shader ABI、写 `.material` envelope，并让 RenderPathGraph pass filter 接受新 BSDF type。视觉公式最后才接到 contract storage / accessor 之后。

## 下一步

进入 [05 在 editor 中验证](05-verify-in-editor.md)。
