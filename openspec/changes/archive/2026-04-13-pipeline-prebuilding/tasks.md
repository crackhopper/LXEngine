## 1. Stage 1 — PipelineBuildInfo / ImageFormat / RenderTarget（纯 additive）

- [x] 1.1 新建 `src/core/resources/pipeline_build_info.hpp` 声明 `struct PipelineBuildInfo { PipelineKey key; std::vector<ShaderStageCode> stages; std::vector<ShaderResourceBinding> bindings; VertexLayout vertexLayout; RenderState renderState; PrimitiveTopology topology; PushConstantRange pushConstant; }` + `static fromRenderingItem(const RenderingItem &)` 工厂声明
- [x] 1.2 确定 `PushConstantRange` 的 core 层定义：如果已有则复用；否则在 `pipeline_build_info.hpp` 定义 `struct PushConstantRange { uint32_t offset; uint32_t size; uint32_t stageFlagsMask; }`（不依赖 Vulkan 枚举）
- [x] 1.3 为 `RenderingItem` 新增 `MaterialPtr material` 字段（在 `scene.hpp`），并确认前向声明 include 合规
- [x] 1.4 修改 `Scene::buildRenderingItem`：新增 `item.material = sub->material`
- [x] 1.5 新建 `src/core/resources/pipeline_build_info.cpp` 实现 `fromRenderingItem`：`key = item.pipelineKey`；从 `item.shaderInfo` 拷贝 `stages` 与 `bindings`；从 `item.vertexBuffer` 取 `VertexLayout`；从 `item.indexBuffer` 取 `PrimitiveTopology`；从 `item.material->getRenderState()` 取 `renderState`；`pushConstant = {0, 128, Vertex|Fragment}` engine-wide 约定
- [x] 1.6 `IShader` 可能没有直接返回 `vector<ShaderResourceBinding>` 的 getter — 确认 `getReflectionBindings()` 返回合适的类型，否则在 factory 里拷贝
- [x] 1.7 新建 `src/core/gpu/image_format.hpp` — `enum class ImageFormat : uint8_t { RGBA8, BGRA8, R8, D32Float, D24UnormS8, D32FloatS8 }`
- [x] 1.8 新建 `src/core/gpu/render_target.hpp` + `.cpp` — `struct RenderTarget { ImageFormat colorFormat = BGRA8; ImageFormat depthFormat = D32Float; uint8_t sampleCount = 1; size_t getHash() const; }` — hash 用 `hash_combine` 三个字段
- [x] 1.9 在 `src/backend/vulkan/details/vk_resource_manager.cpp`（或更合适位置）添加 `VkFormat toVkFormat(LX_core::ImageFormat)`，覆盖全部 6 个 case
- [x] 1.10 新建 `src/test/integration/test_pipeline_build_info.cpp`：用 `FakeShader` 模拟反射 bindings，构造 `RenderingItem`（可复用 `test_pipeline_identity.cpp` 的 `Fixture` 模式），断言 `fromRenderingItem(item).bindings == shader.getReflectionBindings()`、`key == item.pipelineKey`、确定性（两次调用相等）
- [x] 1.11 把 `test_pipeline_build_info` 加入 `src/test/CMakeLists.txt::TEST_INTEGRATION_EXE_LIST`
- [x] 1.12 `cmake ./build && cmake --build ./build` 全量绿；运行 `./build/src/test/test_pipeline_build_info`

## 2. Stage 2 — RenderQueue / FrameGraph / Scene 容器化

