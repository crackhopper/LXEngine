# 01 · VRS 基础

> 阅读前提：理解 fragment shader 是 per-pixel 跑一次。

## 1.1 VRS 的核心想法

正常渲染：每像素跑一次 fragment shader → 着色 → 写 attachment。

**Variable Rate Shading**：让多像素 *共用一次 fragment shader 调用*：

| Shading rate | 含义 | 一次 shader invocation 着色多少像素 |
|--------------|------|------------------------------------|
| 1×1 | 默认（无 VRS） | 1 |
| 1×2 / 2×1 | 单方向减半 | 2 |
| 2×2 | 双方向减半 | 4 |
| 4×4（部分硬件） | 四倍减半 | 16（artifact 多） |

```
2×2 rate 示意：

┌──┬──┬──┬──┐
│ S│ -│ S│ -│       S = 着色一次
├──┼──┼──┼──┤       - = 复制 S 的结果
│ -│ -│ -│ -│
├──┼──┼──┼──┤
│ S│ -│ S│ -│
├──┼──┼──┼──┤
│ -│ -│ -│ -│
└──┴──┴──┴──┘
```

**省 75% fragment 工作**（每 4 像素只跑 1 次 shader）。

## 1.2 来源：Foveated Rendering

VRS 起源于 VR：

- VR 一帧要渲染左右眼两个画面
- VR 对帧延迟极敏感（晕动症）
- 需要更高帧率

观察：人眼 *只在视野中心区域看清细节*，周边视觉对锐度不敏感。所以 *中心 1×1 / 周边 2×2 / 极边 4×4* 总体画质几乎无损但渲染量减半。这叫 **foveated rendering**。

桌面端没有"视野中心"，但 *画面 uniform 区域*（天空、平整墙面）也不需要全速率着色。VRS 把 foveated 推广到通用场景。

## 1.3 跟其它相邻技术区分

| 技术 | 控制什么 | 性能模式 |
|------|---------|---------|
| **VRS** | fragment shader **执行频率** | 减少 shading 次数 |
| **MSAA** | 单像素多次采样 | 增加采样数 |
| **Resolution scaling** | 整体分辨率 | 全画面降清 |
| **Checkerboard rendering** | 隔像素渲染 | 类似 VRS 但更粗 |
| **DLSS / FSR** | AI 上采样 | 低分辨率 + 重建 |

VRS 跟 MSAA *正交*（虽然要小心 corner case）。

## 1.4 三种 Vulkan 集成方式

`VK_KHR_fragment_shading_rate` 提供三种粒度：

### Per-Draw（最粗）

```cpp
// pipeline 创建时
VkPipelineFragmentShadingRateStateCreateInfoKHR rate_info = {...};
rate_info.fragmentSize = { 2, 2 };

// 或 runtime
vkCmdSetFragmentShadingRateKHR(cmd, &rate, ops);
```

适合：远处天空盒、UI overlay、低重要性背景。开销极低（不需要额外资源）。

### Per-Primitive（中粒度）

vertex / mesh shader 写：

```glsl
gl_PrimitiveShadingRateKHR = SHADING_RATE_2x2;
```

适合：跟 LOD 选择关联、跟 mesh shader culling 协同决策。

### Image Attachment（最细，本章选这个）

每个 *tile*（典型 8×8 或 16×16）一个 rate，存到一张 R8_UINT 图像里：

```cpp
VkRenderingFragmentShadingRateAttachmentInfoKHR shading_rate_info = {
    .imageView = vrs_texture->vk_image_view,
    .imageLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
    .shadingRateAttachmentTexelSize = { 1, 1 },
};
rendering_info.pNext = &shading_rate_info;
```

适合：基于 *画面内容* 的精细控制。代价：每帧要先算一张 rate mask。

### 三种方式可叠加

`VkFragmentShadingRateCombinerOpKHR` 控制 combine（min / max / mul / replace）。实际工程通常只用一种。

## 1.5 用 Sobel 边缘检测决定 Rate

> *人眼在 uniform 区域不敏感，在边缘 / 高对比区域敏感*

判据：**亮度梯度（luminance derivative）**。梯度大 = 边缘 = 1×1；梯度小 = 平滑 = 2×2。

### Sobel Filter

经典图像处理算子，3×3 卷积核：

```
水平梯度 dx:        垂直梯度 dy:
| -1  0  1 |        | -1 -2 -1 |
| -2  0  2 |        |  0  0  0 |
| -1  0  1 |        |  1  2  1 |
```

每个 fragment 用 *自己 + 8 个邻居* 的亮度算 dx 和 dy：

```
G = sqrt(dx² + dy²)  ≈ dx² + dy²（省 sqrt）
```

```glsl
uint rate = SHADING_RATE_2x2;
if (G > 0.1) rate = SHADING_RATE_1x1;
```

## 1.6 Shared Memory 优化

朴素实现：每个 thread 算时读 9 个邻居 → 每 thread 9 次 texture fetch → 32 thread × 9 = 288 fetches。

优化：thread group 协作：

```glsl
shared float local_image_data[18][18];   // 16×16 thread + 2 圈 padding

local_image_data[ly][lx] = luminance(...);  // 每 thread 读 1 次
barrier();   // 等所有 thread 写完

// 然后用 shared memory 算 Sobel
float dx = local_image_data[ly-1][lx-1] - ...;
```

**减 15× 内存带宽**。`barrier()` 是 thread group 内同步必需。

边界 thread 还要 fetch 1-2 个 padding 像素填外圈。

## 1.7 Rate 的 Vulkan 位编码

```
texel value bits:
  bit 0-1: log2(size_h)   // height 减半次数
  bit 2-3: log2(size_w)   // width 减半次数

实际 rate:
  size_w = 2^((texel/4) & 3)
  size_h = 2^(texel & 3)
```

例：

| `texel` 值 | size_w | size_h | rate |
|-----------|--------|--------|------|
| 0 | 1 | 1 | 1×1 |
| 1 | 1 | 2 | 1×2 |
| 4 | 2 | 1 | 2×1 |
| 5 | 2 | 2 | 2×2 |

代码：

```glsl
uint rate = (1 << 2) | 1;   // = 5 = 2×2
```

## 1.8 跟 dynamic rendering 的集成

跟 Ch7 G-buffer 一样用 `VK_KHR_dynamic_rendering`。VRS image 作为额外 attachment 接到 `VkRenderingInfoKHR.pNext`。

**Shader 端零修改**：fragment shader 完全不动，rate 由硬件层面消费。这是 image attachment 方式的最大优势。

## 1.9 接下来读什么

- [02 Specialization Constants](02-specialization-constants.md) — SPIR-V 编译期常量机制
