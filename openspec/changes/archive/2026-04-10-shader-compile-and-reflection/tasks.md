## 1. 项目依赖与构建配置

- [x] 1.1 修改 `src/infra/CMakeLists.txt`，添加 shaderc 和 SPIRV-Cross 依赖（通过 Vulkan SDK 的 `find_package` 或直接链接）
- [x] 1.2 创建 `src/infra/shader_compiler/` 目录结构，将新源文件加入 INFRA_SOURCES

## 2. ShaderCompiler 实现

- [x] 2.1 实现 `shader_compiler.hpp`：定义 ShaderCompiler 类，接口包括从文件加载 GLSL、注入 variant 宏、编译为 SPIR-V
- [x] 2.2 实现 `shader_compiler.cpp`：使用 shaderc API 完成编译逻辑，处理错误信息，返回 `ShaderStageCode`

## 3. ShaderReflector 实现

- [x] 3.1 实现 `shader_reflector.hpp`：定义 ShaderReflector 类，接口为接收多个 ShaderStageCode，输出合并后的 `vector<ShaderResourceBinding>`
- [x] 3.2 实现 `shader_reflector.cpp`：使用 SPIRV-Cross 解析 uniform buffers、sampled images、samplers 等资源，填充 ShaderResourceBinding 字段，合并多 stage 的 stageFlags

## 4. ShaderImpl 实现

- [x] 4.1 实现 `shader_impl.hpp/cpp`：继承 `IShader`，持有 stages 和 bindings，实现 `getAllStages()`、`getReflectionBindings()`、`findBinding(set, binding)`、`findBinding(name)`、`getProgramHash()`
- [x] 4.2 实现 `findBinding` 的快速查找（使用 unordered_map 索引 set/binding 和 name）

## 5. PBR Shader 编写

- [x] 5.1 编写 `assets/shaders/glsl/pbr.vert`：包含 camera UBO、model push constant，支持法线和切线属性传递
- [x] 5.2 编写 `assets/shaders/glsl/pbr.frag`：包含 material UBO、albedo texture，通过 `#ifdef HAS_NORMAL_MAP` 和 `#ifdef HAS_METALLIC_ROUGHNESS` 条件编译额外纹理采样

## 6. 集成测试

- [x] 6.1 编写 `src/test/integration/test_shader_compiler.cpp`：加载 PBR shader → 编译（无宏 / 全宏）→ 提取 reflection → 打印所有 ShaderResourceBinding → 打印对应的 VkDescriptorSetLayoutBinding 映射
- [x] 6.2 更新 `src/test/CMakeLists.txt`，将 `test_shader_compiler` 加入构建（需链接 shaderc 和 SPIRV-Cross）
