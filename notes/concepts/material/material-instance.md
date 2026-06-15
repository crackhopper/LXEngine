# MaterialInstance：运行时材质账本

如果 `.material` 是表面材质说明书，`MaterialInstance` 就是这份说明书进入运行时后的账本。它不重新定义 pass，不决定 shader 编译，也不直接创建 pipeline；它记录 BSDF type、source contract、参数 envelope、资源依赖和材质状态版本。

## Instance 保存的状态

| 字段 | 当前职责 |
|---|---|
| `m_bsdfType` | 当前材质的 BSDF 类型，例如 `uber`、`standard-pbr` |
| `m_materialSourceUri` / `m_materialSourceSignature` | contract source 和 material-side pipeline identity 输入 |
| `m_materialSourceReflectionHash` | source reflection 的稳定校验信息 |
| `m_materialContractReflection` | 参数、storage ABI、accessor ABI 的反射结果 |
| `m_renderClass` / `m_tags` | render path input matching 和 authoring metadata |
| `m_materialEnvelopesByName` | `bsdf.parameters.*` 的 typed envelope |
| `m_materialDependencies` | texture、spectrum、bsdf table、material ref 等资源依赖 |
| `m_materialStateVersion` / `m_materialStateDirty` | 上传和验证可观察的版本/脏标记 |

这些字段共同组成 material-side runtime state。Graph pass、shader URI、attachment 和 render state 不在 `MaterialInstance` 里定义。

## 参数写入按 envelope 保存

当前 v2 material 的参数来自 YAML envelope：

```yaml
bsdf:
  parameters:
    baseColor: { kind: rgb, value: [0.8, 0.7, 0.4] }
    roughness: { kind: float, value: 0.35 }
    normalTexture: { kind: texture, valueType: rgb, uri: assets://textures/helmet_normal.png }
```

parser 会对照 `MaterialContractReflection` 校验参数名和 kind，然后调用 instance 保存 envelope。普通参数值不会改变 pipeline key；会影响的是 BSDF type、contract source signature 和 shader/material source variant。

## 资源依赖必须是真资源

`MaterialResourceDependency` 记录：

| 字段 | 含义 |
|---|---|
| `kind` | texture、spectrum、material ref、bsdf table 等 envelope kind |
| `uri` | material 文件中声明的资源 URI |
| `resourceHandle` | `SceneResourceTable` 中注册出的 typed handle |
| `parameterName` | 哪个 material 参数引用了该资源 |

这条依赖不能用空 payload 或 metadata-only entry 顶替。资源要么被解析/注册为真实 payload，要么加载或上传路径给出诊断。

## Pass 选择属于 RenderPathGraph

pass 是否存在、是否匹配某个 material，由 active `RenderPathGraph` 的 pass input contract 和 validation 决定。`MaterialInstance` 提供 `renderClass`、`bsdf.type`、source signature 和 envelope，`RenderWorkCompiler` 再把它与 graph pass 组合成 draw input 和 pipeline desc。

| 改动 | 是否改变 pipeline key | 是否改变 render input |
|---|---|---|
| 改 BSDF 参数值 | 否 | 否，只改变材质数据 |
| 改 texture resource URI | 通常否 | 否，只改变资源 handle |
| 改 BSDF type / contract source | 是 | 可能改变 shader variant |
| graph input 未匹配该 material | 否，key 本身不变 | 是，不为该 pass 产出 input |
| 改 RenderPathGraph pass shader/renderState/attachment | 是 | 可能改变 pipeline 和 pass 输出 |

## Scene 文件里的材质覆盖

scene 可以覆盖材质 envelope，覆盖目标是 contract 中声明的 v2 参数名：

```yaml
material:
  uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material
materialOverrides:
  roughness: { kind: float, value: 0.2 }
  baseColor: { kind: rgb, value: [0.9, 0.7, 0.45] }
```

覆盖仍然必须经过 contract 校验：参数要存在，kind 要匹配，资源 URI 要能解析。

## 我们已经学会了什么

`MaterialInstance` 是运行时账本。它保存 surface envelope、source signature 和资源依赖；pass、shader 和 render state 由 RenderPathGraph 提供。

## 下一步

- [从 .material 到 MaterialInstance](file-to-instance.md)
- [多 Pass 材质怎样变成 RenderInput](pass-rendering-flow.md)
- [MaterialInstance 源码分析](../../source_analysis/src/core/asset/material_instance.md)
