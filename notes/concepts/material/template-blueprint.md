# 模板与 Pass：材质的结构定义

`MaterialTemplate` 是材质系统里的菜谱。它不保存“这一次 baseColor 是多少”这类运行时值，也不保存所有 technique。loader 先选择一条 technique，再把这条 technique 的 pass 映射成 runtime pass definitions，template 只保存这份已选结构。

这条边界很重要：同一张菜谱可以做出很多道菜。多个 `MaterialInstance` 可以共享一个 `MaterialTemplate`，但每个 instance 有自己的参数字节、纹理对象和 pass 启用状态。

## Template 先定义结构

`MaterialTemplate` 当前回答三类结构问题：

| 问题 | 代码入口 | 当前含义 |
|---|---|---|
| 有哪些 runtime pass | `setPassDefinition(pass, definition)` | 以 `StringID` 保存 selected technique 映射后的 `Forward`、`Deferred` 等 pass |
| 每个 pass 怎么画 | `MaterialPassDefinition` | 包含 `ShaderProgramSet` 和 `RenderState` |
| 材质自己拥有哪些 binding | `rebuildMaterialInterface()` | 从 shader reflection 过滤 system-owned binding 后建立 canonical 表 |

`MaterialPassDefinition` 是菜谱里的一个步骤：

| 字段 | 影响 |
|---|---|
| `shaderProgram.shaderName` | 决定逻辑 shader 家族，例如 `blinnphong_0` |
| `shaderProgram.variants` | 决定编译宏组合，并进入 pipeline signature |
| `shaderProgram.shader` | 持有已编译 shader 和 reflection 结果 |
| `renderState` | 决定 cull/depth/blend 等固定功能状态，并进入 pipeline signature |

## Canonical binding 是模板的核心账本

shader reflection 会返回所有 descriptor binding，但材质并不拥有全部 binding。`MaterialTemplate::rebuildMaterialInterface()` 会先过滤系统保留名字，然后把 material-owned binding 收束成一张 canonical 表：

```text
shader reflection bindings
  -> 过滤 CameraUBO / LightUBO / SceneLightsUBO / Bones
  -> 按 StringID(binding.name) 建 canonical material binding
  -> 为每个 pass 保存本 pass 使用的 binding id 列表
```

这让 template 成为材质接口的真值来源。`MaterialInstance` 不需要重新扫描所有 pass，也不需要猜一个 binding 的类型；它只消费 template 已经归并好的 canonical binding。

## 同名 binding 必须跨 pass 一致

如果两个 pass 都声明了 `MaterialUBO`，它们必须在结构上是同一个契约：

| 被比较的字段 | 为什么必须一致 |
|---|---|
| `type` | instance 要知道这是 UBO、SSBO 还是 texture |
| `descriptorCount` | descriptor layout 不能同名不同数组长度 |
| `set` / `binding` | backend 绑定位置必须稳定 |
| `size` | buffer 字节大小必须一致 |
| `members` | `setParameter(binding, member)` 要按同一套 offset 写入 |

一旦不一致，`MaterialTemplate::rebuildMaterialInterface()` 会直接 fail-fast。也就是说，跨 pass 的 binding 归并不是 instance 的补救逻辑，而是 template 构建阶段的结构校验。

## YAML technique pass 映射到模板

```yaml
shader: rtr_experiment_template       # -> ShaderProgramSet.shaderName 默认值
defaultTechnique: Forward

techniques:
  Forward:
    passes:
      Opaque:                         # -> MaterialTemplate::setPassDefinition(Pass_Forward, ...)
        renderState:                  # -> MaterialPassDefinition.renderState
          cullMode: Back              # -> RenderState.cullMode
          depthTest: true             # -> RenderState.depthTestEnable
          depthWrite: true            # -> RenderState.depthWriteEnable
```

如果 scene 没有指定 technique，loader 选择 `defaultTechnique`。如果 scene 指定了 technique，loader 严格选择该 technique；材质缺少这条 technique 时加载失败。未选中的 technique 不进入 `MaterialTemplate`。

## Template 不保存什么

| 不属于 Template | 属于哪里 |
|---|---|
| `MaterialUBO.baseColor` 的当前值 | `MaterialInstance` 的 `ParameterBuffer` |
| `albedoMap` 当前绑定哪张纹理 | `MaterialInstance::m_textureBindingsByName` |
| 某个实例是否关闭了 `Forward` pass | `MaterialInstance::m_enabledPasses` |
| scene camera/light/skeleton 资源 | scene、camera、light、skeleton 系统 |
| Vulkan pipeline 对象 | backend pipeline cache |

## 我们已经学会了什么

`MaterialTemplate` 是结构层：它把 pass、shader variants、render state 和 material-owned binding 接口统一起来。template 构建成功后，instance 才能稳定地按 binding name 写参数、按 pass 取资源。

## 下一步

- [Shader 在材质中的角色](shader.md)
- [MaterialInstance：运行时状态](material-instance.md)
- [模板如何影响 Pipeline](template-and-pipeline.md)
