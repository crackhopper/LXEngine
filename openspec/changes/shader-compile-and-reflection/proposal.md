## Why

当前 `IShader` 接口（`src/core/resources/shader.hpp`）只定义了 shader 资源的抽象接口，没有任何 infra 层实现。项目无法在运行时加载 GLSL 源码、注入 variant 宏、编译为 SPIR-V 并提取 reflection 信息。现有的 `shaders/CMakeLists.txt` 只做离线编译，无法支持 variant 动态组合。需要一个运行时 shader 编译 + reflection 管线，使材质系统能够根据 `ShaderProgramSet` 自动获取正确的 `ShaderResourceBinding` 并驱动 descriptor set layout 的创建。

## What Changes

- 新增 infra 层 shader 编译器实现（`src/infra/shader_compiler/`），基于 **shaderc** 进行运行时 GLSL→SPIR-V 编译，支持通过 `ShaderVariant` 注入预定义宏
- 新增 infra 层 SPIR-V reflection 解析（基于 **SPIRV-Cross** 或 **spirv-reflect**），从编译产物中提取 `ShaderResourceBinding` 列表
- 新增 `IShader` 的具体实现类，满足 core 层接口约定（`getAllStages`、`getReflectionBindings`、`findBinding`、`getProgramHash`）
- 新增一套 PBR shader（`shaders/glsl/pbr.vert` / `pbr.frag`），包含条件编译宏（如 `HAS_NORMAL_MAP`、`HAS_METALLIC_ROUGHNESS`）
- 新增集成测试：加载 PBR shader → 编译 → 提取 reflection → 打印 binding 信息 → 演示如何据此创建 `VkDescriptorSetLayout`

## Capabilities

### New Capabilities
- `shader-compilation`: 运行时 GLSL 编译为 SPIR-V，支持 variant 宏注入和多 stage 编译
- `shader-reflection`: 从 SPIR-V 字节码中解析出 descriptor binding、uniform buffer layout 等 reflection 信息
- `shader-integration-test`: 集成测试验证编译 + reflection 全流程，并演示 descriptor set layout 创建

### Modified Capabilities
（无已有 capability 需要修改）

## Impact

- **新增依赖**：shaderc（运行时编译）、SPIRV-Cross 或 spirv-reflect（reflection 解析）——均可通过 Vulkan SDK 获取
- **构建系统**：需修改 `src/infra/CMakeLists.txt` 链接新库，新增 `src/test/` 下的集成测试 target
- **代码影响**：仅新增文件，不修改已有 core 接口；`ShaderProgramSet::getShader()` 可在后续对接编译器实现
- **Shaders 目录**：新增 `shaders/glsl/pbr.vert` 和 `pbr.frag`
