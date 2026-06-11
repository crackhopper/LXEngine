# Material Contract v2：SurfaceMaterial Pure Envelope 与 RenderPathGraph 分离

> 状态：设计草案，尚未实施。本页用于收敛 material v2 contract；当前可执行行为仍以 `src/` 和现有 current 概念页为准。

Material v2 的核心边界是：`SurfaceMaterial` 是 pure envelope，只描述表面/BSDF 参数和资源引用；`RenderPathGraph` 才描述 Forward、Deferred、OfflineRT 下有哪些 pass、shader、source/target 和 render state。我们不把 `baseColor / metallic / roughness` 当作唯一真相，而是把它们视作 PBRT-style surface material 的一种降级视图。

## 新命名

| 名称 | 含义 |
|---|---|
| `SurfaceMaterial` | 应用于物体表面的 pure envelope，包含 BSDF 参数、typed resource URI、render class / tag |
| `RenderFeature` | shadowmap、SSAO、GI、tone mapping 等算法/效果的 pure envelope，包含 feature 参数 |
| `FeatureEnvelope` | RenderFeature 的参数表，和 material envelope 一样允许常量与资源引用 |
| `RenderPath` | Forward、Deferred、OfflineRT 等顶层渲染框架 |
| `RenderPathGraph` | 某个 RenderPath 的 pass DAG，声明 pass、shader、输入输出、依赖资源和 render state |
| `RenderPassNode` | RenderPathGraph 中的单个 pass 节点 |
| `RenderClass` | `surface.opaque`、`surface.transparent` 等分类标签，供 pass filter 使用 |

因此 `.material` 文件不写 `defaultTechnique`、`techniques`、`passes`、`shader`、`renderState`、`targets` 或 `sources`。这些字段只能出现在 RenderPathGraph。

## 主模型是 PBRT Surface Material

材质参数层以 PBRT surface material 语义为主模型。体渲染暂不进入本轮 contract。

| PBRT type | Material v2 语义 | 主要参数 | BMW-M6 用途 |
|---|---|---|---|
| `matte` | 哑光漫反射；`sigma = 0` 时是 Lambert，`sigma > 0` 时是 Oren-Nayar | `Kd`, `sigma` | logo 颜色、matte floor |
| `glass` | 透明 dielectric；使用 Fresnel、透射和折射率 | `Kr`, `Kt`, `eta`, roughness | 车窗、大灯玻璃 |
| `uber` | 通用绝缘体；diffuse + specular + optional transmission | `Kd`, `Ks`, `Kt`, `roughness`, `eta`, `opacity` | 大灯、刹车、轮胎、密封条 |
| `metal` | conductor；用复折射率计算金属 Fresnel | `eta`, `k`, roughness | 铝制 logo、轮毂、镀铬件 |
| `substrate` | diffuse substrate + glossy/specular lobe 的层状模型 | `Kd`, `Ks`, `uroughness`, `vroughness` | 车漆、碳纤维、地面 |
| `fourier` | 从 `.bsdf` 文件读取测量/预计算 BSDF | `bsdffile` | 皮革 |
| `mix` | 混合两个 named material | `namedmaterial1`, `namedmaterial2`, `amount` | 黑白皮革混合 |

文件层和内部类名都应尽量对齐这些 PBRT 名称，例如 `MatteSurfaceMaterial`、`GlassSurfaceMaterial`、`UberSurfaceMaterial`、`MetalSurfaceMaterial`、`SubstrateSurfaceMaterial`、`FourierSurfaceMaterial`、`MixSurfaceMaterial`。这样从 PBRT 源文件、转换器输出、运行时代码到 shader 调试都能保持同一套术语。

## 参数 Envelope

所有 BSDF 参数都使用统一 envelope，不写裸 YAML 值。参数可以是常量，也可以直接引用 texture、spectrum、BSDF table 或其他 material。

```yaml
schema: lxe.material.v2
name: CarPaint
renderClass: surface.opaque

bsdf:
  type: substrate
  parameters:
    Kd:                         # -> SurfaceMaterialInstance 参数
      kind: rgb                 # -> PBRT-style parameter kind
      value: [0.4, 0.03, 0.03]
    Ks:
      kind: rgb
      value: [0.3, 0.3, 0.3]
    uroughness:
      kind: float
      value: 0.0005
    vroughness:
      kind: float
      value: 0.00051
```