- [x] 2.1 新建 `src/core/scene/render_queue.hpp` + `.cpp`：`class RenderQueue { addItem, sort, getItems, collectUniquePipelineBuildInfos }`；`sort()` 按 `pipelineKey.id` 稳定排序；去重用 `unordered_set<PipelineKey, PipelineKey::Hash>`
- [x] 2.2 新建 `src/core/scene/frame_graph.hpp` + `.cpp`：`struct FramePass { StringID name; RenderTarget target; RenderQueue queue; }`；`class FrameGraph { addPass, buildFromScene, collectAllPipelineBuildInfos, getPasses }`
- [x] 2.3 `Scene::m_renderables` 由 `IRenderableSharedPtr mesh`（公有字段）迁移到 `std::vector<IRenderableSharedPtr> m_renderables`（私有 + getter）；构造函数 `Scene(IRenderableSharedPtr mesh)` 填入 `m_renderables.push_back(std::move(mesh))`
- [x] 2.4 新增 `const std::vector<IRenderableSharedPtr> &Scene::getRenderables() const`
- [x] 2.5 保留向后兼容的 `IRenderableSharedPtr Scene::mesh;` 公有字段？—— **决定不保留**，在所有调用点迁移到 `scene->getRenderables().front()`
- [x] 2.6 新增 `RenderingItem Scene::buildRenderingItemForRenderable(const IRenderableSharedPtr &r, StringID pass) const`，把 `buildRenderingItem(pass)` 的逻辑搬进去（用 `r` 代替 `mesh`）
- [x] 2.7 `Scene::buildRenderingItem(StringID pass)` 改为调用 `buildRenderingItemForRenderable(m_renderables.front(), pass)`，保持旧 API 不变
- [x] 2.8 实现 `FrameGraph::buildFromScene(const Scene &)`：对每个 `FramePass`，清空 `queue`，遍历 `scene.getRenderables()`，调用 `scene.buildRenderingItemForRenderable(r, pass.name)` 加入 queue，最后 `pass.queue.sort()`；如果 `renderable->getPassMask()` 与 pass 不匹配则 skip
- [x] 2.9 实现 `FrameGraph::collectAllPipelineBuildInfos()`：拼接每个 pass 的 `collectUniquePipelineBuildInfos()`，总结果按 `PipelineKey` 再去重一次
- [x] 2.10 迁移 `scene->mesh->...` 调用点：`vk_renderer.cpp`、`test_vulkan_pipeline.cpp`、`test_vulkan_resource_manager.cpp`、`test_vulkan_command_buffer.cpp`、`scene.cpp` 本身。`grep -rn "scene->mesh\|scene\.mesh" src/` 确认归零
- [x] 2.11 新建 `src/test/integration/test_frame_graph.cpp`：构造一个含一个 renderable 的 Scene，手动 addPass(Pass_Forward)，调 buildFromScene + collectAllPipelineBuildInfos，断言返回 1 个条目；再加一个 renderable 同 template → 1 个（去重）；不同 variant → 2 个
- [x] 2.12 把 `test_frame_graph` 加入 `TEST_INTEGRATION_EXE_LIST`
- [x] 2.13 全量 build + `./build/src/test/test_frame_graph` 通过；所有现有 `test_vulkan_*` 仍然绿

## 3. Stage 3a — IRenderResource::getBindingName 迁移

- [x] 3.1 在 `src/core/gpu/render_resource.hpp` `IRenderResource` 类里添加 `virtual StringID getBindingName() const { return StringID{}; }`；include `core/utils/string_table.hpp`
- [x] 3.2 `src/core/scene/camera.hpp`：添加 `StringID getBindingName() const override { return StringID("CameraUBO"); }`；删除 `getPipelineSlotId` override
- [x] 3.3 `src/core/scene/light.hpp`：`getBindingName() -> "LightUBO"`；删 `getPipelineSlotId`
- [x] 3.4 `src/core/resources/skeleton.hpp`：`SkeletonUBO::getBindingName() -> "Bones"`（或查 shaderc 反射对 skeleton buffer 的命名；确认 GLSL 里叫什么）；删 `getPipelineSlotId`
- [x] 3.5 `src/core/resources/material.hpp`：`UboByteBufferResource::getBindingName() -> "MaterialUBO"`；删 `getPipelineSlotId`
- [x] 3.6 `src/core/resources/texture.hpp`：`CombinedTextureSampler` 删除 `m_slotId` 字段与构造参数 `PipelineSlotId slotId`；`getBindingName()` 返回 `StringID{}`（走 `MaterialInstance::m_textures` key 路径，不用自己知道名字）；更新所有构造调用点
- [x] 3.7 `grep -rn "CombinedTextureSampler(" src/` 找到所有调用点，删除 `PipelineSlotId::...` 参数
- [x] 3.8 删除 `enum class PipelineSlotId` 与其全部引用：`src/core/gpu/render_resource.hpp`
- [x] 3.9 删除 `IRenderResource::getPipelineSlotId` 虚函数
- [x] 3.10 `grep -rn "PipelineSlotId\|getPipelineSlotId" src/` 确认归零

