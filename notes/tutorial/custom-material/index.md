# 自定义材质：让 Contract、YAML、Graph 和 Editor 对齐

材质像一套生产单据：`.material` 写表面参数，`.contract.glsl` 写这些参数怎样变成 shader 能读的 surface，`RenderPathGraph` 写哪条 pass 使用它，`MaterialInstance` 是运行时账本。这个系列会先从现有 `standard-pbr` / PBRT-style contract 出发，再说明新增 Gooch 风格 contract 时哪些文件必须一起改。

## 为什么材质不是一个 shader 文件

一个 shader 只能说明某个 pass 里的顶点、像素或 compute 怎样算；材质还要说明 BSDF type、参数 envelope、资源 URI、contract source、怎样进入 material source variant，以及 editor 如何把实例参数保存回 scene。缺少 `.material` 和 runtime instance，shader 就像只有加工程序、没有零件单和运行记录。

## 材质链路图

| 环节 | 作用 | 关键位置 |
|---|---|---|
| `.material` | 声明 BSDF type、contract source、参数 envelope 和资源 URI | `assets/materials/*.material` |
| Material contract GLSL | 定义参数 metadata、storage ABI、`lxLoadMaterialSurface` 和 BSDF 函数 | `assets/shaders/glsl/common/materials/*.contract.glsl` |
| Material parser | 把 YAML + contract source 转成 runtime instance | `src/infra/material_loader/material_resource_parser.*` |
| `MaterialInstance` | 保存 BSDF type、source signature、参数 envelope、资源依赖 | `src/core/asset/material_instance.hpp` |
| RenderPathGraph | 选择 pass shader、source/target、render state，并用 `filters.bsdf` 匹配材质 | `assets/render_paths/*.render-path.yaml` |
| Source variant resolver | 给 pass shader 注入 `LX_MATERIAL_CONTRACT_SOURCE` | `src/infra/resource_parsers/material_source_variant_resolver.*` |
| scene override | 记录某个节点的局部参数 | scene YAML 的 material override 字段 |

## 本系列会完成的闭环

| 章节 | 闭环中的位置 |
|---|---|
| [01 材质积木](01-material-building-blocks.md) | 先分清 surface、contract、graph、instance、override |
| [02 YAML 与 Shader 合同](02-material-yaml-and-shader-contract.md) | 对齐 `.material`、contract metadata 和 shader ABI |
| [03 从现有 Contract 开始](03-start-from-existing-contract.md) | 复制当前 contract 的 `.material`，先让参数链路闭合 |
| [04 Gooch Contract](04-write-gooch-shader.md) | 看新增 BSDF contract 需要哪些 metadata、ABI 和 graph 配套 |
| [05 在 editor 中验证](05-verify-in-editor.md) | 挂到节点、改参数、保存、重新加载 |
| [06 材质排错](06-debug-material-problems.md) | 按编译、反射、binding、参数和视觉链路排查 |

## 相关深入文档

- [Material Contract v2](../../concepts/material/material-contract-v2.md)
- [从 .material 到 MaterialInstance](../../concepts/material/file-to-instance.md)
- [Shader 在材质中的角色](../../concepts/material/shader.md)

## 下一步

进入 [01 材质积木](01-material-building-blocks.md)。