| Envelope 字段 | 含义 |
|---|---|
| `kind` | PBRT 风格参数类型，例如 `float`、`rgb`、`spectrum`、`bool`、`string`、`texture`、`integer` |
| `value` | 内联常量值 |
| `uri` | 参数直接引用的资源路径，例如 texture、SPD、BSDF table |
| `valueType` | texture 这类资源参数的逻辑值类型，例如 `rgb`、`float` |
| `renderClass` | 可选分类标签，例如 `surface.opaque` / `surface.transparent`；不是 shader 绑定 |

资源引用采用参数直接持有 URI 的形式：

```yaml
bsdf:
  type: metal
  parameters:
    eta:
      kind: spectrum
      uri: spds/Al.eta.spd
    k:
      kind: spectrum
      uri: spds/Al.k.spd
```

`MaterialResourceParser` 解析这些 URI，并通过 `SceneResourceTable` 注册 spectrum、texture、bsdf-table 等依赖资源。Parser 不拥有资源，资源身份和生命周期由 `SceneResourceTable` 管理。

## PBRT 默认值由 Converter 显式写入

`MaterialResourceParser` 不补默认值，只校验 `.material` 是否完整。PBRT converter 负责读取默认配置文件，把 PBRT 默认值显式写入 material。

```yaml
# pbrt-defaults.yaml
glass:
  Kr: { kind: rgb, value: [1, 1, 1] }
  Kt: { kind: rgb, value: [1, 1, 1] }
  eta: { kind: float, value: 1.5 }
  uroughness: { kind: float, value: 0.0 }
  vroughness: { kind: float, value: 0.0 }
```

转换规则保持简单：

| 情况 | 行为 |
|---|---|
| PBRT 源材质显式提供参数 | 使用源参数，并在 converter report 中记录来源 |
| 源材质缺参数，默认配置提供参数 | 使用配置值，打印 warning，并在 converter report 中记录来源 |
| 源材质和默认配置都缺参数 | converter fatal |

默认值不硬编码在转换器代码中。配置缺失时不做更多推导。

## SurfaceMaterial、RenderPathGraph 与 Instance 的边界

Material v2 中，`SurfaceMaterialTemplate` 不再表示 pass 结构。我们把“材质参数 contract”和“渲染流程 contract”拆开。

| 对象 | 职责 | 不负责 |
|---|---|---|
| `SurfaceMaterialTemplate` | 某个 BSDF type 的参数 schema、资源槽、CPU/GPU material data layout、校验规则 | render path、pass、shader、render state |
| `SurfaceMaterial` | `.material` 中的 pure envelope、render class/tag、资源 URI | shader、pass、RenderPathGraph |
| `MaterialInstance` | 某份 material 文件或 scene override 后的参数值、资源 handle、template 引用、dirty/version | 改写 template、base material 或 pass graph |
| `RenderFeature` | 算法/效果参数 envelope，例如 shadowmap 分辨率、TAA jitter、tone mapping 参数 | pass、shader、phase、render state |
| `RenderPathGraph` | render path 下的 pass DAG、shader、source/target、feature/material 依赖、render state | BSDF 参数默认值 |
| `SurfaceMaterialResourceParser` | 解析 `.material`，校验 envelope，注册依赖资源，创建 base instance | 持有资源、隐式补默认值、绑定 shader |

同一个 `MatteMaterialTemplate` 可以服务多个 material instance；这些 instance 的 `Kd` 不同，但参数 contract 相同。

## RenderPathGraph 显式绑定 Pass 与 Shader

Forward、Deferred、OfflineRT 不再写进 material。它们是 `RenderPath`，由 renderer/camera 选择；对应 pass graph 写在 `RenderPathGraph` 文件中。

```yaml
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward

features:
  shadow:
    uri: effects/shadow.render-feature.yaml

passes:
  - id: ForwardOpaque
    shader: techniques/Forward/surface_lit
    stage: raster
    dispatch: draw
    filters:
      renderClass: [surface.opaque]
      bsdf: [matte, uber, metal, substrate]
    sources: [geometry.vertex, geometry.index, material.bsdf, scene.camera, scene.lights, shadow.main]
    targets: [hdr.color.direct, depth.main]
    renderState:
      cullMode: None
      depthTest: true
      depthWrite: true
      blendEnable: false
```

