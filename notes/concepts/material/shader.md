# Shader 在材质中的角色

## Shader 不只是 GPU 程序

在材质系统里，shader 不只是"给 GPU 跑的代码"。它同时是一份**结构合同**：

- **反射 bindings** 告诉系统这个 shader 需要哪些 descriptor 资源（UBO、纹理等）
- **vertex inputs** 告诉系统这个 shader 需要 mesh 提供哪些顶点属性
- **shader name + variants** 参与 pipeline identity 和 pipeline signature

所以材质系统关心的不是"一个 .spv 文件"，而是"名字 + variants + 编译结果 + 反射信息"的整体。

## 三层 Shader 对象

| 对象 | 层 | 职责 |
|------|---|------|
| `IShader` | core 接口 | 暴露 stages、reflection bindings、vertex inputs、binding 查询 |
| `CompiledShader` | infra 实现 | 承接 `ShaderCompiler` 编译和 `ShaderReflector` 反射的结果 |
| `ShaderProgramSet` | 材质值对象 | 把 shader 名、enabled variants 和 `IShaderSharedPtr` 打包，嵌入 `MaterialPassDefinition` |

`ShaderProgramSet` 是材质系统真正和 pass 绑定的入口。它一头连着 loader 的编译结果，一头连着 pipeline signature。

## 反射 binding 与归属合同

shader 反射出的 bindings 分为两类：

| 归属 | 判定方式 | 例子 | 谁提供资源 |
|------|---------|------|-----------|
| 系统保留 | `isSystemOwnedBinding()` 返回 true | `CameraUBO`、`LightUBO`、`Bones` | Scene / Skeleton |
| 材质所有 | 其余所有 binding | `MaterialUBO`、`albedoMap`、`SurfaceParams` | MaterialInstance |

这个合同定义在 `shader_binding_ownership.hpp`。保留名字集是固定的三个，扩展需要新 spec。

如果 shader 声明了一个保留名字但类型不对（比如 `CameraUBO` 声明为 `Texture2D`），SceneNode 验证时会 FATAL——这是 shader authoring error。

## 哪些 binding 是"必须绑定"的

对于材质侧 binding：

- **buffer 类型**（`UniformBuffer`、`StorageBuffer`）是结构性必需的——shader 需要一块内存来读参数，MaterialInstance 构造时会自动创建对应的 buffer slot
- **纹理类型**（`Texture2D`、`TextureCube`）不一定必须绑定——shader 可以通过运行时参数（如 `enableAlbedo`）控制是否真的采样

例如 `blinnphong_0.frag` 里的 `albedoMap` 和 `normalMap` 就是可选的 sampled resource。缺省未绑定不会导致 SceneNode 判非法。

## Shader 的完整路径

1. Loader 决定 shader 名和 variants（来自 YAML 或 C++ 代码）
2. `ShaderCompiler` 编译 GLSL → SPIR-V
3. `ShaderReflector` 反射出 binding 列表和 vertex input 列表
4. `CompiledShader` 承接编译和反射结果
5. `ShaderProgramSet` 把名字、variants 和 shader 绑在一起
6. `MaterialPassDefinition` 把 `ShaderProgramSet` 作为某个 pass 的 shader 配置

在 YAML 材质文件里，对应关系是：

```yaml
shader: blinnphong_0        # → shader 名，用来定位 .vert / .frag
variants:                    # → ShaderProgramSet.variants
  USE_LIGHTING: true
  USE_UV: true
```

## 继续阅读

- shader 反射接口：[shader.hpp](../../../src/core/asset/shader.hpp)
- shader 系统设计文档：[../../subsystems/shader-system.md](../../subsystems/shader-system.md)
