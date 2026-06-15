# 创建与排错自定义材质

写自定义材质分两层：最常见的是“复用现有 contract，新建一份 `.material`”；更深入的是“新增一种 BSDF contract，并让 RenderPathGraph 的 pass 接受它”。材质文件负责 surface envelope；graph 文件负责 shader/pass/render state。

## 当前推荐路径

| 步骤 | 文件或 API | 目标 |
|---|---|---|
| 复用 contract 写 `.material` | `assets/materials/<name>.material` | 选择 supported `bsdf.type`，填写参数 envelope 和资源 URI |
| 新增 material contract | `assets/shaders/glsl/common/materials/<type>.contract.glsl` | 定义 BSDF 参数、storage/accessor ABI 和 shader 入口 |
| 写或复用 render path graph | `assets/render_paths/*.render-path.yaml` | 声明 pass、shader、source/target、geometry/attachment contract，并让 `input.material.type` 包含新类型 |
| 注册进场景 | scene document 或 editor runtime | 节点引用 mesh/material |
| 验证渲染 | `lxe_editor` 或集成测试 | 触发 scene/resource/graph validation 和 pipeline preload |

普通材质 authoring 不需要为每个材质写 C++。新增 BSDF contract 通常也先不需要 C++，因为当前 reflector 读取 `.contract.glsl` metadata；C++ 只在新增 envelope kind、资源类型、renderer 注入资源或新的 scene/upload ABI 时介入。

## 路径一：复用现有 contract

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
    normalTexture: { kind: texture, valueType: rgb, uri: assets://textures/helmet_normal.png }
```

这类自定义材质只改参数值或资源 URI。`standard-pbr` 适合 glTF/PBR 贴图链路；`matte`、`uber`、`metal`、`substrate` 适合 PBRT-style 参数 envelope。

如果材质需要透明 pass、deferred GBuffer 或 offline compute 支持，改的是 graph 和 shader contract。

## 路径二：新增一个 BSDF contract

新增类型时，我们先写一份 contract source。它的 metadata 是 parser 和 resolver 的入口：

```glsl
// LX_MATERIAL_CONTRACT_BEGIN
// type: gooch
// status: supported
// reflectionHash: gooch-source-contract-v1
// storageAbiHash: gooch-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: warmColor required rgb
// parameter: coolColor required rgb
// parameter: intensity optional float
// storageField: warmColor vec4 parameter warmColor value default=1,0.85,0.25,1
// storageField: coolColor vec4 parameter coolColor value default=0.15,0.25,1,1
// storageField: intensity float parameter intensity value default=1
// bsdfFunction: evaluate lxEvaluateBsdf
// bsdfFunction: sample lxSampleBsdf
// LX_MATERIAL_CONTRACT_END

#include "../material_surface.glsl"
#include "../material_bsdf.glsl"

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv,
                                        vec3 geometricNormal,
                                        mat3 tangentFrame) {
  LxMaterialSurface surface;
  surface.baseColor = vec3(1.0);
  surface.alpha = 1.0;
  surface.metallic = 0.0;
  surface.roughness = 0.5;
  surface.normal = normalize(geometricNormal);
  surface.ao = 1.0;
  surface.emissive = vec3(0.0);
  return surface;
}
```

这只是最小接入示例。真正要让 `warmColor/coolColor/intensity` 影响画面，还要在 contract shader 中定义 source record 读取逻辑，或者写一条专门的 pass shader 读取这些 storage field。当前 `standard_pbr.contract.glsl` 是更完整的读取模板。

对应 `.material`：

```yaml
schema: lxe.material.v2
renderClass: surface.opaque
bsdf:
  type: gooch
  source: assets://shaders/glsl/common/materials/gooch.contract.glsl
  parameters:
    warmColor: { kind: rgb, value: [1.0, 0.8, 0.25] }
    coolColor: { kind: rgb, value: [0.15, 0.25, 1.0] }
    intensity: { kind: float, value: 1.0 }
```

然后让 graph 接收这个类型：

## RenderPathGraph 决定怎样画

```yaml
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: Forward
    stage: raster
    dispatch: draw
    shader: render_paths/Forward/pbr
    input:
      kind: scene-renderables
      material:
        type: [standard-pbr, uber, metal, matte, gooch]
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
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
      blendEnable: false
```

这个 pass 通过 `input.material.type` 说明它支持哪些 material type，通过 `sources` 说明它需要 `material.bsdf`。如果 shader 需要某个材质 contract 的 specialized variant，variant 由 material source resolver 和 shader variant pipeline 处理，而不是 material YAML 直接塞宏。

如果 pass shader 包含 `LX_MATERIAL_CONTRACT_SOURCE`，它必须声明 `material.bsdf`；如果它不包含该宏，就不能声明 `material.bsdf`。这条一致性由 `MaterialSourceVariantResolver` 校验。

## 常见报错从这几类查

| 现象 | 先检查 |
|---|---|
| schema 不匹配 | `.material` 是否写了 `schema: lxe.material.v2` |
| unknown field | root 或 envelope 字段是否在当前 allowlist 中 |
| BSDF type 不匹配 | `bsdf.type` 是否等于 contract metadata 的 `type` |
| missing contract source | `bsdf.source` URI 是否能解析到 `.contract.glsl` |
| parameter kind mismatch | YAML 中的 `kind` 是否符合 `MaterialContractReflection` 允许的 kind |
| missing resource dependency | texture/spectrum/bsdf-table URI 是否真实注册，不能用空 payload 充数 |
| graph pass 不产出 draw | `input.material.type`、`input.object.renderClass`、visibility mask 是否匹配 material/object |
| pipeline key 不符合预期 | 检查 material type/source variant 和 RenderPathNode signature |
| shader 裸编译失败 | surface pass shader 是否需要 `LX_MATERIAL_CONTRACT_SOURCE`，应由 resolver 编译 variant |

## C++ 路径适合少数情况

| 场景 | 原因 |
|---|---|
| 新增 BSDF contract 类型 | 需要同步 contract metadata、reflection、packer 和测试 |
| 新增资源 envelope kind | 需要 `SceneResourceTable` 与 dependency registration 支持 |
| 新增 RenderPathGraph 字段 | 需要 parser allowlist、model 字段和 contract 测试一起更新 |
| 新增 renderer 注入资源 | 需要 shader binding ownership、upload view 和 backend descriptor 路径一起更新 |

## 最小验证命令

```bash
cd build
ninja test_render_resource_parsers
ninja test_material_source_variant_pipeline
```

如果改了 graph 字段，再跑：

```bash
ninja test_render_path_graph_pass_contract
```

## 我们已经学会了什么

自定义材质的稳定路径是：`.material v2` 写 surface envelope，`.contract.glsl` 写参数和 shader ABI，RenderPathGraph 写 pass/shader/render state，SceneResourceTable 管资源依赖。

## 下一步

- [从 .material 到 MaterialInstance](file-to-instance.md)
- [Material Contract v2](material-contract-v2.md)
- [多 Pass 材质怎样变成 RenderInput](pass-rendering-flow.md)
