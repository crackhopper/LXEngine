# Pipeline Cache

`PipelineCache` 是 Vulkan backend 的 pipeline 存储层。它不理解 scene，也不从旧 work item 反推 pipeline；当前输入来自 `RenderInputDesc.pipelineBuildDesc`。

## 当前链路

```text
RenderWorkCompiler::prepare()
  -> RenderInputDesc.pipelineBuildDesc
  -> VulkanResourceManager::preloadPipelines(descs)
  -> PipelineCache::getOrCreatePipeline(desc, renderPass)
  -> VulkanGraphicsPipeline / VulkanComputePipeline
```

## 核心 API

| API | 当前职责 |
|---|---|
| `find(key)` | 只查 graphics cache，不创建 |
| `getOrCreate(desc, renderPass)` | 创建或返回 graphics pipeline |
| `getOrCreateCompute(desc)` | 创建或返回 compute pipeline |
| `getOrCreatePipeline(desc, renderPass)` | 按 `PipelineBuildType` 返回 `VulkanPipelineRef` |
| `preload(descs, renderPass)` | 批量预构建 pipeline，幂等执行 |

## 关键约束

- `PipelineKey` 只回答“是不是同一条 pipeline”。
- `PipelineBuildDesc` 回答“创建 pipeline 需要哪些 shader、binding、layout、target、render state 和 attachment facts”。
- graphics 和 compute pipeline 共用统一 cache facade，但落到不同内部 map。
- miss 时允许现场补建，并保持可观察日志。
- target/attachment 差异通过 RenderPathNode signature 与 `PipelineBuildDesc` 表达，不重新引入独立 target key 轴。
- preload 输入必须来自 accepted `RenderInputDesc`，不要从 scene 或 material 层旁路拼 pipeline。

## 从哪里改

| 想改什么 | 入口 |
|---|---|
| pipeline key 组成 | `src/core/pipeline/pipeline_key.*` |
| pipeline build facts | `src/core/pipeline/pipeline_build_desc.*` |
| desc 生成 | `src/core/frame_graph/render_work_compiler.*` |
| Vulkan pipeline cache | `src/backend/vulkan/details/pipelines/` |
| runtime miss 行为 | `src/backend/vulkan/details/resource_manager.*` |

## 关联文档

- [Pipeline Identity](../source_analysis/src/core/pipeline/pipeline_identity.md)
- [RenderWorkCompiler](../concepts-design/rendering-pipeline/render-work-compiler.md)
