## Why

REQ-003a 完成后，backend 可以按 `PipelineKey` 缓存 pipeline 并用通用 `VulkanShaderGraphicsPipeline` 类创建。但当前还有三个硬伤阻碍真正的数据驱动渲染：

1. **全部 lazy** — `VulkanResourceManager::getOrCreateRenderPipeline()` 纯 miss-then-create，首帧出现大量卡顿；没有加载期一次性预构建入口
2. **Pipeline 构建依赖硬编码 slot 表** — `vk_resource_manager.cpp` 判断 `shaderName == "blinnphong_0"` 调 `blinnPhongForwardSlots()` 取 `PipelineSlotDetails`，新增一个 shader 就得改 backend 代码
3. **缺少帧结构抽象** — `Scene` 只持有单个 renderable，没有 pass / render queue，无法"扫描整个场景所有 pass 下的 pipeline 需求"，预构建无从下手

REQ-007（`interning-pipeline-identity`）刚刚把 `PipelineKey` / `getRenderSignature(pass)` 链路打通，为 per-pass 预构建提供了结构化的 pipeline 身份。REQ-004（UBO member reflection）给 `ShaderResourceBinding.members` 补齐了 std140 成员信息，使得反射已经能完全替代 `PipelineSlotDetails` 的硬编码信息。这两块前置已经 ready，本变更是"把它们接上"的最后一步。

## What Changes

分三个阶段推进，单个 change 内按 tasks.md 分组：

**阶段 1 — 数据封装（纯 additive）**
- 新增 `LX_core::PipelineBuildInfo` 结构体，从 `RenderingItem` 提取 backend 构建 pipeline 所需的**全部数据**（stages / bindings / vertexLayout / renderState / topology / pushConstant），不再依赖 shader 名字查表
- 新增 `LX_core::ImageFormat` 枚举（`RGBA8/BGRA8/R8/D32Float/D24UnormS8/D32FloatS8`）和 `LX_core::RenderTarget` 结构体（color/depth format + sample count）
- Backend 提供 `VkFormat toVkFormat(ImageFormat)` 映射

**阶段 2 — 帧结构抽象（扩展 Scene API）**
- 新增 `LX_core::RenderQueue` — 收集 `RenderingItem`、按 `pipelineKey` 排序、`collectUniquePipelineBuildInfos()` 去重
- 新增 `LX_core::FramePass`（name / target / queue）与 `LX_core::FrameGraph`
- `FrameGraph::buildFromScene(Scene &)` 遍历所有 renderable × pass 填充各 pass 的 `RenderQueue`
- `FrameGraph::collectAllPipelineBuildInfos()` 按 `PipelineKey` 去重后返回
- `FramePass::name` 使用 `StringID` 而非 `std::string`，和 REQ-007 的 `Pass_Forward / Pass_Shadow` 常量对齐
- `Scene` 新增 `const std::vector<IRenderableSharedPtr> &getRenderables() const`；内部存储从 `IRenderableSharedPtr mesh` 升级为 `std::vector<IRenderableSharedPtr> m_renderables`

**阶段 3 — 破坏性替换**
- 新增 `LX_core::backend::PipelineCache` 独立类：`find` / `getOrCreate` / `preload` / 从 `VulkanResourceManager::m_pipelines` 中抽出缓存
- `VulkanShaderGraphicsPipeline::create` 签名改为接收 `const PipelineBuildInfo &`；构造函数存储 `vector<ShaderResourceBinding>` 取代 `vector<PipelineSlotDetails>`
- `VulkanPipeline::createLayout()` 基于 `ShaderResourceBinding` 创建 `VkDescriptorSetLayout`
- `VulkanDescriptorManager::getOrCreateLayout(...)` / `allocateSet(...)` 签名改用 `std::span<const ShaderResourceBinding>` 或等价 key
- `VulkanCommandBuffer::bindResources` 的 `findBySlotId` 路径改为按**反射 binding 名 → 资源**匹配
- **BREAKING — 删除下列硬编码**：
  - `src/backend/vulkan/details/pipelines/vkp_pipeline_slot.hpp`（整个 `PipelineSlotDetails` + `PipelineSlotStage`）
  - `src/backend/vulkan/details/pipelines/forward_pipeline_slots.hpp`（`blinnPhongForwardSlots()` + `blinnPhongPushConstants()`）
  - `src/core/gpu/render_resource.hpp` 中的 `enum class PipelineSlotId`
  - `IRenderResource::getPipelineSlotId()` 虚函数
  - `Camera/Light/Material/Skeleton/Texture/UboByteBufferResource` 等各实现里的 `getPipelineSlotId()` override
  - `vk_resource_manager.cpp` 中 `if (shaderName == "blinnphong_0") { ... }` 分支
