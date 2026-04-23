# Renderer-Demo 项目评审与后续规划

**日期**: 2026-04-19  
**评审范围**: 整体架构、现有实现、技术风险

---

## 一、项目结构

```
src/
├── core/           # 平台无关接口层
│   ├── asset/      # Shader、MaterialTemplate/Instance、Mesh、Texture、Skeleton
│   ├── frame_graph/# FrameGraph、RenderQueue、Pass、RenderTarget
│   ├── gpu/        # EngineLoop、Renderer 接口
│   ├── input/      # InputState、KeyCode、MouseButton（mock 实现）
│   ├── math/       # Vec3f、Mat4f、Quatf、Bounds、Rect
│   ├── pipeline/   # PipelineKey、PipelineBuildDesc
│   ├── platform/   # Window 接口、类型
│   ├── rhi/        # IGpuResource、VertexBuffer、IndexBuffer、ImageFormat、UniformBuffer
│   ├── scene/      # Scene、SceneNode、Camera、Light、IRenderable
│   ├── time/       # Clock
│   └── utils/      # StringTable、Hash、FilesystemTools、Env
├── infra/          # 基础设施实现层
│   ├── external/   # SPIRV-Cross、SDL3、yaml-cpp、imgui（vendored）
│   ├── gui/        # ImGui 集成
│   ├── material_loader/   # YAML 材质加载器
│   ├── mesh_loader/       # ObjLoader、GLtfLoader（cgltf）
│   ├── shader_compiler/   # ShaderCompiler、ShaderReflector、CompiledShader
│   ├── texture_loader/    # stb_image 纹理加载
│   └── window/     # SDL3 / GLFW 窗口后端
├── backend/
│   └── vulkan/     # Vulkan 渲染后端
│       ├── details/
│       │   ├── commands/       # CommandBuffer、CommandBufferManager
│       │   ├── descriptors/   # DescriptorManager
│       │   ├── device_resources/ # Buffer、Texture、Shader Vulkan 实现
│       │   ├── pipelines/     # Pipeline、PipelineCache、GraphicsShaderProgram
│       │   └── render_objects/ # Swapchain、Framebuffer、RenderPass、RenderContext
│       └── vulkan_renderer.cpp/hpp
├── demos/
│   └── scene_viewer/  # 主演示程序（damaged_helmet）
└── test/
    └── integration/  # 30 个集成测试（自定义 EXPECT 宏，无外部框架）
```

**三层架构清晰**: core（接口）→ infra（通用实现）→ backend（Vulkan 特定实现）。

---

## 二、现有实现评估

### 2.1 核心层 (core/)

