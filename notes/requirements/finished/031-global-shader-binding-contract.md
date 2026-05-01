# REQ-031: 全局 Shader Binding 归属合同

## 背景

当前材质系统依赖一个隐式约定：`MaterialInstance` 通过名字精确等于 `"MaterialUBO"` 的 uniform block 来识别“这块 buffer 属于材质自己”。

这个约定的问题不只是命名不优雅，而是它把“资源归属”绑定在一个特例名字上：

- shader 反射能告诉我们 binding 的类型、set/binding、成员布局
- 但反射本身并不能区分“这份资源归 scene / object / material 谁所有”
- 现有实现用 `"MaterialUBO"` 这一个名字绕过了 ownership 问题

一旦要支持：

- 非 `MaterialUBO` 名字的材质参数块
- 多个材质-owned buffer
- storage buffer 等更多 descriptor 类型
- 更通用的 loader / template builder

这条单点约定就会成为结构性障碍。

同时，当前引擎里也确实存在一些应该由系统固定保留的 binding 名字，例如：

- `CameraUBO`
- `LightUBO`
- `Bones`

这些名字并不是材质参数，而是 scene/object 级资源接口。它们和“其余所有 binding 默认归材质”之间，需要一个正式合同。

这个方向也与常见引擎实践一致：

- Unity 的材质运行时接口按属性名工作，例如 `Material.SetTexture(...)`、`Material.SetConstantBuffer(...)`，而不是依赖一个特殊的 material block 名字。
- Unreal 的 material instance 按参数名覆盖默认值，也不使用单个特殊 block 名作为 ownership 判定。
- Godot 的低层 `RDUniform` 也是显式的 binding/type/resource 组合，ownership 由引擎合同和使用场景决定，而不是由某个 magic name 推导。

## 目标

1. 定义哪些 binding 名字是引擎保留的 system-owned 资源。
2. 定义材质 ownership 的默认规则，不再依赖 `"MaterialUBO"` 特例。
3. 为后续“反射驱动的通用材质接口”和通用材质资产文件提供稳定前提。

## 需求

### R1: 必须定义引擎保留的 system-owned binding 名字

首版必须正式保留且仅保留以下名字：

- `CameraUBO`
- `LightUBO`
- `Bones`

这些名字代表 engine/system-owned descriptor 资源，不属于 `MaterialInstance` 的直接拥有范围。

首版不得预先把 shadow map、environment、IBL 等名字加入保留集；如后续需要增加，必须通过新的 requirement/spec 显式扩展。

### R2: 非保留 binding 默认归材质所有

对于 shader 反射得到的 descriptor binding：

- 如果名字命中保留集，则按 system-owned 资源处理
- 否则默认视为 material-owned binding

首版不得再依赖 `"MaterialUBO"` 这个单点特例来判断材质 ownership。

### R3: 保留名字误用必须是 shader authoring error

如果某个 shader 把保留名字用于与系统语义不一致的用途，例如：

- 用 `CameraUBO` 表示材质参数
- 用 `Bones` 表示任意自定义 storage/uniform data

系统必须把它视为 authoring error，而不是默默按材质 binding 处理。

### R4: Push constant 约定与 descriptor 约定必须分开描述

- descriptor binding 名字归属合同
- push constant block 名字 / ABI 合同

必须分开定义，不能混成一条“所有 shader 接口命名规则”。

也就是说：

- `ObjectPC` 或后续等价 push constant block 名可以继续存在于 shader 合同里
- 但它不参与 material/system descriptor ownership 推导

### R5: 文档必须明确“system-owned names 是有限保留集”

文档必须强调：

- 保留集是少量、明确、可枚举的名字
- 不是“凡是看起来像 camera/light/bones 的都归系统”
- 新 system-owned binding 名字的增加，必须通过 requirement/spec 显式扩展

### R6: 外部 material asset 文件不得覆写 ownership 规则

后续若引入外部 material asset 文件，例如 `yaml`：

- 它可以声明默认值、默认资源、显示分组、实例化参数
- 它不得把某个 shader binding 从 system-owned 改成 material-owned
- 它也不得把某个非保留 binding 强行改成 system-owned

也就是说，ownership 的唯一规则来源仍然是：

- 本 REQ 定义的保留名字集
- shader reflection 给出的实际 binding 集

## 测试

- 一个 shader 反射出 `CameraUBO` / `LightUBO` / `Bones` 时，材质接口构建不得把它们纳入 material-owned slots
- 一个 shader 反射出 `SurfaceParams`、`albedoMap`、`normalMap` 这类非保留名字时，默认必须把它们视为 material-owned
- 若 shader 使用保留名字但类型/语义与系统合同冲突，系统必须给出明确失败路径，而不是静默接受
- 外部 material asset 文件若试图覆写 ownership，系统必须拒绝并报错

## 修改范围

- shader binding ownership 相关的 core 规则
- `SceneNode` / `Scene` 对 scene-owned 资源的识别路径
- `MaterialTemplate` / `MaterialInstance` 的 material-owned binding 推导规则
- `notes/subsystems/material-system.md`
- `notes/subsystems/scene.md`
- `openspec/specs/material-system/spec.md`
- `openspec/specs/scene-node-validation/spec.md`
- 后续 material asset 格式文档

## 边界与约束

- 本 REQ 只定义“名字归属合同”，不直接定义 `MaterialInstance` 的最终 API 形态
- 本 REQ 不直接定义外部配置文件格式
- 本 REQ 不要求 shader 改名，只要求 ownership 语义有正式规则

## 依赖

- 当前 shader reflection 能稳定提供 descriptor binding 名字与类型
- 当前 scene/object 路径已经把 `CameraUBO` / `LightUBO` / `Bones` 当作 system-owned 资源使用

## 后续工作

- [`REQ-032`](032-pass-aware-material-binding-interface.md)：基于本 REQ 的 ownership 规则，重做 pass-aware 材质接口，并纳入正式支持的 descriptor 类型范围
- [`REQ-033`](033-generic-material-asset-and-defaults.md)：定义通用 material asset、yaml 默认值和通用 loader 合同

## 实施状态

2026-04-24 已归档。

- `src/core/asset/shader_binding_ownership.hpp` 定义 `kSystemOwnedBindings = {"CameraUBO", "LightUBO", "Bones"}` 与 `isSystemOwnedBinding(...)` / `getExpectedTypeForSystemBinding(...)`
- `MaterialTemplate::rebuildMaterialInterface()` 在构造 canonical material binding 时过滤系统名字
- `MaterialInstance` 不再以 `"MaterialUBO"` 作 ownership 特例；非保留名字一律走 material-owned 路径
- 对应 spec：`openspec/specs/shader-binding-ownership/spec.md`
- R1–R6 全部完成
