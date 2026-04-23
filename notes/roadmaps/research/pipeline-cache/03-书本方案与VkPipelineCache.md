# 03 · 书本方案与 `VkPipelineCache`

> 这一章梳理 *Mastering Graphics Programming with Vulkan*（第 2 章 Improving Resources Management）给出的 pipeline cache 方案，以及 Vulkan 原生 `VkPipelineCache` 对象的工作机制。
> 核心要明白的是：**它解决的是"驱动更快地编译这条 pipeline"**，和 [02](02-LX当前实现.md) 里 LX 的对象缓存不是一件事。

## 3.1 一句话定位

**`VkPipelineCache`：解决"驱动更快地编译这条 pipeline"。**

当你调 `vkCreateGraphicsPipelines` 时，驱动要：

1. 解析 SPIR-V
2. 把 SPIR-V 编译成目标 GPU 的 ISA（最慢的一步）
3. 根据 pipeline state 生成硬件寄存器配置

`VkPipelineCache` 允许驱动把编译的中间/最终结果记下来，下次遇到相似 shader 时跳过第 2 步。

## 3.2 书里的典型代码路径

### 修改 create API 接收缓存路径

```cpp
GpuDevice::create_pipeline(const PipelineCreation& creation,
                           const char* cache_path)
```

### 尝试从磁盘读取现有缓存

```cpp
VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
VkPipelineCacheCreateInfo pipeline_cache_create_info {
    VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };

if (cache_exists) {
    FileReadResult read_result = file_read_binary(cache_path, allocator);
    pipeline_cache_create_info.initialDataSize = read_result.size;
    pipeline_cache_create_info.pInitialData    = read_result.data;
}

vkCreatePipelineCache(vulkan_device, &pipeline_cache_create_info,
                      vulkan_allocation_callbacks, &pipeline_cache);
```

### 创建 pipeline 时传入缓存对象

```cpp
vkCreateGraphicsPipelines(vulkan_device,
                          pipeline_cache,      // ← 这里！LX 当前传的是 VK_NULL_HANDLE
                          1, &pipeline_info,
                          vulkan_allocation_callbacks,
                          &pipeline->vk_pipeline);
```

### 退出前回写磁盘

```cpp
size_t cache_data_size = 0;
vkGetPipelineCacheData(vulkan_device, pipeline_cache, &cache_data_size, nullptr);

void* cache_data = allocator->allocate(cache_data_size, 64);
vkGetPipelineCacheData(vulkan_device, pipeline_cache, &cache_data_size, cache_data);

file_write_binary(cache_path, cache_data, cache_data_size);
vkDestroyPipelineCache(vulkan_device, pipeline_cache, vulkan_allocation_callbacks);
```

## 3.3 Header 校验 —— 关键但容易漏的一步

磁盘 blob 的前面是一段固定格式的 header：

```cpp
struct VkPipelineCacheHeaderVersionOne {
    uint32_t                     headerSize;
    VkPipelineCacheHeaderVersion headerVersion;
    uint32_t                     vendorID;
    uint32_t                     deviceID;
    uint8_t                      pipelineCacheUUID[VK_UUID_SIZE];
};
```

载入 blob 时**必须**手动比对这几个字段：

```cpp
auto* hdr = (VkPipelineCacheHeaderVersionOne*)read_result.data;
if (hdr->deviceID == phys_props.deviceID &&
    hdr->vendorID == phys_props.vendorID &&
    memcmp(hdr->pipelineCacheUUID,
           phys_props.pipelineCacheUUID, VK_UUID_SIZE) == 0) {
    // 有效，喂给 initialData
    create_info.initialDataSize = read_result.size;
    create_info.pInitialData    = read_result.data;
} else {
    // 失效：当作没有 cache，重新生成
    cache_exists = false;
}
```

**任何下列变化都会让缓存作废**：

- 驱动升级（`pipelineCacheUUID` 变）
- 换 GPU（`deviceID` 变）
- 换厂商（`vendorID` 变）

驱动遇到不匹配时**不保证**优雅降级 —— 所以应用必须自己做前置校验，不匹配就当没缓存。

## 3.4 `VkPipelineCache` 缓存的究竟是什么

规范层面只说"实现相关的内部数据"，不承诺具体格式。实践上它保存的是：

- SPIR-V 解析后的 IR（legalized、优化过的）
- 目标 GPU ISA 二进制片段
- pipeline state 相关的硬件寄存器配置片段

**key 是驱动内部的（不可见）**，粗略是 SPIR-V 字节 + pipeline state 的某种哈希。所以即使你两次请求的 `VkPipeline` 句柄不同、`PipelineKey` 不同，只要 shader 字节完全一致，驱动也可能跨命中。

对应对象层次：

| 层次 | 缓存内容 | key 定义者 |
|------|---------|----------|
| LX `PipelineCache` | `VulkanPipeline` 对象整体 | 引擎（`PipelineKey`） |
| `VkPipelineCache` | 驱动内部编译中间/最终产物 | 驱动内部（对应用不可见） |

## 3.5 `VkPipelineCache` 相比 LX 的 `PipelineCache` 有什么优势

它的优势集中在 **"首次创建时更快"，特别是跨进程、跨下次启动时**：

- 复用驱动已经做过的 shader/pipeline 编译工作
- "程序重启后重新建同一批 pipeline" 走快路径
- 对大量 pipeline 的加载时间、关卡切换时间有帮助

LX 当前的 `PipelineCache` 做不到这些，因为它：

- 只活在当前进程内存里
- 进程退出就没了
- 下次启动还是要重新调 `vkCreateGraphicsPipelines` 走完整编译

**核心收益：冷启动加速 + 跨运行复用驱动编译结果。**

## 3.6 它拿不到 LX `PipelineCache` 那套能力

反过来，即使你给每个 create 都传 `VkPipelineCache`，它也**不会**告诉你：

- 这条 pipeline 是否已经被引擎建过（没有 `PipelineKey` 级身份）
- preload 是否漏项（驱动不管预构建契约）
- 哪个 `PipelineKey` 导致了 miss（驱动不认识 `PipelineKey`）
- scene 初始化阶段应该提前建哪些 pipeline（驱动不感知 scene）
- 不对外暴露"命中/miss"的可观测接口（它只是默默帮你 / 不帮你）

也不能帮你：

- 管理 `VulkanPipeline` 生命周期
- 复用 descriptor layout / pipeline layout / shader module 这些上层对象

驱动说白了只做一件事："**你每次传同样的 `VkGraphicsPipelineCreateInfo`，我最多帮你更快地建**"。其余全是引擎侧的职责。

## 3.7 为什么不能直接用 `VkPipelineCache` 替代 LX 的实现

如果去掉 LX 的 `PipelineCache`、只用 `VkPipelineCache`，会退化成：

- 每次 draw 或 preload 都得重新调 `vkCreateGraphicsPipelines`
- 只是驱动内部可能加速
- 引擎侧仍然缺少真正的对象缓存与身份管理
- 每次还要重复分配上层对象（descriptor layout、pipeline layout、shader module）

**这不对**。所以正确的做法是 **两层叠加** —— 引擎层做对象缓存 + 身份管理，驱动层做编译加速。具体怎么叠见 [04 · 演进路径](04-演进路径.md)。

## 下一步

- 看两层怎么结合、LX 实际怎么接入 → [04 · 演进路径](04-演进路径.md)
