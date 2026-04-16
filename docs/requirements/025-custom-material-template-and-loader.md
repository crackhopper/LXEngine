# REQ-025: 自定义材质模板与材质模板加载契约

## 背景

当前材质系统已经有 `MaterialTemplate`、`MaterialPassDefinition`、`MaterialInstance` 这三层模型，也已经有 `loadBlinnPhongMaterial()` 这样的具体 loader。但从使用者视角看，还缺一个统一问题的答案：

- 如果我要定义自己的材质模板，最小契约是什么？
- loader 应该返回 template、instance，还是两者都要暴露？
- template 级职责和 instance 级职责在哪里分界？

这导致概念层里可以讲“材质系统”，但还不能把“自定义材质模板”写成稳定入口。

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

loader 必须至少支持以下两种入口之一，并在文档中写死：

- 返回一个可复用的 `MaterialTemplate`
- 基于模板直接返回已经写入默认参数的 `MaterialInstance`

当 loader 只返回 instance 时，文档要明确 template 是否可追溯、是否允许复用。

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

- 示例可以是测试代码、sample 或 notes 文档片段
- 目标是验证“自定义模板”不是只存在于理论中的概念

## 修改范围

- `src/core/asset/material_template.hpp`
- `src/core/asset/material_pass_definition.hpp`
- `src/infra/material_loader/`
- `notes/concepts/material/`
- `notes/subsystems/material-system.md`

## 依赖

- [`REQ-022`](022-material-pass-selection.md)：instance 级 pass enable/disable
- `openspec/specs/material-system/spec.md`

## 实施状态

未开始。
