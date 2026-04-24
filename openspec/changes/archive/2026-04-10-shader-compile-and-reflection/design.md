## Context

项目采用 core/infra/backend 三层架构。`src/core/resources/shader.hpp` 定义了 `IShader` 接口（含 stage 字节码、reflection binding 查询、program hash）和 `ShaderProgramSet`（shader name + variant 宏组合）。当前没有 `IShader` 的任何实现——现有的 `shaders/CMakeLists.txt` 仅做离线 glslc 编译，不支持 variant 宏注入，也无法提取 reflection 信息。

infra 层已有 mesh_loader、texture_loader、window 等模块，每个模块以独立目录存在，通过 `src/infra/CMakeLists.txt` 统一编译进 `LX_Infra` 库。测试位于 `src/test/integration/`，以独立可执行文件形式运行。

## Goals / Non-Goals

**Goals:**
- 实现 infra 层 `ShaderCompiler`：从文件系统加载 GLSL 源码，注入 `ShaderVariant` 宏，调用 shaderc 编译为 SPIR-V
- 实现 infra 层 `ShaderReflector`：从 SPIR-V 字节码解析出 `ShaderResourceBinding` 列表（set, binding, type, size, stageFlags 等）
- 实现 `IShader` 的具体类 `ShaderImpl`，组合编译 + reflection 结果，满足 core 接口
- 提供 PBR shader（含 `HAS_NORMAL_MAP`、`HAS_METALLIC_ROUGHNESS` 等条件编译宏）
- 集成测试验证全流程，并演示 reflection 数据如何映射到 `VkDescriptorSetLayoutBinding`

**Non-Goals:**
- 不实现 shader 热重载 / 文件监控
- 不实现 shader 缓存（SPIR-V 缓存到磁盘）
- 不修改已有的 `IShader` core 接口
- 不实现完整的 PBR 渲染管线，PBR shader 仅用于测试 reflection 提取

## Decisions

### 1. Reflection 库选择：SPIRV-Cross

**选择**: 使用 SPIRV-Cross 进行 reflection 解析

**替代方案**:
- **spirv-reflect**：更轻量，只做 reflection，但社区维护力度不如 SPIRV-Cross
- **手动解析 SPIR-V**：工作量过大，且容易出错

**理由**: SPIRV-Cross 是 Khronos 官方维护，功能完善且广泛使用。Vulkan SDK 自带 SPIRV-Cross，无需额外下载。未来如需 GLSL 反编译也能直接复用。

### 2. 编译库选择：shaderc

**选择**: 使用 shaderc（Vulkan SDK 自带）

**理由**: shaderc 封装了 glslang，提供简洁的 C++ API，支持 `#define` 宏注入、include 文件解析。Vulkan SDK 已包含，无额外依赖。

### 3. 模块组织

```
src/infra/shader_compiler/
  shader_compiler.hpp / .cpp    — ShaderCompiler（编译）
  shader_reflector.hpp / .cpp   — ShaderReflector（reflection 解析）
  shader_impl.hpp / .cpp        — ShaderImpl : IShader（具体实现）
```

遵循 infra 层已有的模块组织模式（独立子目录 + hpp/cpp 分离）。

### 4. ShaderImpl 实现策略

`ShaderImpl` 持有编译后的 `vector<ShaderStageCode>` 和 reflection 解析得到的 `vector<ShaderResourceBinding>`。构造时一次性完成编译和 reflection，之后只读访问。`getProgramHash()` 基于 SPIR-V 字节码内容计算。

### 5. 集成测试策略

测试不依赖 GPU 上下文——shaderc 编译和 SPIRV-Cross reflection 均为纯 CPU 操作。测试将：
1. 从 `assets/shaders/glsl/` 加载 PBR shader
2. 分别启用/禁用 variant 宏进行编译
3. 提取并打印 `ShaderResourceBinding`
4. 演示如何将 binding 信息映射为 `VkDescriptorSetLayoutBinding`（仅打印，不调用 Vulkan API）

测试新增为独立 target `test_shader_compiler`，加入现有 `BuildTest` 体系。

## Risks / Trade-offs

- **[Risk] Vulkan SDK 版本差异导致 shaderc/SPIRV-Cross API 不兼容** → Mitigation: 使用稳定的 C++ API，避免 bleeding-edge 功能；CMake 中通过 `find_package(Vulkan)` 自动定位
- **[Risk] SPIRV-Cross reflection 不包含某些 binding 属性（如 buffer size/offset）** → Mitigation: SPIRV-Cross 的 `ShaderResources` + `get_decoration` 可获取 set/binding/descriptor count；buffer member offset 通过 `get_type()` + `get_member_decoration` 获取
- **[Trade-off] 运行时编译 vs 离线编译** → 本次实现运行时编译以支持 variant 动态组合；性能敏感场景可后续加缓存层
