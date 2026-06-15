# 01 材质积木：surface 与 render path 为什么要分开

我们先不写代码，先建立当前材质系统的边界：shader 不是材质，材质也不是 pass。LXEngine 把三件事分开：`.material` 描述表面，`RenderPathGraph` 描述渲染步骤，shader 是某个步骤里执行的程序。

## 新人最容易混淆的边界

| 对象 | 类比 | 当前角色 |
|---|---|---|
| `.material` | 表面参数单 | 声明 BSDF type、参数 envelope 和材质资源 URI |
| `MaterialInstance` | 运行时账本 | 保存 BSDF type、source signature、参数 envelope、资源依赖 |
| `RenderPathGraph` | 工艺流程单 | 声明 pass、shader、source/target、attachment、render state |
| `RenderFeature` | 全局效果参数包 | tone mapping、shadow、post effect 等非物体参数 |
| Shader | 工艺步骤里的程序 | 消费 material/feature/scene 数据，产出像素或 compute 结果 |
| Mesh / vertex data | 原材料 | 提供顶点、法线、UV 等几何输入 |
| Scene node override | 局部改写 | 针对节点覆盖 surface 参数，不改 pass 或 shader |

真正的一帧画面需要这些单据一起生效：scene 节点引用 mesh 和 material；active RenderPathGraph 选择 pass；pass input 命中 material 的 BSDF type 和 object render class；shader 读取 material、feature 和 scene 数据；backend 用 `RenderInputDesc` 生成 pipeline 并执行 draw/dispatch。

## 当前材质文件只回答一个问题

`.material` 只回答“这块表面是什么”。一份最小 surface material 长这样：

```yaml
schema: lxe.material.v2
renderClass: surface.opaque
bsdf:
  type: standard-pbr
  source: assets://shaders/glsl/common/materials/standard_pbr.contract.glsl
  parameters:
    baseColor: { kind: rgb, value: [0.8, 0.7, 0.4] }
    metallic: { kind: float, value: 0.0 }
    roughness: { kind: float, value: 0.45 }
```

`bsdf.type` 选择材质类型，`bsdf.source` 指向 shader contract，`bsdf.parameters` 保存 typed envelope。pass、shader、attachment、geometry contract 和 render state 都在 RenderPathGraph 中声明。

## 读一个材质时的顺序

1. 打开 `.material`，确认 `schema: lxe.material.v2`、`bsdf.type` 和参数 envelope。
2. 打开 `bsdf.source` 指向的 `.contract.glsl`，看这个材质类型的参数和 accessor ABI。
3. 打开 active `assets/render_paths/*.render-path.yaml`，确认哪些 pass 的 `input.material.type` 会消费这个材质。
4. 打开 pass 的 shader，确认它如何读取 material surface 和 scene/feature 数据。
5. 在 editor 中切换 material URI，验证 scene 保存和重新加载。

| 路径 | 读它的原因 |
|---|---|
| `notes/concepts/material/index.md` | 从概念层理解 SurfaceMaterial / RenderPathGraph |
| `notes/concepts/material/file-to-instance.md` | 当前 `.material v2` 到 `MaterialInstance` 的链路 |
| `src/core/asset/material_instance.hpp` | instance 的 C++ 状态 |
| `src/infra/material_loader/material_resource_parser.*` | v2 material parser |
| `src/infra/resource_parsers/render_path_graph_resource_parser.*` | render path graph parser |

## 我们已经学会了什么

材质系统不是“shader 文件等于材质”。当前材质是一份 surface envelope；pass 和 shader 由 RenderPathGraph 提供；两者在 scene validation、source variant resolver 和 `RenderWorkCompiler` 里汇合。

## 下一步

进入 [02 YAML 与 Shader 合同](02-material-yaml-and-shader-contract.md)。
