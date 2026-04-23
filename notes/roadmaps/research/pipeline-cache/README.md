# Pipeline Cache 技术调研

> 状态：**应用层已实现 / 驱动层未接入**
> 最后更新：2026-04-23
> 目的：记录 Vulkan 两种"pipeline cache"的含义、当前 LX Engine 的实现程度、与书本（*Mastering Graphics Programming with Vulkan*）方案的差异，以及将来是否接入 `VkPipelineCache` 底层机制的判断。
>
> 注：`notes/subsystems/pipeline-cache.md` 是 **当前子系统设计** 的权威内部文档；本目录是 **技术演进调研**，重点在"和业界做法的对比 + 将来要不要扩展"，两者互补不重复。

## 这个目录是什么

"Pipeline cache" 在 Vulkan 上其实是 **两件不同的事**，经常被混为一谈：

1. **应用层 pipeline 对象缓存** —— 按 `PipelineKey` 复用已经创建的 `VkPipeline` 句柄，避免同一进程内重复 create
2. **驱动层 pipeline 二进制缓存** —— Vulkan 原生的 `VkPipelineCache` 对象 + 磁盘持久化，把"驱动编译的中间产物"跨进程、跨启动复用

这两件事 **解决的是不同阶段的耗时**。这个目录记录两者关系、LX Engine 的实现状态、以及将来的演进判断。

## 建议阅读顺序

| # | 文档 | 讲什么 |
|---|------|-------|
| 01 | [两种缓存的区别](01-两种缓存的区别.md) | 应用层对象缓存 vs 驱动层二进制缓存，各自解决什么瓶颈 |
| 02 | [LX Engine 当前实现](02-LX当前实现.md) | 我们做到了什么：`PipelineCache` 类 + `PipelineKey` 索引 + FrameGraph 预加载 |
| 03 | [书本方案与 VkPipelineCache](03-书本方案与VkPipelineCache.md) | 书里讲的 `vkCreatePipelineCache` + 磁盘持久化 + header 校验 |
| 04 | [演进路径](04-演进路径.md) | 是否/何时把 `VkPipelineCache` 接入，预期收益与成本 |

## TL;DR

- **应用层对象缓存**：✅ **LX Engine 已实现**。见 `src/backend/vulkan/details/pipelines/pipeline_cache.{hpp,cpp}`，契约在 `openspec/specs/pipeline-cache/spec.md`。提供 `find / getOrCreate / preload` 三入口，FrameGraph 驱动在 `initScene` 时预构建所有 pipeline
- **驱动层二进制缓存**：❌ **LX Engine 未接入**。`vkCreateGraphicsPipelines` 调用时传的是 `VK_NULL_HANDLE`，未使用 `VkPipelineCache`，也没有磁盘持久化
- **结论**：我们节省了"同一进程内重复创建 pipeline"的时间，但**没有节省"驱动每次启动都要从 SPIR-V 编译成硬件指令"的时间**
- **是否要加**：**值得加，但不紧迫**。目前 pipeline 数量少、启动时间不敏感。等 pipeline 数量过百、或 AOT 预编译流程成型时再接。详见 [04 · 演进路径](04-演进路径.md)

## 两件事的关系速查

| 维度 | 应用层对象缓存（LX 已做） | 驱动层二进制缓存（书本方案） |
|------|-----------------------|---------------------------|
| 粒度 | `VkPipeline` 对象（整个 pipeline） | 中间编译产物（可跨多个 pipeline 共享基础块） |
| 避免的成本 | 重复调 `vkCreateGraphicsPipelines` | 驱动把 SPIR-V 编译成 GPU 指令的时间 |
| 持久化 | 进程生命周期内内存（进程退出即失效） | 可序列化到磁盘，跨启动复用 |
| 主要场景 | "同一 pipeline 反复被同一帧请求" | "首次启动 / 切关卡 / 驱动升级后" |
| API 入口 | 自定义 `PipelineCache::getOrCreate` | `VkPipelineCacheCreateInfo` + `vkGetPipelineCacheData` |
| LX 状态 | 已实现 | 未接入 |
| 适用约束 | 无 | 驱动升级 / 设备更换会导致缓存失效，需 header 校验 |

这两层**不互斥，是叠加的**：你同时拥有它们时，首次启动慢（驱动编译）但有磁盘缓存，下次启动快；进程内同一 pipeline 反复用时零开销（对象命中）。

## 参考

- *Mastering Graphics Programming with Vulkan* · Chapter 2 · "Improving load times with a pipeline cache"
- [Vulkan 规范 · Pipeline Caches](https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#pipelines-cache)
- LX Engine 内部契约：`openspec/specs/pipeline-cache/spec.md`
- LX Engine 子系统设计文档：`notes/subsystems/pipeline-cache.md`
- LX Engine 实现：`src/backend/vulkan/details/pipelines/pipeline_cache.{hpp,cpp}`
