# 自定义材质：让 shader、YAML 和 editor 对齐

材质像菜谱：`shader` 是烹饪方法，`.material` 是菜谱说明，`MaterialInstance` 是真正端上桌的一盘菜，节点级参数覆盖就是“这一盘多加一点盐”。这个系列会带我们写一个简单 Gooch 风格实验材质，并在 `lxe_editor` 里验证。

## 为什么材质不是一个 shader 文件

一个 shader 只能说明顶点和像素怎么算；材质还要说明它属于哪个 pass、需要哪些默认参数、绑定哪些资源、怎样进入 pipeline identity，以及 editor 如何把实例参数保存回 scene。缺少 `.material` 和 runtime instance，shader 就像只有做法、没有食材清单和上菜记录。

## 材质链路图

| 环节 | 作用 | 关键位置 |
|---|---|
| GLSL | 定义计算和 binding | `assets/shaders/glsl/*.vert|*.frag` |
| reflection | 从 SPIR-V 读出 shader 合同 | shader 编译与反射链路 |
| `.material` | 声明 pass、shader、默认参数和资源 | `assets/materials/*.material` |
| loader | 把 YAML 转成 runtime 对象 | `src/infra/material_loader/generic_material_loader.*` |
| instance | 保存当前参数和 texture | `src/core/asset/material_instance.hpp` |
| scene override | 记录某个节点的局部参数 | `.scene.yaml` 的 material override 字段 |

## 本系列会完成的 Gooch 材质闭环

| 章节 | 闭环中的位置 |
|---|---|
| [01 材质积木](01-material-building-blocks.md) | 先分清 shader、template、instance、override |
| [02 YAML 与 Shader 合同](02-material-yaml-and-shader-contract.md) | 对齐 `.material` 和 GLSL binding |
| [03 从 RTR 模板开始](03-start-from-rtr-template.md) | 复制现有实验模板，降低变量 |
| [04 Gooch Shader](04-write-gooch-shader.md) | 写冷暖色调的非真实感片元逻辑 |
| [05 在 editor 中验证](05-verify-in-editor.md) | 挂到节点、改参数、保存、重新加载 |
| [06 材质排错](06-debug-material-problems.md) | 按编译、反射、binding、参数和视觉链路排查 |

## 相关深入文档

- [材质系统概念](../../concepts/material/index.md)
- [材质系统总览](../../concepts/material/index.md)

## 下一步

进入 [01 材质积木](01-material-building-blocks.md)。
