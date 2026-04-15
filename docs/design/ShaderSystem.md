# Shader 系统设计

Shader 系统覆盖从 GLSL 源码到可用 Shader 对象的完整流程：编译、反射、封装。

> 源码位置：
> - 接口：`src/core/asset/shader.hpp`（`LX_core`）
> - 实现：`src/infra/shader_compiler/`（`LX_infra`）

## 整体流程

```
GLSL 源码 (.vert/.frag)
    │  + ShaderVariant macros (#define)
    ▼
ShaderCompiler::compileProgram()   ← shaderc
    │  → CompileResult { stages: [ShaderStageCode...] }
    ▼
ShaderReflector::reflect()         ← SPIRV-Cross
    │  → vector<ShaderResourceBinding>
    ▼
CompiledShader(stages, bindings)       ← IShader 实现
    │  构建 (set,binding) 和 name 索引
    ▼
MaterialTemplate.buildBindingCache()
    │  → StringID 索引的 binding 缓存
    ▼
渲染管线使用
```

## 核心数据结构

### ShaderStage（bitmask）

```cpp
enum class ShaderStage : uint32_t {
  Vertex   = 1 << 0,
  Fragment = 1 << 1,
  Compute  = 1 << 2,
  // ...
};
```

多阶段合并时通过 OR 组合：`stageFlags = Vertex | Fragment`

### ShaderResourceBinding

反射出的每个资源绑定：

```cpp
struct ShaderResourceBinding {
  std::string      name;            // "u_BaseColor"
  uint32_t         set = 0;
  uint32_t         binding = 0;
  ShaderPropertyType type;          // UniformBuffer, Texture2D, ...
  uint32_t         descriptorCount; // 数组大小
  uint32_t         size;            // UBO 字节大小
  ShaderStage      stageFlags;      // 在哪些阶段使用
};
```

### ShaderVariant

条件编译宏，用于生成 Shader 变体：

```cpp
struct ShaderVariant {
  std::string macroName;  // "HAS_NORMAL_MAP"
  bool enabled = false;
};
```

启用的变体会在编译时注入 `#define HAS_NORMAL_MAP 1`。

## ShaderCompiler

通过 shaderc 将 GLSL 编译为 SPIR-V 字节码：

```cpp
// 编译单个文件
static CompileResult compileFile(path, variants);

// 编译顶点+片段着色器程序
static CompileResult compileProgram(vertPath, fragPath, variants);
```

- 自动从文件扩展名推断 ShaderStage（`.vert`→Vertex，`.frag`→Fragment）
- 目标 Vulkan 1.3
- 启用的 variant 通过 `options.AddMacroDefinition(v.macroName, "1")` 注入

## ShaderReflector

通过 SPIRV-Cross 从 SPIR-V 提取绑定信息：

```cpp
static vector<ShaderResourceBinding> reflect(stages);
```

- 提取 `uniform_buffers`、`sampled_images`、`separate_samplers`、`storage_buffers`
- 多阶段合并：相同 `(set, binding)` 的资源合并 `stageFlags`
- **push_constant 不参与反射**（采用引擎硬编码约定，业界通行做法）

## CompiledShader（IShader 实现）

封装编译+反射结果，提供快速查找：

```cpp
class CompiledShader : public IShader {
  vector<ShaderStageCode>        m_stages;
  vector<ShaderResourceBinding>  m_bindings;

  // O(1) 查找索引
  unordered_map<uint32_t, size_t> m_setBindingIndex;  // key = (set << 16) | binding
  unordered_map<string, size_t>   m_nameIndex;         // key = binding name
};
```

- `findBinding(set, binding)` — 通过组合键直接查找
- `findBinding(name)` — 通过名称查找（fallback）
- `getProgramHash()` — 基于所有 SPIR-V 字节码的 `hash_combine`

## ShaderProgramSet

Shader 名称 + 变体组合，用于材质模板中标识一个完整的 Shader 程序：

```cpp
struct ShaderProgramSet {
  std::string shaderName;
  std::vector<ShaderVariant> variants;

  size_t getHash() const;  // 缓存哈希，按 enabled macros 排序后计算
};
```

## 依赖

| 依赖 | 用途 | 集成方式 |
|------|------|----------|
| **shaderc** | GLSL→SPIR-V 编译 | `find_library`（优先共享库） |
| **SPIRV-Cross** | SPIR-V 反射 | `find_package` + `FetchContent` 回退 |

CMake 选项 `SHADERC_DIR` / `SPIRV_CROSS_DIR` 支持 Windows 自定义路径。
