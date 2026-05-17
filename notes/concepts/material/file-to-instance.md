# 从 .material 到 MaterialInstance

`.material` 文件像一张点菜单：它告诉 loader 这道材质用哪份 shader、开哪些变体、有哪些 pass、默认参数和默认纹理是什么。真正进入 scene 的不是 YAML 节点本身，而是 loader 创建出的 `MaterialInstance`。

当前入口是 `LX_infra::loadGenericMaterial(path)`。它会解析 YAML、编译 shader、反射 binding、构造 `MaterialTemplate`，最后创建并填充 `MaterialInstance`。

## 一份当前可用的 .material

```yaml
shader: rtr_experiment_template       # -> assets/shaders/glsl/rtr_experiment_template.vert/.frag

passes:
  Forward:                            # -> MaterialTemplate::setPassDefinition(StringID("Forward"), ...)
    renderState:                      # -> MaterialPassDefinition.renderState
      cullMode: Back                  # -> RenderState.cullMode
      depthTest: true                 # -> RenderState.depthTestEnable
      depthWrite: true                # -> RenderState.depthWriteEnable

parameters:
  MaterialUBO.surfaceColor: [0.8, 0.35, 0.25] # -> MaterialInstance::setParameter("MaterialUBO", "surfaceColor", Vec3f)
  MaterialUBO.accentColor: [0.15, 0.4, 0.95, 1.0]
  MaterialUBO.mixAmount: 0.35
  MaterialUBO.mode: 0
```

这里的 `shader` 是 basename，不是任意路径。loader 会在当前 runtime root 下找 `assets/shaders/glsl/<name>.vert` 和 `<name>.frag`。

## Loader 的执行顺序

| 步骤 | 代码行为 | 产物 |
|---|---|---|
| 解析 YAML | 读取 `shader`、`variants`、`passes`、`parameters`、`resources`、`variantRules` | loader 内部节点 |
| 找 shader 目录 | `getRuntimeShaderSourceDir()` | `assets/shaders/glsl/` |
| 编译每个 pass | `ShaderCompiler::compileProgram(vert, frag, variants)` | SPIR-V stages |
| 反射 shader | `ShaderReflector::reflect()` / `reflectVertexInputs()` | bindings + vertex inputs |
| 校验 YAML | 参数和纹理名必须存在于 material-owned reflection | fail-fast 或继续 |
| 建 template | `setPassDefinition()` 后 `rebuildMaterialInterface()` | canonical material interface |
| 建 instance | `MaterialInstance::create(tmpl)` | 参数 buffer 和 pass enable 初始状态 |
| 写默认值 | `applyParameters()` / `applyResources()` | parameter bytes + texture resources |
| 标记上传 | `syncGpuData()` | dirty parameter buffers |

这条链路的关键是：`.material` 不直接描述 Vulkan descriptor set，也不直接描述 pipeline。它描述的是材质的逻辑结构和默认数据，loader 再把这些信息翻译成 engine runtime 对象。

## passes 可以省略，但 shader 不能省略

当前 `.material` 必须有顶层 `shader`。如果没有写 `passes`，loader 会自动创建一个默认 `Forward` pass：

```yaml
shader: blinnphong_0

parameters:
  MaterialUBO.baseColor: [1.0, 1.0, 1.0]
```

等价于“用 `blinnphong_0` 编译一个 `Pass_Forward`，render state 使用默认值”。如果需要不同 pass 用不同 shader 或 render state，就显式写 `passes`。

## variants 和 variantRules

```yaml
variants:
  USE_LIGHTING: true
  USE_UV: true

variantRules:
  - requires: [USE_NORMAL_MAP]
    depends: [USE_LIGHTING, USE_UV]

passes:
  Forward:
    variants:
      USE_NORMAL_MAP: false
```

loader 会把顶层 variants 和 pass 内 variants 合并，再编译该 pass 的 shader。`variantRules` 用来提前挡住不合法组合，例如开启 normal map 却没有 UV。

enabled variants 属于结构信息，会进入 pipeline signature。它们不是普通材质参数。

## parameters 只能写 reflected buffer member

参数 key 必须是 `bindingName.memberName`：

```yaml
parameters:
  MaterialUBO.shininess: 32.0
```

loader 会先确认：

| 校验 | 原因 |
|---|---|
| `MaterialUBO` 是 shader reflection 中的 material-owned buffer binding | instance 需要一个 `ParameterBuffer` |
| `shininess` 是该 binding 的 reflected member | 写入需要 offset 和类型 |
| YAML 值类型和 reflected member 类型匹配 | 避免把 Vec4 写进 float |

当前支持写入 `float`、`int`、`Vec3`、`Vec4`。

## resources 只写材质拥有的纹理默认值

```yaml
resources:
  albedoMap: white       # -> placeholder texture
  normalMap: normal      # -> placeholder texture
```

`resources` 只接受 material-owned `Texture2D` / `TextureCube` binding。它不是 shader 可见资源总表，所以不能写：

```yaml
resources:
  SceneLightsUBO: system # 当前不支持，也不是 resources 的职责
```

`SceneLightsUBO`、`CameraUBO`、`LightUBO`、`Bones` 是系统保留 binding，由 scene/camera/light/skeleton 路径注入。

## pass-scoped parameters/resources 当前不支持

当前 loader 允许 pass 内读取 `shader`、`variants`、`renderState`，但 pass-scoped `parameters` 和 `resources` 会被拒绝。原因是 `MaterialInstance` 保存的是一套 canonical 参数和资源集合，不是“每个 pass 一套实例数据”。

```yaml
passes:
  Forward:
    parameters:          # 当前会报错
      MaterialUBO.mode: 1
```

如果多个 pass 使用同一个 `MaterialUBO`，它们必须共享同一份 reflected layout 和同一份实例值。

## 我们已经学会了什么

`.material` 是材质资产的入口，但运行时真正使用的是 `MaterialInstance`。loader 会把 YAML、shader 编译、reflection、template interface 和默认值写入串成一条固定链路。理解这条链路后，我们再看 template 和 instance 的职责边界就会清楚很多。

## 下一步

- [模板与 Pass：材质的结构定义](template-blueprint.md)
- [Shader 在材质中的角色](shader.md)
- [创建与排错自定义材质](custom-template.md)
