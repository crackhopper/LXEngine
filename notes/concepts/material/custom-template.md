# 创建自定义材质

## 两条路径

| 路径 | 适用场景 |
|------|---------|
| **YAML**（推荐） | 新增普通材质（PBR、unlit、toon 等），快速迭代默认值 |
| **C++** | 需要自定义 variant 校验、特殊编译逻辑或程序化生成 |

## YAML 路径

三步：写 shader → 写 `.material` → 调用 `loadGenericMaterial(path)`。

### 最小示例

```yaml
shader: blinnphong_0

variants:
  USE_LIGHTING: true

passes:
  Forward:
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
  Shadow:
    shader: shadow_depth_only       # Shadow 用完全不同的 shader

parameters:
  MaterialUBO.baseColor: [0.8, 0.8, 0.8]
  MaterialUBO.shininess: 12.0
  MaterialUBO.specularIntensity: 1.0

resources:
  albedoMap: white
```

### Schema 速查

| 字段 | 必填 | 说明 |
|------|------|------|
| `shader` | 是 | 全局默认 shader 名（对应 `shaders/glsl/<name>.vert/.frag`） |
| `variants` | 否 | 全局 shader variants（`NAME: true/false`） |
| `parameters` | 否 | 全局默认参数，格式 `bindingName.memberName: value` |
| `resources` | 否 | 全局默认纹理，值为文件路径或占位名 |
| `passes` | 否 | per-pass 配置，省略则默认一个 Forward pass |
| `variantRules` | 否 | variant 依赖规则列表 |

每个 pass 可以有 `shader`（覆盖全局 shader）、`renderState`、`variants`（合并全局）、`parameters`（覆写全局）、`resources`（覆写全局）。

### Variant 依赖规则

`variantRules` 用来声明 variant 之间的依赖关系。每条规则：如果 `requires` 中的 variant 全部启用，则 `depends` 中的 variant 也必须全部启用，否则 FATAL。

```yaml
variantRules:
  - requires: [USE_NORMAL_MAP]
    depends: [USE_LIGHTING, USE_UV]
  - requires: [USE_SKINNING]
    depends: [USE_LIGHTING]
```

### 占位纹理

| 名字 | RGBA | 用途 |
|------|------|------|
| `white` | (255, 255, 255, 255) | 默认 albedo |
| `black` | (0, 0, 0, 255) | 默认 emissive / AO |
| `normal` | (128, 128, 255, 255) | 平坦切线空间法线 |

### 验证规则

- 参数的 `bindingName` 和 `memberName` 必须在 shader 反射中存在，否则 FATAL
- 资源的 binding 名必须在 shader 反射中存在，否则 FATAL
- YAML 不参与 ownership 判定（归属由 `shader_binding_ownership.hpp` 决定）
- YAML 中没列出的合法参数仍然存在，只是零初始化，运行时可以 `setParameter`

参考示例：

- [`materials/pbr_gold.material`](../../../materials/pbr_gold.material)
- [`materials/blinnphong_lit.material`](../../../materials/blinnphong_lit.material)

## C++ 路径

```cpp
// 1. 编译 shader
auto compiled = ShaderCompiler::compileProgram(vert, frag, variants);
auto bindings = ShaderReflector::reflect(compiled.stages);
auto vertexInputs = ShaderReflector::reflectVertexInputs(compiled.stages);
auto shader = std::make_shared<CompiledShader>(
    std::move(compiled.stages), bindings, vertexInputs, "my_material");

// 2. 创建 template（只需要名字，shader 在 pass 级别指定）
auto tmpl = MaterialTemplate::create("my_material");

// 3. 组装 pass（shader 绑定在这里）
ShaderProgramSet programSet;
programSet.shaderName = "my_material";
programSet.variants = variants;
programSet.shader = shader;

MaterialPassDefinition entry;
entry.shaderSet = programSet;
entry.renderState = RenderState{};
entry.buildCache();

tmpl->setPass(Pass_Forward, std::move(entry));
tmpl->buildBindingCache();  // 不能漏，否则 per-pass binding 列表为空

// 4. 创建 instance 并设默认参数
auto instance = MaterialInstance::create(tmpl);
instance->setParameter(StringID("MyUBO"), StringID("roughness"), 0.5f);
instance->syncGpuData();
```

C++ 路径的完整样板可参考 `materials/pbr_gold.material` 或 `materials/blinnphong_default.material` 的 YAML 等价逻辑。

## 继续阅读

- [generic_material_loader.hpp](../../../src/infra/material_loader/generic_material_loader.hpp)
- [placeholder_textures.hpp](../../../src/infra/texture_loader/placeholder_textures.hpp)
- `openspec/specs/material-asset-loader/spec.md`