## 4. Stage 3b — VulkanPipeline 反射化

- [x] 4.1 `src/backend/vulkan/details/pipelines/vkp_pipeline.hpp`：`m_slots` → `m_bindings: std::vector<LX_core::ShaderResourceBinding>`；`getSlots()` → `getBindings()`；include 改 `core/resources/shader.hpp`
- [x] 4.2 `vkp_pipeline.hpp`：构造函数签名改为 `VulkanPipeline(Token, VulkanDevice &, const PipelineBuildInfo &, VkRenderPass)`；删除旧 `VkExtent2D extent / shaderName / PipelineSlotDetails* / slotCount / PushConstantDetails` 参数
- [x] 4.3 `vkp_pipeline.cpp::createLayout()` 按 `m_bindings` 分组（`unordered_map<uint32_t setIdx, vector<VkDescriptorSetLayoutBinding>>`）构造 descriptor set layouts；每个 binding 通过 helper `toVkDescriptorType(ShaderPropertyType)` 和 `toVkStageFlags(ShaderStage)` 转换
- [x] 4.4 在 `vkp_pipeline.cpp` 顶部添加 anonymous namespace helpers `VkDescriptorType toVkDescriptorType(LX_core::ShaderPropertyType)` 与 `VkShaderStageFlags toVkStageFlags(LX_core::ShaderStage)`
- [x] 4.5 `vkp_shader_graphics.hpp/cpp`：`create` 签名改为 `create(VulkanDevice &, const PipelineBuildInfo &, VkRenderPass)`；内部直接 forward 到 `VulkanPipeline` 构造函数；`m_shaderBaseName` 从 `buildInfo.key` 或 metadata 取（或保留为空 — 仅用于 debug）
- [x] 4.6 `vkp_pipeline.cpp` 中 `getVertexInputStateCreateInfo` 从 `m_bindings` 的 `VertexLayout`（或新增字段 `m_vertexLayout`）生成 `VkVertexInputAttributeDescription`；确定 vertexLayout 存在哪里 — 决定：在 `VulkanPipeline` 里新增 `m_vertexLayout: VertexLayout` 字段，由构造函数从 `buildInfo` 拷入
- [x] 4.7 `vkp_pipeline.cpp` 中 input assembly 的 topology 从 `m_topology`（新字段，由构造函数从 `buildInfo.topology` 拷入）生成
- [x] 4.8 `vkp_pipeline.cpp` 中 rasterization / depth / blend 从 `m_renderState`（新字段）生成；添加 helper `VkCullModeFlags toVkCull(LX_core::CullMode)` 等
- [x] 4.9 `buildGraphicsPpl` 接受 `VkRenderPass` 参数不变，内部从 `m_renderState` 构造 `VkPipelineRasterizationStateCreateInfo` 等
- [x] 4.10 删除 `vkp_shader_graphics.hpp` 中 `m_shaderBaseName`、`m_vertexLayout`、`m_vkTopology` 等冗余字段（基类已持有）
- [x] 4.11 保留 `VulkanPipeline::getShaderName()`（从 buildInfo metadata 或空字符串）用于日志

## 5. Stage 3c — DescriptorManager 反射化

