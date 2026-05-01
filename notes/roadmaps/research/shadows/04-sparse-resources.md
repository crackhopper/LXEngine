# 04 · Sparse Resources

> 阅读前提：[03](03-mesh-shader-shadow.md) 讲完了 shadow 渲染。本文展开 *Vulkan sparse residency* —— 让多光源 shadow 内存可控的关键能力。
>
> **本文范围超出 shadow**：sparse resources 是跨主题的 Vulkan 基础能力，未来 streaming texture / virtual geometry / virtual UV 也都会用。

## 4.1 朴素方案的内存账

256 光源 × 1024×1024 cubemap × 6 face × 4 字节（depth32f）：

```
256 × 1024 × 1024 × 6 × 4 = 6.4 GB
```

不可接受。降到 256×256：

```
256 × 256 × 256 × 6 × 4 = 400 MB
```

仍然太多，且 *远处小光源用不到 256² 分辨率，是浪费*。

## 4.2 关键观察

- 远处的小光源 → 屏幕上几个 pixel → 256² 浪费
- 近处的大光源 → 占半个屏幕 → 1024² 才够
- 完全被剔除的光源 → 0 也行
- 大量 cubemap face 在场景中 *根本没物体* → 不需要分配

但传统资源 API *必须预分配最坏情况*。**Sparse resources 让资源能 *按需绑定 GPU 内存***。

## 4.3 Sparse 的两种模式

Vulkan 提供：

| 模式 | flag | 含义 |
|------|------|------|
| **Sparse Binding** | `SPARSE_BINDING_BIT` | 资源可绑非连续 memory，但必须 *全部* 绑定 |
| **Sparse Residency** | `SPARSE_RESIDENCY_BIT` | 资源可 *部分* 绑定（典型用法） |
| Sparse Aliased | `SPARSE_ALIASED_BIT` | 多个资源可绑同一 memory（高级用法） |

Shadow 和 streaming texture 都用 **sparse residency**。

```cpp
VkImageCreateInfo image_info = {...};
image_info.flags = VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT
                 | VK_IMAGE_CREATE_SPARSE_BINDING_BIT;
```

## 4.4 工作原理

```
逻辑资源（cubemap array）：
   logical view: 256 cubemaps × 1024×1024 × 6 layers
   
GPU memory pool（预分配）：
   一组固定大小的 page (通常 64 KB / 128 KB / 256 KB)
   
绑定（每帧动态）：
   per-frame 决定每个 cubemap layer 实际绑定到 pool 中的哪几页
   未绑定的部分访问行为 undefined（一般 reads as 0）
```

**逻辑资源是大的（容量），物理 memory 是小的（实际占用）**。中间通过 page mapping 表关联。

类似 OS 的 *虚拟内存*：每个进程看到 4 GB 地址空间，物理 RAM 可能只有 16 GB（多进程共享）。

## 4.5 Page Size 是硬件约束

不能任意大小的内存绑给资源 — 必须按 *硬件支持的 page size* 操作：

```cpp
VkPhysicalDeviceSparseImageFormatInfo2 format_info = {...};
format_info.format  = texture->vk_format;
format_info.type    = ...;
format_info.usage   = texture->vk_usage;
format_info.tiling  = VK_IMAGE_TILING_OPTIMAL;

vkGetPhysicalDeviceSparseImageFormatProperties2(...);

u32 block_width  = props.imageGranularity.width;
u32 block_height = props.imageGranularity.height;
```

每个 GPU 有自己的 sparse block size，跟 format 有关：

| Bits per pixel | Block shape (2D) |
|---------------|-------------------|
| 8-bit | 256×256 |
| 16-bit | 256×128 |
| 32-bit | 128×128 |
| 64-bit | 128×64 |
| 128-bit | 64×64 |

绑定的 *最小单位* 是一个 block。不能更细。

## 4.6 Page Pool 模式

**预分配一大池 page，运行期分配**：

```cpp
// 1. 启动期：分配 N 个固定大小 page
u32 block_count = pool_size / (block_width * block_height);

VmaAllocationCreateInfo allocation_info = {...};
VkMemoryRequirements page_memory_requirements = {
    .size      = memory_requirements.alignment,  // 一个 page 的大小
    .alignment = memory_requirements.alignment,
    ...
};

vmaAllocateMemoryPages(allocator, &page_memory_requirements,
                       &allocation_info, block_count,
                       page_pool->vma_allocations.data, nullptr);
```

VMA（Vulkan Memory Allocator）的 `vmaAllocateMemoryPages` 是 *批量* 分配 N 个固定大小 page 的 helper。

启动期一次性分配 → 运行期只做 *哪几页绑给哪个资源* 的映射。这种模式叫 *page pool* 或 *virtual texture pool*，是 sparse resources 的标准用法。

## 4.7 vkQueueBindSparse

绑定 page 到 image 的 API：

```cpp
VkSparseImageMemoryBind sparse_bind = {
    .subresource = { aspect, mipLevel, arrayLayer },
    .offset      = { dest_x, dest_y, 0 },
    .extent      = { block_width, block_height, 1 },
    .memory      = allocation_info.deviceMemory,
    .memoryOffset = allocation_info.offset,
};

VkSparseImageMemoryBindInfo info = {
    .image     = texture->vk_image,
    .bindCount = N,
    .pBinds    = sparse_binds_array,
};

VkBindSparseInfo sparse_info = {...};
sparse_info.imageBindCount      = N;
sparse_info.pImageBinds          = bind_infos;
sparse_info.signalSemaphoreCount = 1;
sparse_info.pSignalSemaphores    = &bind_semaphore;

vkQueueBindSparse(queue, 1, &sparse_info, VK_NULL_HANDLE);
```

