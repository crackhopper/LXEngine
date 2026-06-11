# Pipeline Cache

> `PipelineCache` 是 Vulkan backend 里的 pipeline 存储层。它只做三件事：预构建、查找、必要时补建。
>
> 当前事实以 `src/backend/vulkan/details/pipelines/`、`src/core/pipeline/` 和本页说明为准。

## 它解决什么问题

- 把 pipeline map 从 `VulkanResourceManager` 里拆出来。
- 让“预期已存在”和“允许现场创建”变成两个明确 API。
- 提供 preload 入口，避免首帧抖动。

## 核心对象

| 对象 / API | 当前职责 |
|---|---|
| `find(key)` | 只查 graphics cache，不创建 |
| `getOrCreate(desc, renderPass)` | 创建或返回 `VulkanGraphicsPipeline&`，输入必须是 graphics desc |
| `getOrCreateCompute(desc)` | 创建或返回 `VulkanComputePipeline&`，输入必须是 compute desc |
| `getOrCreatePipeline(desc, renderPass)` | 按 `PipelineBuildType` 返回 `VulkanPipelineRef` |
| `preload(descs, renderPass)` | 批量预构建 graphics / compute pipeline，不打 warning |
| `m_cache` | `std::unordered_map<PipelineKey, VulkanGraphicsPipelineUniquePtr, PipelineKey::Hash>` |
| `m_computeCache` | `std::unordered_map<PipelineKey, VulkanComputePipelineUniquePtr, PipelineKey::Hash>` |

## 典型数据流

1. `FrameGraph` 收集 `PipelineBuildDesc`。
2. `PipelineCache::preload(...)` 预构建。
3. `VulkanResourceManager::preloadPipelines(...)` 只是转发到 `PipelineCache::preload(...)`。
4. 执行 `RenderWorkItem` 时，`VulkanResourceManager::getOrCreatePipeline(item)` 从 item 派生 build desc，再调用 `PipelineCache::getOrCreatePipeline(...)`。
5. 如果 preload 漏了，运行时 miss 会现场补建并打印 warning。

## 关键约束

- `find` 绝不能偷偷创建 pipeline。
- `getOrCreate` miss 时必须可观测，方便排查 preload 漏项。
- `preload` 必须幂等。
- `PipelineKey` 和 `PipelineBuildDesc` 分工明确，不能混用。
- `getOrCreate(...)` 命中时直接返回缓存里的 `VulkanGraphicsPipeline&`；miss 时先构建 `VulkanShaderGraphicsPipeline`，再把 `unique_ptr` 放进 `m_cache`。
- `getOrCreateCompute(...)` 命中时直接返回缓存里的 `VulkanComputePipeline&`；miss 时创建 compute pipeline，再放进 `m_computeCache`。
- `getOrCreatePipeline(...)` 是统一入口，返回 `VulkanPipelineRef`，让 command buffer 在 bind 阶段再分发 graphics / compute。
- `preload(...)` 当前不是单独的构建路径，而是通过一个 `m_suppressMissWarning` 标志临时关闭日志，然后循环调用 `getOrCreatePipeline(...)`。

## 当前实现边界

- `find(...)` 已实现，但当前 renderer 热路径没有先查 `find()` 再 fallback，而是直接走 `getOrCreatePipeline(...)`。
- `VulkanResourceManager` 对外暴露统一的 `getOrCreatePipeline(item)`，不再区分 graphics-only 和 compute-only 调用入口。
- miss 日志会打印 `GlobalStringTable::toDebugString(info.key.id)`，用于定位是哪一类 pipeline 没被 preload 到。
- 当前并没有单独的 eviction / LRU / 容量上限逻辑；cache 生命周期就是 renderer 生命周期内常驻。

## 从哪里改

- 想改缓存策略：看 `PipelineCache`。
- 想改 preload 输入来源：看 `FrameGraph`。
- 想改运行时 miss 行为：看 `getOrCreate(...)` 的 warning 和 fallback 路径。

## 关联文档

- `notes/source_analysis/src/core/pipeline/pipeline_identity.md`
- `notes/source_analysis/src/core/frame_graph/frame_graph.md`
