# 06 材质排错

材质问题通常不是一个点坏了，而是 `.material`、contract metadata、shader variant、RenderPathGraph filter、scene override 之间某个环节没对上。我们像查账一样逐层核对。

## 快速诊断表

| 现象 | 优先检查 |
|---|---|
| material 加载失败 | `schema`、root allowlist、`bsdf.source`、contract `status` |
| 参数名报 unknown | YAML 参数名是否真的在 `.contract.glsl` 的 `parameter:` 行中 |
| texture 参数报错 | texture envelope 是否写了 `kind: texture`、`valueType` 和 `uri` |
| pass 不产出 draw | RenderPathGraph `filters.renderClass` / `filters.bsdf` 是否命中 |
| shader 编译失败 | pass shader 是否需要 `LX_MATERIAL_CONTRACT_SOURCE` variant |
| 物体全黑 | contract `lxLoadMaterialSurface` 是否填出有效 baseColor/normal/ao，light 输入是否存在 |
| 保存后参数丢失 | scene YAML 是否保存了 `materialOverrides` 或 `nodeMaterialOverrides` |
| pipeline key 不变但画面变了 | 普通参数值只改材质数据，不应触发 pipeline 重建 |

## 当前仓库里的工具

| 工具 | 用途 |
|---|---|
| `ninja test_material_v2_parser` | 验证 `.material v2` schema、root allowlist、envelope 和资源依赖 |
| `ninja test_material_source_contract` | 验证 contract metadata、storage packer、source signature |
| `ninja test_material_source_variant_pipeline` | 验证 graph + material source shader variant + pipeline identity |
| `ninja test_render_path_graph_pass_contract` | 验证 RenderPathGraph pass 必填字段、source/target 和 render state contract |
| `ninja test_default_material_asset_audit` | 验证默认 `.material` 资产符合当前 schema 和 root allowlist |

## 一步一步排查

先确认 material 文件指向真实 contract：

```bash
rg -n "bsdf:|type:|source:" assets/materials/gooch_demo.material
```

再确认 contract 声明了同名类型和参数：

```bash
rg -n "type: gooch|parameter:" assets/shaders/glsl/common/materials/gooch.contract.glsl
```

确认 graph filter 接受该类型：

```bash
rg -n "bsdf: .*gooch|gooch" assets/render_paths
```

然后跑针对性验证：

```bash
cd build
ninja test_material_v2_parser
ninja test_material_source_contract
ninja test_material_source_variant_pipeline
```

最后再进 editor 验证可视结果和 scene round-trip。

## 读诊断时先定位层级

| 诊断里出现 | 通常是哪一层 |
|---|---|
| `root.<field>` | `.material` 根字段不在 v2 allowlist |
| `bsdf.source` | contract 文件读不到、type/status 不匹配、metadata/ABI 不完整 |
| `bsdf.parameters.<name>` | 参数名未知、required 参数缺失、kind 不允许 |
| `LX_MATERIAL_CONTRACT_SOURCE` | pass shader 需要 source variant，但没有通过 resolver 编译 |
| `RenderPathNodeSignature` | graph pass contract 或 pipeline identity 层 |

## 我们已经学会了什么

材质排错要按链路查：YAML 能过 schema，contract 能反射，参数能进 envelope，资源能注册，graph 能命中，shader variant 能编译，scene 能保存覆盖。只看“画面变了没有”不足以判断材质接入是否完整。

## 下一步

继续 [自定义灯光](../custom-light/index.md)。
