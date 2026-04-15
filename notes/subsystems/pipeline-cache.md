# Pipeline Cache

> `PipelineCache` 是 Vulkan backend 里的 pipeline 存储层。它只做三件事：预构建、查找、必要时补建。
>
> 权威 spec: `openspec/specs/pipeline-cache/spec.md`

## 它解决什么问题

- 把 pipeline map 从 `VulkanResourceManager` 里拆出来。
- 让“预期已存在”和“允许现场创建”变成两个明确 API。
- 提供 preload 入口，避免首帧抖动。

## 核心对象

- `find(key)`：只查，不建。
- `getOrCreate(desc, renderPass)`：查不到就建，并打 warning。
- `preload(descs, renderPass)`：批量预构建，不打 warning。
- `m_cache`：`std::unordered_map<PipelineKey, VulkanPipelinePtr, PipelineKey::Hash>`，由 cache 自己拥有 pipeline 生命周期。

## 典型数据流

1. `FrameGraph` 收集 `PipelineBuildDesc`。
2. `PipelineCache::preload(...)` 预构建。
3. `VulkanResourceManager::preloadPipelines(...)` 只是转发到 `PipelineCache::preload(...)`。
4. draw 时 `VulkanResourceManager::getOrCreateRenderPipeline(item)` 直接调用 `PipelineCache::getOrCreate(...)`。
5. 如果 preload 漏了，运行时 miss 会现场补建并打印 warning。

## 关键约束

- `find` 绝不能偷偷创建 pipeline。
- `getOrCreate` miss 时必须可观测，方便排查 preload 漏项。
- `preload` 必须幂等。
- `PipelineKey` 和 `PipelineBuildDesc` 分工明确，不能混用。
- `getOrCreate(...)` 命中时直接返回缓存里的 `VulkanPipeline&`；miss 时先构建 `VulkanShaderGraphicsPipeline`，再把 `unique_ptr` 放进 `m_cache`。
- `preload(...)` 当前不是单独的构建路径，而是通过一个 `m_suppressMissWarning` 标志临时关闭日志，然后循环调用 `getOrCreate(...)`。

## 当前实现边界

- `find(...)` 已实现，但当前 renderer 热路径没有先查 `find()` 再 fallback，而是直接走 `getOrCreate(...)`。
- `VulkanResourceManager` 仍保留 `getOrCreateRenderPipeline(item)` 这个旧入口，但它已经只是对 `PipelineCache` 的转发层。
- miss 日志会打印 `GlobalStringTable::toDebugString(info.key.id)`，用于定位是哪一类 pipeline 没被 preload 到。
- 当前并没有单独的 eviction / LRU / 容量上限逻辑；cache 生命周期就是 renderer 生命周期内常驻。

## 从哪里改

- 想改缓存策略：看 `PipelineCache`。
- 想改 preload 输入来源：看 `FrameGraph`。
- 想改运行时 miss 行为：看 `getOrCreate(...)` 的 warning 和 fallback 路径。

## 关联文档

- `openspec/specs/pipeline-cache/spec.md`
- `notes/subsystems/pipeline-identity.md`
- `notes/subsystems/frame-graph.md`
