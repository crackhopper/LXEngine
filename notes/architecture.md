# 架构总览

## 三层结构

```
┌─────────────────────────────────────────────┐
│  backend (src/backend/vulkan/)              │  Vulkan 实现
│  VulkanDevice / ResourceManager / Pipeline  │
│  CommandBuffer / RenderPass / Swapchain     │
└───────────────↑─────────────────────────────┘
                │ 只依赖 core 的接口
┌───────────────┴─────────────────────────────┐
│  infra (src/infra/)                         │  系统适配 + 运行期实现
│  ShaderCompiler / ShaderReflector / Window  │
│  TextureLoader / MeshLoader / GUI           │
│  blinnphong_material_loader                 │
└───────────────↑─────────────────────────────┘
                │ 只依赖 core 的接口
┌───────────────┴─────────────────────────────┐
│  core (src/core/)                           │  接口、值类型、纯数据
│  gpu/ (IRenderResource, Renderer, ...)      │
│  resources/ (Mesh, Material, Shader, ...)   │
│  scene/ (Scene, FrameGraph, RenderQueue)    │
│  utils/ (GlobalStringTable, StringID)       │
│  math/ / platform/                          │
└─────────────────────────────────────────────┘
```

**依赖规则**（详见 `openspec/specs/cpp-style-guide/spec.md`）:

- `core/` **不允许** include `infra/` 或 `backend/`
- `infra/` 可以 include `core/`，不可 include `backend/`
- `backend/vulkan/` 可以 include 两者

命名空间:
- Core: `LX_core`
- Infra: `LX_infra`
- Backend: `LX_core::backend`（**嵌套在 core 下**，不是独立的顶层 `LX_backend`）

## 资源生命周期模型

核心抽象在 `src/core/gpu/render_resource.hpp:44` — `class IRenderResource`。所有 GPU 端可消费的资源都继承自它：

- `SkeletonUBO` (`src/core/resources/skeleton.hpp:24`) — 骨骼矩阵数组
- `UboByteBufferResource` (`src/core/resources/material.hpp:247`) — material 的 std140 字节 buffer 包装器
- `CombinedTextureSampler` (`src/core/resources/texture.hpp:49`) — 纹理 + sampler 对
- `IVertexBuffer` / `IndexBuffer` (`src/core/resources/vertex_buffer.hpp:151` / `index_buffer.hpp:50`)
- `CameraUBO` / `DirectionalLightUBO` / `ObjectPC` — scene 层值类型

### Dirty 模型

GPU 同步通过"dirty 标志 + ResourceManager 同步"：

1. CPU 代码修改资源内容 → 调 `setDirty()`（继承自 `IRenderResource`）
2. `VulkanResourceManager::syncResource(cmdBufferMgr, resource)` 在每帧检查 dirty 位
3. 命中 → 将 CPU 数据拷贝到 GPU staging buffer 并 submit copy command
4. 清除 dirty

这条路径是 **GPU 同步的唯一触发点**，任何新资源必须继承 `IRenderResource` 并使用这个 dirty 通道。

## 一帧的数据流

```
Scene 持有多个 IRenderable
  │
  │ for each pass (Pass_Forward / Pass_Shadow / ...)
  ▼
Scene::buildRenderingItem(pass)          src/core/scene/scene.cpp
  │
  │ 收集: vertexBuffer / indexBuffer / descriptorResources / passMask
  │ 计算: PipelineKey::build(objectSig, materialSig)
  ▼
RenderingItem { pipelineKey, pass, material, descriptorResources, ... }
  │
  ▼
FrameGraph::buildFromScene(scene)         src/core/scene/frame_graph.cpp
  │ (预构建期)
  │ 收集所有 pass × renderable → 每个 pass 的 RenderQueue
  ▼
FrameGraph::collectAllPipelineBuildInfos()
  │ PipelineBuildInfo::fromRenderingItem(item) × N
  │ 去重 by PipelineKey
  ▼
PipelineCache::preload(buildInfos, renderPass)   src/backend/vulkan/details/pipelines/
  │ 为每个 PipelineBuildInfo 构建 VulkanPipeline 并缓存
  ▼
(运行期) for each RenderingItem:
  │
  ▼
VulkanResourceManager::syncResource(...)         src/backend/vulkan/details/vk_resource_manager.cpp
  │ 把 dirty 的 CPU 资源推到 GPU
  ▼
VulkanCommandBuffer::bindResources(item, pipeline)   src/backend/vulkan/details/commands/vkc_cmdbuffer.cpp
  │ 按 IRenderResource::getBindingName() 匹配反射 binding
  │ 更新 descriptor set
  ▼
VulkanCommandBuffer::drawItem(item)
```

## 核心接口层（src/core/gpu/）

| 文件 | 内容 |
|------|------|
| `render_resource.hpp` | `IRenderResource` 基类、`ResourceType` / `ResourcePassFlag` 枚举、`PC_Base` / `PC_Draw` push constant 类型 |
| `renderer.hpp` | `Renderer` 抽象基类（被 `VulkanRenderer` 实现） |
| `image_format.hpp` | `ImageFormat` 枚举（`RGBA8` / `BGRA8` / `R8` / `D32Float` / `D24UnormS8` / `D32FloatS8`），backend 做到自己的 native 格式映射 |
| `render_target.hpp` | `RenderTarget` 结构（colorFormat + depthFormat + sampleCount），目前不参与 `PipelineKey` |

## Infra 层的职责

infra 不是"工具库"，它是 **core 接口的具体实现**。每个 infra 模块对应一个 core 抽象：

| Core 接口 | Infra 实现 | 位置 |
|-----------|-----------|------|
| `IShader` | `ShaderImpl` (via `ShaderCompiler` + `ShaderReflector`) | `src/infra/shader_compiler/` |
| `LX_core::Window` | `LX_infra::Window` (SDL3 或 GLFW) | `src/infra/window/` |
| Material factory | `loadBlinnPhongMaterial()` | `src/infra/loaders/blinnphong_material_loader.hpp` |
| Mesh IO | `ObjLoader` / `GLTFLoader` | `src/infra/mesh_loader/` |
| Texture IO | `TextureLoader`（stb_image） | `src/infra/texture_loader/` |
| GUI | `Gui`（imgui） | `src/infra/gui/` |

core ↔ infra 之间通过**构造函数注入**连接，禁止 setter DI（见 cpp-style-guide spec R5）。

## 延伸阅读

- `openspec/specs/cpp-style-guide/spec.md` — 所有权 / 智能指针 / RAII 强制规则
- `openspec/specs/renderer-backend-vulkan/spec.md` — VulkanDevice/Buffer/Texture/Shader/Pipeline 的详细契约
- `docs/design/ShaderSystem.md` — shader 编译 + 反射的端到端数据流
- `docs/design/MaterialSystem.md` — Template-Instance 架构的设计权衡
- `docs/design/GlobalStringTable.md` — 字符串驻留系统的实现细节
- `GUIDES.md` — 开发流程与命令链
