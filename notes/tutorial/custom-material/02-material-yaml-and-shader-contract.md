# 02 YAML 与 Shader 合同

`.material` 和 GLSL 之间像一份点菜单和厨房备料单：菜单上写了 `surfaceColor`，厨房里也必须真的有这个参数，否则服务员找不到该把值送到哪里。

## YAML 和 shader 必须签同一份合同

Shader reflection 会读取 SPIR-V，告诉引擎：

| 信息 | 例子 |
|---|---|
| uniform block 名 | `MaterialUBO` / `GoochParams` |
| block 内字段 | `surfaceColor` / `accentColor` |
| descriptor 名 | `MaterialUBO` / `LightUBO` |
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
  MaterialUBO.surfaceColor: [0.8, 0.35, 0.25] # -> MaterialInstance parameter write
  MaterialUBO.accentColor: [0.15, 0.4, 0.95, 1.0]
  MaterialUBO.mixAmount: 0.35
  MaterialUBO.mode: 0
```

如果 shader 里没有 `MaterialUBO.surfaceColor`，loader 就不应该假装能写成功。反射校验就是这里的安全网。

`resources` 只表示材质自己拥有的纹理默认值，不表示“shader 可见的所有资源”。`rtr_experiment_template` 当前没有材质侧纹理，所以示例不写 `resources`。如果某个 shader 反射出 material-owned `Texture2D albedoMap`，才可以写：

```yaml
resources:
  albedoMap: white                     # -> MaterialInstance::setTexture(...)；仅限材质侧纹理 binding
```

`CameraUBO`、`LightUBO`、`SceneLightsUBO`、`Bones` 这类 system-owned binding 由 scene、light 或 skeleton 路径提供，不能写进 `.material resources`。

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
