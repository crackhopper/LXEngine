# 材质系统总览

几何告诉引擎"这个东西长什么形状"，材质告诉引擎"这个东西该怎么渲染"：用哪组 shader、跑哪些 pass、什么 render state、提供哪些参数和纹理。

一句话概括当前设计：

> `MaterialTemplate` 是菜谱——规定了一道菜要哪些步骤（pass）和食材类型（shader / variants / render state）。`MaterialInstance` 是上桌的那道菜——填入了具体的调料用量（参数值）和实际食材（纹理）。

## 核心对象

| 对象 | 职责 | 类比 |
|------|------|------|
| `MaterialTemplate` | 定义有哪些 pass，每个 pass 的 shader、variants、render state | 菜谱 |
| `MaterialPassDefinition` | 单个 pass 的完整配置（shader + render state + 反射 binding 缓存） | 菜谱里的一个步骤 |
| `MaterialInstance` | 运行时参数值、纹理资源、pass 开关 | 上桌的菜 |
| `ShaderProgramSet` | 把 shader 名、variants 和编译后的 shader 打包成一个值对象 | 步骤里标注的"用哪把刀、什么火候" |

另外三个支撑组件：

- **`shader_binding_ownership`** — 区分哪些 binding 归系统（`CameraUBO`、`LightUBO`、`Bones`），哪些归材质
- **`GenericMaterialLoader`** — 从 `.material` 直接创建完整材质，不用写 C++
- **`PlaceholderTextures`** — 内置 1×1 占位纹理（`white`、`black`、`normal`）

## 阅读顺序

1. [模板与 Pass：材质的结构定义](template-blueprint.md) — 先理解"菜谱"
2. [Shader 在材质中的角色](shader.md) — 理解反射、variants 和 binding 归属
3. [什么是 Pipeline](what-is-pipeline.md) — 先把“渲染结构的复用单位”理解清楚
4. [模板如何影响 Pipeline](template-and-pipeline.md) — 再看模板里的哪些部分会进入 pipeline
5. [MaterialInstance：运行时状态](material-instance.md) — 理解参数写入、资源绑定和 pass 开关
6. [创建自定义材质](custom-template.md) — YAML 路径和 C++ 路径

## 权威参考

- [`../../subsystems/material-system.md`](../../subsystems/material-system.md) — 实现层设计文档
- `openspec/specs/material-system/spec.md` — 材质系统 spec
- `openspec/specs/shader-binding-ownership/spec.md` — binding 归属 spec
- `openspec/specs/material-asset-loader/spec.md` — 通用 loader spec
