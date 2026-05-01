# REQ-025: 自定义材质模板与材质模板加载契约

## 背景

当前材质系统已有 `MaterialTemplate`、`MaterialPassDefinition`、`MaterialInstance` 三层模型，并已统一到 `LX_infra::loadGenericMaterial(.material)` 作为唯一 loader。任何材质都通过 `.material` YAML 资产 + shader reflection 进入运行时，不再存在任何 material-type-specific 的硬编码 loader。但从使用者视角看，还需要明确：

- 如果我要定义自己的材质模板，最小契约是什么？
- loader 的返回边界：template 与 instance 分别承担哪些职责？
- template 级职责和 instance 级职责在哪里分界？

本 REQ 负责把这套稳定入口写成规范，便于概念层直接引用“自定义材质模板”。

## 目标

1. 定义自定义 `MaterialTemplate` 的最小构造契约。
2. 定义 material loader 对外暴露 template / instance 的边界。
3. 让概念层可以稳定引用“自定义材质模板”和“加载材质模板”这两条能力。

## 需求

### R1: `MaterialTemplate` 继续作为蓝图对象

- `MaterialTemplate` 负责持有 `pass -> MaterialPassDefinition` 映射。
- 每个 pass 至少定义 shader set 与 render state。
- template 在本期仍按静态蓝图处理，不支持运行时结构性热修改。

### R2: loader 的最小返回语义要明确

正式 loader 入口为 `LX_infra::loadGenericMaterial(materialPath)`，由 `.material` YAML 资产驱动。文档要写死：

- 基于 YAML 中的 `shader` 字段构建可复用的 `MaterialTemplate`
- 直接返回已经写入默认参数与默认纹理的 `MaterialInstance`
- 调用方可通过 `MaterialInstance::getTemplate()` 反查 template 以便复用

不得引入 material-type-specific 的硬编码 loader。任何新材质必须通过 shader + `.material` YAML 上线。

### R3: 自定义模板的构造步骤要成为正式约定

概念和代码路径都需要稳定支持下列顺序：

1. 创建 `MaterialTemplate`
2. 为每个 pass 填 `MaterialPassDefinition`
3. 基于 template 创建 `MaterialInstance`
4. 写入运行时参数与纹理
5. 把 instance 交给 `SceneNode`

### R4: 文档必须说明 template / instance 的职责边界

- template 决定 pass、shader、render state、variant 上限
- instance 决定运行时参数、纹理、pass enable 子集
- loader 负责把外部配置或资产桥接到前两者

### R5: 至少提供一个非 BlinnPhong 的自定义模板示例

- 示例应为仓库内真实 `.material` 资产 + 对应 shader，不是 BlinnPhong 家族的变体
- 不允许通过新增 C++ 硬编码 loader 来“凑”示例；必须走通用 loader 路径
- 目标是验证“自定义模板”不是只存在于理论中的概念

## 修改范围

- `src/core/asset/material_template.hpp`
- `src/core/asset/material_pass_definition.hpp`
- `src/infra/material_loader/`
- `notes/concepts/material/`
- `notes/subsystems/material-system.md`

## 依赖

- [`REQ-022`](finished/022-material-pass-selection.md)：instance 级 pass enable/disable
- `openspec/specs/material-system/spec.md`

## 实施状态

2026-04-24 已归档。

- `MaterialTemplate` / `MaterialPassDefinition` / `MaterialInstance` 支持直接 C++ 组装
- `loadGenericMaterial(materialPath)` 是 `.material` 资产唯一正式 loader；无 material-type-specific C++ loader
- 非 BlinnPhong 真实示例：`assets/materials/pbr_gold.material` + `assets/shaders/glsl/pbr.{vert,frag}`，由 `test_generic_material_loader.cpp::test_pbr_example_material_loads` 覆盖
- 概念文档 `notes/concepts/material/custom-template.md` 与 `notes/subsystems/material-system.md` 已引用
- R1–R5 全部完成
