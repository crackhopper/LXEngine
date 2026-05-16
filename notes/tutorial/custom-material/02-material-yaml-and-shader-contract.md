# 02 YAML 与 Shader 合同

`.material` 和 GLSL 之间像一份点菜单和厨房备料单：菜单上写了 `warmColor`，厨房里也必须真的有这个参数，否则服务员找不到该把值送到哪里。

## YAML 和 shader 必须签同一份合同

Shader reflection 会读取 SPIR-V，告诉引擎：

| 信息 | 例子 |
|---|---|
| uniform block 名 | `MaterialUBO` / `GoochParams` |
| block 内字段 | `warmColor` / `coolColor` |
| descriptor 名 | `SceneLightsUBO` |
| vertex input | `inPosition` / `inNormal` |

`.material` 的职责是把这些名字填上默认值。

## 合同两端在仓库里的位置

| 文件 | 作用 |
|---|---|
| `assets/materials/rtr_experiment_template.material` | YAML 示例 |
| `assets/shaders/glsl/rtr_experiment_template.frag` | 参数消费方 |
| `src/infra/shader_compiler/shader_reflector.*` | 反射 |
| `src/infra/material_loader/generic_material_loader.*` | 写入参数 |

## 一个带注释的 YAML 片段

```yaml
shader: rtr_experiment_template        # -> assets/shaders/glsl/<name>.vert/.frag
passes:
  Forward:                             # -> MaterialTemplate::setPass(Pass_Forward, ...)
    renderState:                       # -> MaterialPassDefinition.renderState
      cullMode: Back
parameters:
  MaterialUBO.baseColor: [0.8, 0.7, 0.4, 1.0] # -> MaterialInstance parameter write
  MaterialUBO.warmColor: [1.0, 0.8, 0.25, 1.0]
  MaterialUBO.coolColor: [0.1, 0.25, 0.8, 1.0]
resources:
  SceneLightsUBO: system               # -> system-owned binding, scene 提供
```

如果 shader 里没有 `MaterialUBO.warmColor`，loader 就不应该假装能写成功。反射校验就是这里的安全网。

## 当前和未来的边界

当前可用：

- `.material` 声明 shader、pass、参数、资源。
- shader 编译和反射能检查参数结构。
- editor 可以切换 material URI，并保存节点级覆盖。

仍属于后续：

- 更完整的材质资产浏览器。
- 更友好的 shader 编译错误 UI。

## 我们已经学会了什么

YAML 和 GLSL 是一份合同的两面。名字、类型、binding 不一致时，问题不在 editor，而在合同没有对齐。

## 下一步

进入 [03 从 RTR 模板开始](03-start-from-rtr-template.md)。