| 模块 | 状态 | 说明 |
|------|------|------|
| **RHI** | ✅ 完整 | VertexBuffer、IndexBuffer、UniformBuffer、ImageFormat、IGpuResource |
| **Shader/Material** | ✅ 完整 | MaterialTemplate + MaterialInstance，StringID 驱动属性映射 |
| **StringTable** | ✅ 完整 | GlobalStringTable + StringID，线程安全（shared_mutex） |
| **Scene** | ✅ 完整 | Scene、SceneNode、Camera、Light、IRenderable |
| **FrameGraph** | ✅ 完整 | FrameGraph、RenderQueue、按 PipelineKey 排序 |
| **Pipeline** | ✅ 完整 | PipelineKey、PipelineBuildDesc、PipelineCache |
| **Skeleton** | ⚠️ 存在骨架结构 | Skeleton、SkeletonUBO 类型已定义，但无动画播放管线 |
| **math/** | ✅ 完整 | Vec3f、Mat4f、Quatf、Bounds、Rect |
| **input/** | ⚠️ Mock | InputState、KeyCode 等仅 mock 实现 |
| **gpu/EngineLoop** | ✅ 完整 | update hooks 机制 |

**问题**:
- `src/core/resources/` 目录为空（README 说资源类型应在此，但实际在 `rhi/`）
- `src/core/rhi/renderer.cpp` 为空（仅头文件声明，实现在 backend）

### 2.2 基础设施层 (infra/)

| 模块 | 状态 | 说明 |
|------|------|------|
| **ShaderCompiler** | ✅ 完整 | GLSL → SPIR-V（shaderc）+ 反射（SPIRV-Cross） |
| **MeshLoader** | ✅ 完整 | OBJ（tinyobjloader）、glTF（cgltf） |
| **TextureLoader** | ✅ 完整 | stb_image |
| **MaterialLoader** | ✅ 完整 | YAML 格式材质文件加载 |
| **Window** | ✅ 完整 | SDL3 + GLFW 双后端，PImpl 封装 |
| **GUI** | ✅ 完整 | ImGui 集成、DebugUI |

### 2.3 后端层 (backend/vulkan/)

| 模块 | 状态 | 说明 |
|------|------|------|
| **Device** | ✅ 完整 | VulkanDevice 封装、验证层、队列管理 |
| **CommandBuffer** | ✅ 完整 | 录制机制、CommandBufferManager |
| **DescriptorManager** | ✅ 完整 | Descriptor pool / set 管理 |
| **Pipeline** | ✅ 完整 | 创建、缓存、preload 机制 |
| **Swapchain** | ✅ 完整 | 生命周期管理 |
| **RenderPass/Framebuffer** | ✅ 完整 | 渲染通道封装 |
| **ResourceManager** | ✅ 完整 | GPU 资源生命周期、垃圾回收、pipeline 预加载 |

### 2.4 测试 (src/test/integration/)

- **30 个集成测试**，使用自定义 `EXPECT()` 宏
- ShaderCompiler 测试无需 GPU，可独立运行
- Vulkan 测试需要 GPU 环境

### 2.5 文档 (notes/, openspec/specs/)

- **25+ OpenSpec 规范**，结构化描述intended behavior
- **Subsystem 文档** 覆盖 FrameGraph、MaterialSystem、ShaderSystem、VulkanBackend 等
- **C++ Style Guide** 规范：禁止 raw 指针、RAII、constructor injection

---

## 三、明显风险

### 3.1 缺失或残缺的功能

| 风险 | 严重度 | 说明 |
|------|--------|------|
| **骨骼动画管线未完成** | 高 | Skeleton/SkeletonUBO 类型存在，但无动画播放、数据上传、skinned mesh 渲染通路 |
| **Shadow Mapping 仅为存根** | 高 | `Pass_Shadow` 定义存在，无实际 shadow pass 实现 |
| **Deferred Rendering 存根** | 中 | `Pass_Deferred` 定义存在，无 G-buffer 渲染通路 |
| **PBR shader 存在但未集成** | 中 | `shaders/glsl/pbr.vert/.frag` 已提供，材质系统未对接 |
| **src/main.cpp 为空** | 低 | 入口仅为 `return 0;`，实际入口在 `demos/scene_viewer/main.cpp` |

### 3.2 技术债务

| 问题 | 说明 |
|------|------|
| **SPIRV-Cross 编译警告** | `spirv_glsl.cpp` 中 lambda 隐式捕获 `this` 警告，CMake 每次全量编译耗时长 |
| **无后端抽象** | 仅 Vulkan 后端，README 列 TODO 但未实现 |
| **PImpl 使用不一致** | 部分 PImpl 使用原始指针（如 `Impl*`），非 `unique_ptr` |
| **Pipeline 热重载缺失** | 修改 shader/pipeline 需重建场景 |
| **错误处理不完善** | 多个 `vkQueueSubmit` 等失败仅提前返回，无错误日志 |
| **FrameGraph 每帧全量重建** | `initScene()` 重新构建，无增量更新 |

### 3.3 文档与代码不一致

| 项目 | 实际情况 |
|------|----------|
| README 称 `src/core/resources/` 包含 vertex_buffer.hpp 等 | 这些文件实际在 `src/core/rhi/` |
| README 列出"待实现"项目（如"渲染循环完整实现"） | 部分可能已实现，但未更新 README |
| README 旧架构图提到 `scene/components/` 子目录 | 该目录不存在 |

### 3.4 潜在 bug

| 位置 | 风险 |
|------|------|
| `GlobalStringTable` | `m_nextID` 为 atomic，但 StringID composition 操作非 lock-free，高并发下可能有问题 |
| `PipelineBuildDesc::fromRenderingItem()` | 无校验，假设所有字段有效，非法输入会导致未定义行为 |
| `MaterialInstance::m_passOverrides` | 无清理机制，可能无限增长 |

---

## 四、后续规划建议

### 4.1 短期（修复类）

1. **更新 README** — 修正 `resources/` vs `rhi/` 路径、移除过时 TODO
2. **完善错误处理** — 为 `vkQueueSubmit` 等关键调用添加错误日志和返回值检查
3. **统一 PImpl 风格** — 将原始指针改为 `unique_ptr<Impl>`
4. **CI 流水线** — 接入 shader compiler 测试（无需 GPU），防止构建 regression

### 4.2 中期（功能类）

1. **骨骼动画管线** — 实现 skeleton 数据加载 → GPU 上传 → skinned mesh 渲染全链路
2. **Shadow Mapping** — 实现 directional light shadow pass
3. **PBR 材质集成** — 将 pbr.vert/.frag 接入 MaterialTemplate/MaterialInstance 系统
4. **Pipeline 热重载** — 支持运行时重新编译/更新单个 pipeline

### 4.3 长期（架构类）

1. **后端抽象** — 提取 `gpu::Renderer` 接口，实现 D3D12/Metal 后端（对应 README TODO）
2. **FrameGraph 增量构建** — 支持场景局部更新而非全量重建
3. **材质 pipeline hash 缓存** — 参见 `openspec/specs/resource-pipeline-hash/`

---

## 五、总结

| 维度 | 评价 |
|------|------|
| **架构设计** | 三层分离清晰，接口/实现边界明确，设计文档详尽 |
| **核心渲染管线** | 基本完整，FrameGraph + VulkanBackend + MaterialSystem 联动正常 |
| **工具链** | Shader 编译/反射、mesh/texture 加载、材质加载均已就绪 |
| **测试覆盖** | 30 个集成测试覆盖主要模块，但无单元测试框架 |
| **文档质量** | OpenSpec 规范详细，subsystem 文档覆盖全面 |
| **工程化** | CMake 构建、部分 CI 缺失，部分 PImpl 不一致，错误处理粗糙 |

**整体评估**: 项目处于**可运行的核心渲染器**阶段，架构设计优良，文档齐全，但**功能完整度约 60-70%**（骨骼动画、shadow、deferred、PBR 均未完成或仅存根）。建议优先完成骨骼动画和 shadow mapping，再推进 PBR 集成和后端抽象。
