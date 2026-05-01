# REQ-033: 通用材质资产与默认值合同

## 背景

仅靠 shader reflection 不足以支撑一个通用材质 authoring 流程。

reflection 稳定提供：

- 有哪些 pass
- 有哪些 binding
- 每个 binding 的 descriptor 类型
- buffer 成员布局与 offset

但 reflection 不回答：

- 这个材质实例的默认参数值是什么
- 默认纹理/立方体贴图资源是什么
- 哪些参数属于全局默认，哪些参数只在某个 pass 下覆写
- 新增一个材质模板时，如何避免每次都单独写一个定制 loader

如果继续把这些语义散落在代码里的特化 loader 中，材质系统虽能运行，但无法形成通用资产路径。

因此，在 [`REQ-031`](031-global-shader-binding-contract.md) 和 [`REQ-032`](032-pass-aware-material-binding-interface.md) 定义 ownership 与 runtime interface 之后，还需要一份外部 material asset 合同，用来描述默认值与 loader 契约。

## 目标

1. 为材质提供一个通用的外部资产格式，避免每新增模板都开发专用 loader。
2. 让默认参数值、默认资源引用脱离代码硬编码。
3. 保持 ownership 仍由 shader contract + reflection 决定，而不是被外部配置重写。
4. 为后续编辑器和资产管线提供稳定的 material authoring 入口。

## 需求

### R1: 首版必须定义统一的 material asset 文件格式

首版必须定义一个统一的 YAML 材质资产格式（`.material`）。

该格式至少要能表达：

- 所属 shader / material template
- shader 变体（全局 + pass 级合并）
- 全局默认参数值
- 全局默认资源引用
- pass 级 shader / renderState / 参数 / 资源覆写

首版不得再把“新增一个可配置材质模板”作为必须开发新 loader 的前提。

### R2: Material asset 文件不得参与 ownership 判定

material asset 文件可以描述：

- 默认值
- 默认资源
- pass 级覆写

但不得描述或覆写：

- 哪些 binding 属于 material-owned
- 哪些 binding 属于 system-owned

ownership 的唯一规则来源仍然是：

- [`REQ-031`](031-global-shader-binding-contract.md) 中的保留名字集
- shader reflection 的实际 binding 集

### R3: 参数与资源合法性必须由 shader reflection 决定

material asset 文件中出现的：

- parameter binding 名
- member 名
- texture / cube / buffer binding 名
- pass 名

都必须能在 shader reflection 与 material interface 中找到对应项。

- YAML 可以补充默认值
- 但不能把不存在的 binding/member 通过配置“声明出来”

### R4: 首版必须支持全局默认值与 pass 级覆写

material asset 文件必须支持两层默认值：

- 全局默认值
- `passes.<pass>` 下的局部覆写

合同：

- 不带 pass 的默认值用于所有 pass
- `passes.<pass>.parameters` 与 `passes.<pass>.resources` 可对局部值覆写

这与 `REQ-032` 中“同名 binding 可跨 pass 共存，但运行时解析必须显式带 pass”的合同保持一致。

### R5: 参数写入模型必须与 runtime API 对齐

material asset 文件中的 buffer 参数必须按：

- `bindingName.memberName`

表达，而不是只按 member 名。必须与 `REQ-032` 的 runtime API 语义一致：

- `MaterialInstance::setParameter(bindingName, memberName, value)`

首版不得定义与 runtime API 语义不一致的另一套参数命名规则。

### R6: 首版资源默认值只支持简单引用模型

首版 material asset 文件对资源默认值只要求支持：

- 资源路径（相对仓库 `assets/` / cwd）
- 内置占位符名字：`white` / `black` / `normal`

首版不要求支持：

- sampler 状态内联定义
- 复杂 import graph
- 运行时表达式

### R7: YAML 中的参数列举不是白名单约束

material asset 文件中的 `parameters` / `resources` 仅表示“此 asset 给出的默认值”，不是合法性白名单。

如果某个 binding/member 在 shader reflection 中存在，而 YAML 没列出来：

- 它仍然是合法的 material-owned slot
- 只是在当前 asset 中没有默认值

### R8: 通用 loader 必须成为首版正式路径

首版必须提供一个通用 material loader，实现以下流程：

1. 读取 YAML 材质资产
2. 按 pass 解析 shader / 变体 / renderState
3. 编译 shader 并读取 reflection
4. 根据 `REQ-031` / `REQ-032` 构建 material interface
5. 应用全局 + pass 级默认参数与默认资源
6. 生成 `MaterialTemplate` / `MaterialInstance`

系统不得再要求每种材质都编写专门的硬编码 loader。

## 测试

- 一个带 YAML 的材质资产可以在不编写专用 loader 的情况下实例化出可用材质
- YAML 中声明的参数或资源名字若不在 reflection 中存在，系统必须 `FATAL + terminate`
- 全局默认值作用到所有 pass，`passes.<pass>` 覆写仅影响对应 pass 的共享 buffer 状态
- YAML 中未列出的合法 shader 参数不会因此失效
- 默认资源可通过资源路径或内置占位符成功解析
- 不同 pass 可指定不同 shader 族，loader 正确编译并接入 template

## 修改范围

- `src/infra/material_loader/generic_material_loader.{hpp,cpp}`
- `src/core/asset/material_template.*`
- `src/core/asset/material_instance.*`
- `openspec/specs/material-asset-loader/spec.md`
- `notes/subsystems/material-system.md`
- `notes/subsystems/shader-system.md`

## 边界与约束

- 本 REQ 不改变 ownership 规则；ownership 由 [`REQ-031`](031-global-shader-binding-contract.md) 定义
- 本 REQ 不重新定义 runtime material interface；runtime 合同由 [`REQ-032`](032-pass-aware-material-binding-interface.md) 定义
- 首版只要求 YAML，不要求 editor、graph-based material authoring、schema 继承机制
- 首版不处理所有资源导入管线问题
- 首版不要求 authoring metadata（`displayName` / `group` / `exposed`）

## 依赖

- [`REQ-031`](031-global-shader-binding-contract.md)
- [`REQ-032`](032-pass-aware-material-binding-interface.md)
- `openspec/specs/material-system/spec.md`

## 后续工作

- Authoring metadata（`displayName` / `group` / `exposed`）可在编辑器 / UI 集成时另立 REQ 追加
- 更复杂的 sampler / render-state authoring 可独立扩展
- 真正的 material graph authoring 应在本 REQ 之上另立更高层资产 requirement

## 实施状态

2026-04-24 已落地。

- `src/infra/material_loader/generic_material_loader.cpp` 提供 `LX_infra::loadGenericMaterial(...)`，覆盖全局默认值 + `passes.<pass>` 覆写 + 多 pass shader override + 变体合并 + variant 规则校验
- 参数键采用 `bindingName.memberName` 与 `MaterialInstance::setParameter(...)` 对齐；YAML 声明按 reflection 校验 fail-fast
- 内置占位符纹理 `white` / `black` / `normal` 通过 `infra/texture_loader/placeholder_textures.hpp` 暴露
- ownership 仍由 `REQ-031` 保留名字集 + reflection 决定，YAML 不参与
- 对应 spec：`openspec/specs/material-asset-loader/spec.md`
- R1–R8 全部完成；authoring metadata 显式纳入“后续工作”