- [x] 5.1 `src/backend/vulkan/details/descriptors/vkd_descriptor_manager.hpp`：`DescriptorLayoutKey` 改为 `struct { std::vector<LX_core::ShaderResourceBinding> bindings; bool operator==(...) const; }`
- [x] 5.2 `DescriptorLayoutHasher::operator()` 按 `(set, binding, type, stageFlags, descriptorCount)` 五元组 `hash_combine`；**不要**把 `name` 放进 hash
- [x] 5.3 `getOrCreateLayout` 签名改为 `VkDescriptorSetLayout getOrCreateLayout(const std::vector<LX_core::ShaderResourceBinding> &bindings)`；按 binding 的 `set` 字段过滤（调用方保证传入单 set 的 binding 子集）
- [x] 5.4 `allocateSet` 签名同步改为 `DescriptorSetUniquePtr allocateSet(const std::vector<LX_core::ShaderResourceBinding> &bindings)`
- [x] 5.5 `getOrCreateLayout` 内部构造 `VkDescriptorSetLayoutBinding` 时从 `ShaderResourceBinding` 字段转换：`b.binding`、`toVkDescriptorType(type)`、`b.descriptorCount`、`toVkStageFlags(stageFlags)`
- [x] 5.6 迁移 `vkp_pipeline.cpp::createLayout()` 的调用点以新签名调用

## 6. Stage 3d — CommandBuffer 按名字绑定

- [x] 6.1 `src/backend/vulkan/details/commands/vkc_cmdbuffer.cpp::bindResources` 删除 `findBySlotId` 与 `setGroups: unordered_map<uint32_t, vector<PipelineSlotDetails>>`
- [x] 6.2 在 `bindResources` 顶部构造 `std::unordered_map<StringID, IRenderResourcePtr, StringID::Hash> resourceByName` 从 `item.descriptorResources` 填充（只收录 `getBindingName() != StringID{}` 的资源，外加 material 的 textures —— 这些由 `MaterialInstance::getDescriptorResources()` 已返回，但 `CombinedTextureSampler::getBindingName()` 返回空 … 要解决这个断链）
- [x] 6.3 **断链处理**：纹理路径不在 `getBindingName()` 上；`MaterialInstance::getDescriptorResources()` 返回的纹理顺序已按 `(set, binding)` 排序，但 bind 时需要知道 set/binding。方案 A（优先）：让 `CombinedTextureSampler` 持有一个 `StringID m_bindingName`，由 `MaterialInstance::getDescriptorResources()` 在返回前通过 `m_template->findBinding(id)` 反查写入 —— 这个修改在纹理路径非常局部；方案 B：在 `bindResources` 里做 `(pipeline bindings × item.descriptorResources)` 的按类型+顺序匹配（fragile）—— **选 A**
- [x] 6.4 实现方案 A：`MaterialInstance::getDescriptorResources()` 在 push 纹理前调 `tex->setBindingName(id)`（`CombinedTextureSampler` 新增非 const setter）
- [x] 6.5 `CombinedTextureSampler::getBindingName()` 返回 `m_bindingName`
- [x] 6.6 `bindResources` 循环：按 pipeline 的 `m_bindings` 分组为 `setGroups: unordered_map<uint32_t set, vector<ShaderResourceBinding>>`；对每个 set：`allocateSet(bindings_of_set)`，然后对每个 binding 在 `resourceByName` 里按 name 查，匹配后 `updateBuffer`/`updateImage`
- [x] 6.7 删除 `PipelineSlotDetails` 相关的 include：`vkc_cmdbuffer.cpp` 里的 `pipeline_slot.hpp`
- [x] 6.8 `grep -rn "PipelineSlotDetails\|findBySlotId" src/` 确认归零（除开本次要删除的文件自身）

## 7. Stage 3e — 删除 PipelineSlotDetails / forward_pipeline_slots / 旧 slot 表

- [x] 7.1 删除 `src/backend/vulkan/details/pipelines/vkp_pipeline_slot.hpp`
- [x] 7.2 删除 `src/backend/vulkan/details/pipelines/forward_pipeline_slots.hpp`
- [x] 7.3 `vk_resource_manager.cpp` 删除 `#include "pipelines/forward_pipeline_slots.hpp"`、`#include "vkp_pipeline_slot.hpp"` 及所有 `PipelineSlotDetails` / `blinnPhongForwardSlots` / `blinnPhongPushConstants` 引用
- [x] 7.4 `vk_resource_manager.cpp::getOrCreateRenderPipeline` 的实现改为 stage 3f 的薄壳（见下）

