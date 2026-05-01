# IGpuResource：core 层的 GPU 资源统一契约

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/rhi/gpu_resource.hpp](../../../../../src/core/rhi/gpu_resource.hpp)
出发，解释为什么引擎需要一个极薄的 `IGpuResource` 接口，
以及为什么 `CameraData`、`SkeletonData`、`ParameterBuffer`、
`CombinedTextureSampler`、`VertexBuffer` / `IndexBuffer`
这些看起来语义不同的类型会在这里汇合。

可以先带着一个问题阅读：为什么项目没有把“CPU 侧对象”和
“backend 侧 upload/bind 输入”拆成更多层？答案是，这里故意只保留
backend 真正需要的最小公共契约，避免每条资源路径都发明一套专用接口。

源码入口：[gpu_resource.hpp](../../../../src/core/rhi/gpu_resource.hpp)

## IGpuResource：core 层定义的“可被 GPU 消费”的统一契约

这个接口不是 Vulkan buffer / image 的后端对象，而是 core 层给 backend 的统一入口：
只要某个对象能提供“资源类型 + 原始字节 + 字节大小 + 可选 binding 名”，
backend 就可以沿同一条同步和绑定路径处理它。

这也是为什么项目里很多业务类型会直接实现它：

- `VertexBuffer` / `IndexBuffer`：把几何数据暴露给 upload 路径
- `CameraData` / `SkeletonData` / `ParameterBuffer`：把 CPU 侧 buffer 字节暴露给 descriptor 路径
- `CombinedTextureSampler`：把纹理像素和 shader binding 名暴露给采样器绑定路径

接口刻意保持得很薄，只表达 backend 真正需要的最小信息：

- `getType()`：决定后端要创建哪种 GPU 对象
- `getRawData()` + `getByteSize()`：提供 upload 源数据
- `getBindingName()`：让 descriptor 绑定按 shader 名字对齐
- dirty 标记：把“CPU 数据刚改过”显式传给资源同步阶段

因此它更像“GPU 资源适配接口”，而不是“渲染对象基类”。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 设计拆解

从职责上看，`IGpuResource` 只回答 backend 真正关心的四个问题：

| 问题 | 接口成员 | 后端如何使用 |
|------|----------|--------------|
| 这是什么资源 | `getType()` | 决定创建 `VulkanBuffer` 还是 `VulkanTexture`，以及走哪个上传分支 |
| 要上传哪些字节 | `getRawData()` | 取 CPU 侧源地址 |
| 一共多大 | `getByteSize()` | 分配 staging / copy 大小 |
| 它对应 shader 的哪个 binding | `getBindingName()` | descriptor 绑定按名字路由 |

dirty 标记没有进入纯虚接口，而是作为基类上的通用状态，原因也很直接：
它不是某个资源子类的特殊能力，而是所有 GPU 资源都共享的同步协议。

## 具体有哪些实现

当前代码里比较核心的 `IGpuResource` 实现有：

| 类型 | 资源形态 | 主要来源 |
|------|----------|----------|
| `VertexBuffer` / `IVertexBuffer` | 顶点缓冲 | mesh 几何数据 |
| `IndexBuffer` | 索引缓冲 | mesh 几何数据 |
| `CameraData` | UniformBuffer | 相机 view/proj/eye 参数 |
| `DirectionalLightData` | UniformBuffer | 光照参数 |
| `SkeletonData` | UniformBuffer | 骨骼矩阵调色板 |
| `ParameterBuffer` | UniformBuffer / StorageBuffer | 材质 buffer binding 字节 |
| `CombinedTextureSampler` | CombinedImageSampler | 纹理 + sampler 绑定入口 |

这里最值得注意的是：这些类型并没有统一追加 `Resource` 后缀。
项目现在采用的是“接口名表达抽象层，具体类型保留业务名”的做法。
这样阅读调用点时，看到的是 `CameraData`、`SkeletonData`、`IndexBuffer` 这些业务概念，
而不是一串为了迎合继承体系而膨胀出来的类名。

## 顺着调用链看

在运行时，典型路径是：

1. scene / material / camera 等 CPU 对象更新自己的数据
2. 对应的 `IGpuResource` 调用 `setDirty()`
3. backend 的 resource manager 读取 `getType()` / `getRawData()` / `getByteSize()`
4. 若资源参与 descriptor 绑定，再读取 `getBindingName()`

这条路径说明 `IGpuResource` 的真正价值不是“抽象得很漂亮”，
而是把多种资源压进了一条统一、可缓存、可增量同步的 backend 通道里。

## 配套阅读：资源上传

如果想继续追“这些 CPU 侧对象到底在什么时候变成 Vulkan 资源”，
建议接着看 [资源上传](../../../../subsystems/resource-upload.md)。

那一页回答的是这几个更偏运行时的问题：

- `setDirty()` 之后什么时候真的发生上传
- `initScene()` 和 `uploadData()` 的分工是什么
- `VulkanRenderer` 和 `VulkanResourceManager` 分别负责什么
- buffer 路径和 texture 路径为什么不是同一种上传策略

## REQ-034 会扩展什么

[`REQ-034`](../../../../requirements/034-render-target-desc-and-target.md) R7 把
RenderTarget 的所有 attachment 句柄统一到 `IGpuResourceSharedPtr` 上，与本页
描述的 "GPU 资源走 IGpuResource，无例外" 这条契约保持一致。具体扩展点：

- **新增隐式角色：image-as-attachment**。当前 `IGpuResource` 的实现集中于
  buffer 类（`VertexBuffer` / `IndexBuffer` / `ParameterBuffer` / `CameraData` /
  `SkeletonData`）和 texture 类（`CombinedTextureSampler`）。REQ-034 让
  RenderTarget 持有的 color / depth-stencil attachment 也走 `IGpuResource`，
  意味着 backend 内部需要新增一个 image-as-attachment 适配器（或扩展
  `CombinedTextureSampler` 的角色，让它能在不带 sampler 的情况下作为 attachment
  暴露 `IGpuResource`）。
- **接口本身不变**。本页的"资源类型 + 原始字节 + 字节大小 + 可选 binding 名"
  四元契约对 attachment 仍然成立 — attachment 也是 GPU image，只是用途从
  shader read 切换到 framebuffer write。是否需要在 `ResourceType` 枚举里加
  `Attachment` 是个内部决策，不影响契约形状。
- **swapchain image 的来源仍由 backend 控制**。REQ-034 R8 明确 v1 不实现
  swapchain resize；swapchain image 通过 `IGpuResource` 适配器暴露给 core，
  core 不直接接触 `VkImage` / `VkImageView`。这条约束跟本页"backend 真正需要
  的最小公共契约"一致。

也就是说，REQ-034 不要求重新设计 `IGpuResource`，只是把它的覆盖面从 buffer +
texture 扩展到 attachment image — 是接口契约的一次自然延伸，不是分裂。
