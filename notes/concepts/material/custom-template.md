# 创建与排错自定义材质

写一个自定义材质，可以理解成给厨房增加一道新菜：我们要先写菜谱需要的工具，也就是 shader；再写点菜单，也就是 `.material`；最后让 scene 或 editor 加载它。当前最稳妥的路径是 YAML + GLSL，不需要为普通材质写 C++。

## 当前推荐路径

| 步骤 | 文件或 API | 目标 |
|---|---|---|
| 写 shader | `assets/shaders/glsl/<name>.vert/.frag` | 声明 vertex inputs、material binding、system binding |
| 写 material | `assets/materials/<name>.material` | 指向 shader，提供 variants、passes、默认参数和纹理 |
| 加载材质 | `LX_infra::loadGenericMaterial(uri)` | 得到 `MaterialInstance` |
| 放进场景 | scene document 或 editor runtime | 节点获得 `MaterialComponent` |
| 验证渲染 | `lxe_editor` 或集成测试 | 触发 scene validation 和 pipeline preload |

## 写 shader 时先分清资源归属

```glsl
layout(set = 2, binding = 0) uniform MaterialUBO {
    vec3 baseColor;
    float shininess;
} material;

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;
```

| Binding | 应该由谁提供 |
|---|---|
| `MaterialUBO` | material-owned，`.material parameters` 写默认值 |
| `albedoMap` / `normalMap` 等 texture | material-owned，`.material resources` 写默认纹理 |
| `CameraUBO` | system-owned，scene/camera 注入 |
| `SceneLightsUBO` / `LightUBO` | system-owned，light/scene 注入 |
| `Bones` | skeleton/renderable 路径注入 |

不要在 `.material resources` 里写 `CameraUBO: system` 或 `SceneLightsUBO: system`。当前 loader 会把 `resources` 当作 material-owned texture 默认值表来校验。

## 写 .material 时保持一套 canonical 参数

```yaml
shader: my_toon

variants:
  USE_LIGHTING: true

passes:
  Forward:
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true

parameters:
  MaterialUBO.baseColor: [0.9, 0.7, 0.4]
  MaterialUBO.shininess: 24.0

resources:
  albedoMap: white
```

当前 `parameters` 和 `resources` 是 instance 级默认值。我们不在 pass 下面写另一套参数：

```yaml
passes:
  Forward:
    parameters:          # 当前会被 loader 拒绝
      MaterialUBO.baseColor: [1.0, 0.0, 0.0]
```

如果多个 pass 都需要 `MaterialUBO`，它们要共享同一个 binding 名和同一个 member layout。

## 常见报错从这几类查

| 现象 | 先检查 |
|---|---|
| `missing required 'shader' field` | `.material` 顶层是否有 `shader` |
| `shader files not found` | `assets/shaders/glsl/<shader>.vert/.frag` 是否同时存在 |
| `parameter binding not found` | GLSL 是否声明了同名 material-owned UBO/SSBO |
| `member not found` | YAML key 的 member 是否和 GLSL block 成员名一致 |
| `resource ... not found as a texture binding` | `resources` 是否只写了 texture binding |
| `pass-scoped parameters/resources ...` | 参数和资源是否写在了 pass 内 |
| `reserved binding ... wrong descriptor type` | `CameraUBO` 等系统名字是否声明成了 UBO |
| `missing vertex input` | mesh vertex layout 是否提供 shader 需要的 location/type |
| `shader variant / Bones binding mismatch` | `USE_SKINNING` 和 `Bones` binding 是否同时出现 |

## 多 pass 材质的 authoring 思路

多 pass 不是把所有差异都塞进一份 fragment shader。我们先按“这一步的输出和状态是否不同”来决定是否拆 pass：

| 情况 | 当前表达方式 |
|---|---|
| 同一 Forward pass 内切换颜色 | parameter |
| 是否采样某张贴图，且 shader 已支持 | parameter + texture |
| 是否编译 normal map 代码路径 | variant |
| Forward 与 Shadow 使用不同 shader/render state | 多 pass |
| 同一 shader 但 blend/cull/depth 不同 | 不同 pass 或不同 material/template |

这能帮助我们避免把 pipeline 结构差异误写成普通参数，也避免把普通运行时数据误写成 variant。

## C++ 路径适合少数情况

普通材质优先走 `.material`。直接用 C++ 创建 template/instance 适合这些情况：

| 场景 | 原因 |
|---|---|
| 程序化生成材质 | 没有固定资产文件 |
| 测试特定结构错误 | 需要构造最小对象 |
| 实验新的 loader 或 variant 规则 | YAML 表达能力还不够 |

即便走 C++，也应保持同样边界：template 管结构，instance 管运行时值。

## 我们已经学会了什么

自定义材质的稳定路径是 shader + `.material` + scene/editor 验证。写之前先分清 material-owned 和 system-owned binding，再判断一个差异应该落在 parameter、resource、variant、pass 还是 template 上。

## 下一步

- [从 .material 到 MaterialInstance](file-to-instance.md)
- [Shader 在材质中的角色](shader.md)
- [自定义材质 Tutorial](../../tutorial/custom-material/index.md)
