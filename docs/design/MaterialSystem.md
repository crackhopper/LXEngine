# 材质系统设计

材质系统负责将 Shader、渲染状态和运行时属性组织为可复用的模板-实例结构。

> 源码位置：`src/core/resources/material.hpp`，命名空间 `LX_core`

## 核心架构

材质系统采用 **Template-Instance** 两级设计：

- **MaterialTemplate** — 定义 Shader 组合、渲染状态和 Binding 缓存（共享，多个实例引用同一模板）
- **MaterialInstance** — 持有每实例的属性覆盖值（颜色、贴图等），通过 `StringID` 索引

```
MaterialTemplate (shared_ptr)
├── m_passes: { passName → RenderPassEntry }
│   └── RenderPassEntry
│       ├── RenderState (cull, depth, blend)
│       ├── ShaderProgramSet (shader + variants)
│       └── bindingCache: { name → ShaderResourceBinding }
├── m_bindingCache: { StringID → ShaderResourceBinding }  // 全局缓存
└── MaterialInstance (shared_ptr)
    ├── m_template → MaterialTemplate
    ├── m_vec4s:    { StringID → Vec4f }
    ├── m_floats:   { StringID → float }
    └── m_textures: { StringID → TexturePtr }
```

## RenderState

描述渲染管线状态，直接映射到 Vulkan Pipeline 参数：

```cpp
struct RenderState {
  CullMode   cullMode        = CullMode::Back;
  bool       depthTestEnable = true;
  bool       depthWriteEnable= true;
  CompareOp  depthOp         = CompareOp::LessEqual;
  bool       blendEnable     = false;
  BlendFactor srcBlend       = BlendFactor::One;
  BlendFactor dstBlend       = BlendFactor::Zero;
};
```

## RenderPassEntry

每个渲染 Pass（Forward/Shadow 等）对应一个 Entry，包含该 Pass 的渲染状态、Shader 组合和从反射信息构建的 Binding 缓存：

```cpp
struct RenderPassEntry {
  RenderState renderState;
  ShaderProgramSet shaderSet;
  std::unordered_map<std::string, ShaderResourceBinding> bindingCache;

  void buildCache() {
    bindingCache.clear();
    auto shader = shaderSet.getShader();
    for (const auto &b : shader->getReflectionBindings()) {
      bindingCache[b.name] = b;
    }
  }
};
```

## MaterialTemplate

模板通过 `buildBindingCache()` 将所有 Pass 的 Shader 反射信息合并到一个以 `StringID` 为键的全局缓存中：

```cpp
void buildBindingCache() {
  m_bindingCache.clear();
  for (auto &[_, entry] : m_passes) {
    auto shader = entry.shaderSet.getShader();
    for (auto &b : shader->getReflectionBindings()) {
      StringID id = MakeStringID(b.name);
      m_bindingCache[id] = b;
    }
  }
}
```

查找 Binding 时直接通过 `StringID` 做 O(1) 查找：

```cpp
std::optional<...> findBinding(StringID id) const;
```

## MaterialInstance

实例持有对模板的 `shared_ptr` 引用，属性通过 `StringID` 索引存储。设置属性时会通过 `assert` 校验 Binding 存在性和类型：

```cpp
void setVec4(StringID id, const Vec4f &value) {
  auto binding = m_template->findBinding(id);
  assert(binding && binding->get().type == ShaderPropertyType::Vec4);
  m_vec4s[id] = value;
}
```

使用示例：

```cpp
auto tmpl = std::make_shared<MaterialTemplate>("PBR");
tmpl->setPass("forward", forwardPassEntry);
tmpl->buildBindingCache();

auto mat = std::make_shared<MaterialInstance>(tmpl);
mat->setVec4("u_BaseColor", Vec4f{1, 0, 0, 1});  // 隐式 StringID 构造
mat->setFloat("u_Roughness", 0.5f);
mat->setTexture("u_AlbedoMap", albedoTex);
```

## 设计要点

- **StringID 键**：属性存储和查找全部使用 `StringID`，避免字符串比较开销
- **Shader 驱动**：Binding 缓存从 Shader 反射自动构建，模板不硬编码属性
- **Pipeline Hash 缓存**：`getPipelineHash()` 缓存哈希值，相同状态+Shader 的管线只创建一次
- **类型安全**：`assert` 在设置属性时校验类型匹配（Debug 构建）
