# 从 .material 到 MaterialInstance

`.material` 文件现在是一张表面材质说明书。它不告诉 renderer “用哪个 pass 画”，也不指定 shader；它只描述 BSDF type、参数 envelope 和材质自己依赖的资源。真正的 pass 和 shader 来自 `RenderPathGraph`。

## 一份当前可用的 .material

```yaml
schema: lxe.material.v2
bsdf:
  type: uber
  source: assets://shaders/glsl/common/materials/uber.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    Ks: { kind: rgb, value: [0.04, 0.04, 0.04] }
    eta: { kind: float, value: 1.5 }
    uroughness: { kind: float, value: 0.5 }
    vroughness: { kind: float, value: 0.5 }
```

这份文件的重点是 `schema: lxe.material.v2`。根字段只描述 surface contract；pass、shader、source/target 和 render state 由 RenderPathGraph 提供。

## Loader 的执行顺序

| 步骤 | 代码行为 | 产物 |
|---|---|---|
| 解析 YAML | root 必须是 map，`schema` 必须是 `lxe.material.v2` | parser 内部模型 |
| 校验 root allowlist | 只允许 `schema`、`bsdf`、`renderClass`、`tags`、`metadata` | root field diagnostics |
| 读取 BSDF header | 读取 `bsdf.type` 和 `bsdf.source` | material type + contract URI |
| 反射材质 contract | 通过 `MaterialContractReflection` 得到参数、storage ABI、accessor ABI | `MaterialContractReflection` |
| 对齐 type/status | contract `declaredType` 必须等于 `bsdf.type`，`status` 必须 supported | fail-fast |
| 校验参数 envelope | 每个参数必须存在于 contract，required 参数必须写出，kind 必须在 allowlist | fail-fast 或继续收集诊断 |
| 注册依赖资源 | texture、spectrum、bsdf table、material ref 等 URI 进入 `SceneResourceTable` 依赖图 | typed resource handles |
| 创建 instance | 构造 `MaterialInstance`，写入 bsdf type、source URI/signature、reflection hash | runtime material |
| 写入 envelope | `setMaterialEnvelope(...)` 保存参数 envelope | material state dirty/version |

这条链路的关键是：`.material` 不直接描述 Vulkan descriptor set，也不直接描述 pipeline。它描述 surface contract；渲染路径再决定这份 surface contract 被哪个 pass 消费。

代码入口集中在 `src/infra/material_loader/material_resource_parser.cpp:437`。其中 root allowlist 在 `src/infra/material_loader/material_resource_parser.cpp:107`，contract source 加载和 type/status 校验在 `src/infra/material_loader/material_resource_parser.cpp:474`，资源依赖注册在 `src/infra/material_loader/material_resource_parser.cpp:580`。

## Contract source 是必须字段

`bsdf.source` 缺失会直接失败，因为 parser 必须先读 `.contract.glsl` 才知道参数名、required/optional、允许的 kind 和 storage ABI。

```yaml
schema: lxe.material.v2
bsdf:
  type: matte
  parameters:
    Kd: { kind: rgb, value: [0.8, 0.7, 0.6] }
    sigma: { kind: float, value: 0.0 }
```

这份文件缺 `bsdf.source`，parser 无法反射参数和 storage ABI。正确写法是：

```yaml
schema: lxe.material.v2
bsdf:
  type: matte
  source: assets://shaders/glsl/common/materials/matte.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [0.8, 0.7, 0.6] }
    sigma: { kind: float, value: 0.0 }
```

## RenderPathGraph 负责 pass 和 shader

Forward 的 pass 结构写在 `assets/render_paths/forward_main.render-path.yaml`：

```yaml
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward

passes:
  - id: Forward
    stage: raster
    dispatch: draw
    shader: techniques/Forward/pbr
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
        - target: depth.main
          format: D32Float
          depth: true
    sources:
      - geometry.vertex
      - geometry.index
      - material.bsdf
      - scene.camera
      - scene.lights
    targets: [hdr.color, depth.main]
```

这里的 `material.bsdf` 是 graph 对材质 envelope 的依赖声明。graph 也显式声明 attachment contract、geometry contract、render state 和 shader URI，所以 renderer 不需要从材质文件里推导 Forward/Deferred/OfflineRT 结构。

## MaterialInstance 保存什么

| 数据 | 当前位置 |
|---|---|
| BSDF type | `MaterialInstance::m_bsdfType` |
| material source URI/signature/reflection hash | `m_materialSourceUri` / `m_materialSourceSignature` / `m_materialSourceReflectionHash` |
| contract reflection | `m_materialContractReflection` |
| render class / authoring tags | `m_renderClass` / `m_tags` |
| 参数 envelope | `m_materialEnvelopesByName` |
| texture/spectrum/bsdf-table 等依赖 | `m_materialDependencies` |
| 非 surface shader binding buffer | `m_parameterBuffersByName`，只给 post/procedural 等非 surface 资源路径使用 |

## Envelope 字段保持运行时形态

运行时 envelope 只接收渲染所需的字段：

| 字段 | 含义 |
|---|---|
| `kind` | 参数类型，例如 `float`、`rgb`、`texture`、`spectrum` |
| `value` | inline 参数值 |
| `uri` | texture、spectrum、material ref 或 BSDF table 资源 URI |
| `valueType` | texture 参数的采样值类型，例如 `rgb` 或 `float` |

转换器、导入器或调试工具产生的 provenance 信息应写在 report、manifest 或 metadata 中，不写进参数 envelope：

```yaml
Kd: { kind: rgb, value: [0.8, 0.7, 0.6], source: explicit }
```

`source` 这样的字段不是 runtime envelope 字段。这样可以保证 `.material` 的参数段只表示渲染输入，而不是导入过程。

## 我们已经学会了什么

`.material` 现在只回答“表面是什么”。它变成 `MaterialInstance` 后保存 BSDF envelope、source signature 和资源依赖；RenderPathGraph 再把这份 surface data 接到具体 pass、shader 和 target contract 上。

## 下一步

- [Material Contract v2](material-contract-v2.md)
- [多 Pass 材质怎样变成 RenderWork](pass-rendering-flow.md)
- [创建与排错自定义材质](custom-template.md)