### 注意点 1：必须 sparse-capable queue

`vkQueueBindSparse` 只能在带 `VK_QUEUE_SPARSE_BINDING_BIT` 的 queue 上调用。这通常是 graphics queue 或独立 transfer queue。

### 注意点 2：异步操作

`vkQueueBindSparse` 提交后立即返回，但绑定可能 *没完成*。必须用 semaphore 通知后续提交"绑定已就绪"。

```cpp
// Bind 完发 semaphore：
vkQueueBindSparse(queue, 1, &sparse_info, VK_NULL_HANDLE);  // signal bind_semaphore

// 主渲染等 semaphore：
VkSubmitInfo submit_info = {...};
submit_info.pWaitSemaphores = &bind_semaphore;
vkQueueSubmit(queue, 1, &submit_info, ...);
```

如果 main rendering submission 不等这个 semaphore → 用未绑定 memory → 未定义行为。

### 跟 timeline 的协同

Bind semaphore 也走 [`multi-threading/08`](../multi-threading/08-Timeline与资源退休模型.md) 的 timeline 模型。每帧的 bind sequence 走同一条 timeline，按 `RetirePoint` 跟踪生命周期。

## 4.8 Per-Light Resolution 决策（场景驱动）

仅"按需绑定"还不够，还需要决定 *每个光源用多少分辨率*。这是 [05 LX 当前状态对照](05-LX当前状态对照.md) §5.4 的 importance metric。

### Cluster-based importance

每个光源对每个 cluster 算 *projected solid angle*：

```cpp
f32 d = distance(sphere_screen, tile_center);
f32 diff = d * d - tile_radius_sq;
f32 solid_angle = (2.0f * pi) * (1.0f - sqrt(diff) / d);
f32 resolution = sqrt((4.0f * pi * tile_pixels) / (6 * solid_angle));
```

含义：

- `solid_angle` = 从 light 中心看出去，cluster 占据的立体角
- `tile_pixels` = cluster 在屏幕上的像素数
- `resolution` = "shadow map 一面要多少 pixel 才能 *texel 密度匹配屏幕 pixel 密度*"

跨 cluster 取 max 作为该光源最终分辨率：

```cpp
for each light:
    max_resolution = 0
    for each cluster:
        if (light intersects cluster):
            res = compute_resolution(light, cluster)
            max_resolution = max(max_resolution, res)
    light.shadow_resolution = max_resolution
```

### 内存账（动态）

```
256 lights × 平均 256² × 6 face × 4 字节 ≈ 100 MB
```

vs 朴素 *固定 1024²*：6.4 GB。**省 64×**。

实际场景中，远处光源会 cap 在最低 (64² 或更小)，进一步降低占用。

## 4.9 整套流程

```
启动期一次:
  分配 page pool（N 个固定大小 page）

每帧:
  Step 1: 算 per-light importance
    for each light: compute target resolution
  
  Step 2: bind 资源
    for each light:
      free old pages
      bind new pages cover light.shadow_resolution
    vkQueueBindSparse (signal bind_semaphore)
  
  Step 3: 渲染（等 bind_semaphore）
    [03 mesh shader shadow 流水线]
  
  Step 4: lighting pass 采样 cubemap shadow
```

CPU 只做 *importance 计算 + bind 命令录制*，bind 本身是 GPU 异步操作。

## 4.10 跨主题的应用

Sparse resources 不只 shadow 用：

| 应用 | 需求 |
|------|------|
| **Shadow Maps**（本章） | per-light dynamic resolution |
| **Streaming Texture** | 大世界的 detail texture，按相机距离 stream in/out |
| **Virtual Geometry**（UE5 Nanite） | 极其细密的 mesh，按 LOD 加载 |
| **Virtual UV** | 大场景的程序化 UV，按需生成 |
| **Sparse Buffer**（GPU readback） | 大 buffer 部分可读 |

LX 引入 sparse 后，这些场景都可以叠加。所以 [shadows roadmap §06](06-演进路径.md) 把 sparse residency 单独列为 REQ-D，跟 shadow 解耦。

## 4.11 跟其它调研的耦合

| 调研 | 耦合点 |
|------|--------|
| **multi-threading/08** | bind_semaphore 走 timeline，bind 完成节点是 RetirePoint |
| **frame-graph** | sparse-bound 资源在 frame graph 里需要 *跨帧 + 跨 pass* 表达 |
| **async-compute** | sparse bind 可以走独立 sparse queue（如果硬件支持），跟 graphics 并行 |
| **multi-threading/09 frame-local 资源集** | sparse 是另一类 frame-local 资源，需要纳入 retire 模型 |

## 4.12 LX 当前状态

**完全空白**：

- ❌ 资源创建时未声明 sparse flag
- ❌ 没有 page pool 概念
- ❌ 没碰过 `vkQueueBindSparse`
- ❌ 没有 importance metric
- ❌ VMA `vmaAllocateMemoryPages` 未使用

是 LX 现有 IGpuResource 抽象的 *扩展点*，需要：

- `IGpuResource` 加 sparse-capable 标记
- backend 提供 page pool 管理
- 资源生命周期模型扩展（sparse 资源的 retire 跟普通资源不同）

## 4.13 接下来读什么

- [05 LX 当前状态对照](05-LX当前状态对照.md) — gap 分析（综合 shadow + sparse）