- **迁移**：资源类新增 `StringID getBindingName() const` 虚函数（默认返回 `StringID{}`），`Camera/Light/Skeleton/UboByteBufferResource` 返回 `"LightUBO"` / `"CameraUBO"` / `"Bones"` 等与 GLSL binding 名匹配的字符串；`Material` 的纹理路径已经走 `StringID`，保持不变
- `FrameGraph::preloadPipelines(...)` + `vk_renderer.cpp` 的 initScene 调用链：scene 加载后一次性预构建所有 pipeline；运行时 miss 仍然 fallback 到 `PipelineCache::getOrCreate` 并打 warn 日志

## Capabilities

### New Capabilities

- `pipeline-build-info`: 定义 `PipelineBuildInfo` 的数据契约 — 从 `RenderingItem` + `IShader` 反射提取 pipeline 构建所需的全部信息，替代硬编码 slot 表
- `frame-graph`: 定义 `ImageFormat` / `RenderTarget` / `RenderQueue` / `FramePass` / `FrameGraph`，以及 `FrameGraph::buildFromScene(Scene &)` / `collectAllPipelineBuildInfos()` 作为加载期预构建的入口
- `pipeline-cache`: 定义 backend-agnostic 的 pipeline 缓存契约 — `find` / `getOrCreate` / `preload` 三件套和 miss-then-warn 兜底语义；`Scene::buildRenderingItem(pass)` 与 `FrameGraph::preloadPipelines(...)` 共同消费

### Modified Capabilities

- `renderer-backend-vulkan`:
  - "VulkanPipeline shall create graphics pipelines" — 输入源从"shader name + slot table"改为 `PipelineBuildInfo`
  - "VulkanResourceManager shall map IRenderResource" — 明确 pipeline 缓存委托给独立的 `PipelineCache`；资源→descriptor 路由改用反射 binding 名

## Impact

- **硬依赖**：REQ-004（`ShaderResourceBinding.members`）✅、REQ-005（`MaterialInstance` 是 `IMaterial` 唯一实现）✅、REQ-007（`buildRenderingItem(StringID pass)` + `getRenderSignature`）✅
- **受影响代码**
  - Core 新文件：`src/core/resources/pipeline_build_info.{hpp,cpp}`、`src/core/gpu/image_format.hpp`、`src/core/gpu/render_target.{hpp,cpp}`、`src/core/scene/render_queue.{hpp,cpp}`、`src/core/scene/frame_graph.{hpp,cpp}`
  - Core 改动：`scene.{hpp,cpp}`（`m_renderables` 容器化 + `getRenderables()`）、`render_resource.hpp`（删 `PipelineSlotId`、加 `getBindingName()`）、`camera.hpp` / `light.hpp` / `skeleton.hpp` / `material.hpp` / `texture.hpp`（删 `getPipelineSlotId` override、加 `getBindingName` override）
  - Backend 新文件：`src/backend/vulkan/details/pipelines/pipeline_cache.{hpp,cpp}`
  - Backend 改动：`vk_resource_manager.{hpp,cpp}`（委托 PipelineCache）、`vkp_pipeline.{hpp,cpp}`（存储 `std::vector<ShaderResourceBinding>` 替代 slots）、`vkp_shader_graphics.{hpp,cpp}`（`create(buildInfo, ...)`）、`vkd_descriptor_manager.{hpp,cpp}`（layout/allocate 签名）、`vkc_cmdbuffer.cpp`（`bindResources` 按 name 匹配）、`vk_renderer.cpp`（预构建 hook）
  - Backend 删除：`vkp_pipeline_slot.hpp`、`forward_pipeline_slots.hpp`
- **调用点迁移**：`test_vulkan_{pipeline,resource_manager,command_buffer}.cpp`、`test_render_triangle.cpp` 需要更新为经由 `FrameGraph` 或继续直接 `getOrCreatePipelineCache(...)`
- **新增测试**：`test_pipeline_build_info.cpp`（从 RenderingItem 提取的字段与反射一致）、`test_frame_graph.cpp`（Scene → FrameGraph → 去重 PipelineBuildInfo）、`test_pipeline_cache.cpp`（find/getOrCreate/preload 语义；命中后不重建）
- **行为不变**：`test_render_triangle` 依然渲染出三角形（关键回归指标）
- **backend 渲染路径**：bind 顺序与 draw call 保持位精确等价；仅内部结构化
- **文档归档**：`docs/requirements/003b-pipeline-prebuilding.md` 落地后移入 `finished/`