所有影响结果的字段都必须显式写出。缺 `shader`、`stage`、`dispatch`、`sources`、`targets`、完整 `renderState` 时，graph validation fail-fast。代码不根据 pass 名称隐式推导 shader、target、render state 或 render path。

## RenderFeature 也是 Pure Envelope

不绑定具体物体的算法参数不放在 material 中。我们用 `RenderFeature` 文件表达后处理、阴影、GI、TAA 等参数。它和 SurfaceMaterial 一样不绑定 shader；真正的 pass 仍由 RenderPathGraph 声明。

```yaml
schema: lxe.render-feature.v1
name: MainShadow
feature: shadowmap
parameters:
  resolution:
    kind: integer
    value: 2048
  bias:
    kind: float
    value: 0.001
```

camera 节点引用 render path graph 和 feature：

```yaml
camera:
  renderPath: Forward
  renderPathGraph:
    uri: render_paths/forward_main.render-path.yaml
  features:
    shadow:
      uri: effects/shadow.render-feature.yaml
```

Forward 可以是单 pass，也可以为了 shadow/transparent/tone mapping 拆成多 pass；Deferred 是否需要 GBuffer 也由 RenderPathGraph 声明。FrameGraph 只看 graph 里的 source/target 依赖，不从 RenderPath 名称硬推结构。

## 标准 Graph Target

`sources` 和 `targets` 不是自由字符串，必须来自标准 graph resource registry。新增 target namespace 需要先进入 contract。

| Target | 用途 |
|---|---|
| `depth.main` | 主深度 |
| `gbuffer.albedo` | Deferred base color / diffuse data |
| `gbuffer.normal` | Deferred normal / roughness data |
| `gbuffer.material` | material id、BSDF type、参数索引等 |
| `gbuffer.emissive` | emissive 输出 |
| `hdr.color` | linear HDR color |
| `ldr.color` | tone mapped LDR color |
| `swapchain.color` | present target |
| `shadow.main` | shadow map |
| `environment.radiance` | 环境光照输入 |

如果 pass source 没有 producer，也不是 imported resource，FrameGraph 构建失败。多个 pass 写同一个 target 时，必须由 render graph rule 显式允许 blend、append 或 overwrite，否则报错。

## Scene Override 只影响 Instance

scene/node override 只允许覆盖 `bsdf.parameters.*` 和 instance 运行时状态。它不能修改 `RenderPathGraph`、`passes`、`shader`、`stage`、`renderState` 或 `variants`。

```yaml
material:
  uri: materials/CarPaint.material
  overrides:
    bsdf.parameters.Kd:
      kind: rgb
      value: [0.5, 0.02, 0.02]
      source: scene-override
```

这样 `.material` 文件仍对应稳定的 authoring asset；editor 临时调材质只影响 `MaterialInstance`，不回写原始 material YAML。

## 待继续收敛

| 问题 | 当前倾向 |
|---|---|
| material-local intermediate target | 第一版先不支持；如果 layered material 需要多 pass，在 RenderPathGraph 引入局部 target 作用域 |
| Deferred lighting pass 粒度 | 按 `SurfaceMaterialTemplate` / BSDF type 组织 lighting work，而不是每个 mesh 重复光照 |
| shader 目录 | 现有 `techniques/<Forward>` 目录作为过渡可保留，但语义是 RenderPath 目录；公用 BSDF 和 direct lighting 函数放 `common/` |
| render path graph validation | 顶层校验一个 RenderPathGraph 下所有 pass 的 shader reflection、source/target、layout 是否能被 pipeline 自动创建 |
| 缺失 graph 支持 | 对象不渲染，打印 warning；converter 不再生成 Forward/Deferred/OfflineRT material technique |

## 继续阅读

- [材质系统未来路线](future-roadmap.md)
- [从 .material 到 MaterialInstance](file-to-instance.md)
- `docs/superpowers/specs/` 中后续落地的材质设计 spec