## 8. Stage 3f — PipelineCache 独立类 + preload 链路

- [x] 8.1 新建 `src/backend/vulkan/details/pipelines/pipeline_cache.hpp`：`class PipelineCache { PipelineCache(VulkanDevice &); find / getOrCreate / preload; }`
- [x] 8.2 新建 `pipeline_cache.cpp`：`find` 只查 `m_cache`；`getOrCreate` miss 时构造 `VulkanShaderGraphicsPipeline::create(m_device, info, renderPass)` 并写入缓存，如果 `!m_suppressMissWarning` 则打印 `warn("pipeline cache miss: " + toDebugString(info.key.id))`
- [x] 8.3 `preload(infos, renderPass)` 翻转 `m_suppressMissWarning = true`，循环 `getOrCreate`，结束后复位
- [x] 8.4 `VulkanResourceManager` 持有 `std::unique_ptr<PipelineCache> m_pipelineCache`；构造函数（或 `initializeRenderPassAndPipeline`）初始化
- [x] 8.5 `VulkanResourceManager::getOrCreateRenderPipeline(item)` 改为：`return m_pipelineCache->getOrCreate(PipelineBuildInfo::fromRenderingItem(item), m_renderPass->getHandle());`
- [x] 8.6 从 `VulkanResourceManager` 删除 `std::unordered_map<PipelineKey, VulkanPipelineUniquePtr, PipelineKey::Hash> m_pipelines`
- [x] 8.7 新建 `VulkanResourceManager::preloadPipelines(const std::vector<PipelineBuildInfo> &infos)` 转发到 `m_pipelineCache->preload(infos, m_renderPass->getHandle())`
- [x] 8.8 `src/backend/vulkan/vk_renderer.cpp::initScene`：在 syncResource 循环之后，构造 `FrameGraph`，`addPass(FramePass{ Pass_Forward, {}, {} })`，`buildFromScene(*scene)`，`auto infos = frameGraph.collectAllPipelineBuildInfos()`，`resourceManager->preloadPipelines(infos)`
- [x] 8.9 新建 `src/test/integration/test_pipeline_cache.cpp`（GPU 路径，参照 `test_vulkan_pipeline.cpp` 的 device 初始化样式）：初始化 device → 构造 renderPass → 构造 PipelineBuildInfo（由 blinnphong_material_loader 驱动）→ PipelineCache → find 返回 nullopt → getOrCreate 返回有效 pipeline + cache size == 1 → find 返回非空 → preload 额外 infos 无 warn
- [x] 8.10 把 `test_pipeline_cache` 加入 `TEST_INTEGRATION_EXE_LIST`
- [x] 8.11 `test_vulkan_pipeline.cpp`、`test_vulkan_resource_manager.cpp`、`test_vulkan_command_buffer.cpp`：确认 `getOrCreateRenderPipeline(item)` 仍然可用（通过薄壳）；若编译失败则修

## 9. 回归 + clang-format + 归档

- [x] 9.1 `clang-format -i` 本次新建和修改的所有 `.hpp` / `.cpp`
- [x] 9.2 `cmake --build ./build` 全量绿，0 error 0 warning
- [x] 9.3 `./build/src/test/test_pipeline_build_info` / `test_frame_graph` / `test_pipeline_cache` / `test_string_table` / `test_pipeline_identity` / `test_material_instance` 全部通过
- [x] 9.4 `./build/src/test/test_render_triangle` 能够成功初始化（关键回归指标：preload 走通 + 一个真实 pipeline 被构造 + 渲染不崩）
- [x] 9.5 `./build/src/test/test_vulkan_pipeline` / `test_vulkan_resource_manager` / `test_vulkan_command_buffer` 通过
- [x] 9.6 `openspec validate pipeline-prebuilding --strict` 通过
- [x] 9.7 `docs/requirements/003b-pipeline-prebuilding.md` 的"实施状态"更新为已完成 + 迁移到 `docs/requirements/finished/`
