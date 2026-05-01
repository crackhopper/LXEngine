# 04 · TAA (Temporal Anti-Aliasing)

> 阅读前提：[01](01-temporal-范式.md) 讲清 temporal 共享基础。本文展开 *TAA 算法 + 5 大改进 + blurriness 缓解*。

## 4.1 Anti-Aliasing 历史

| 时期 | 技术 | 原理 |
|------|------|------|
| 1990s-2000s | **MSAA** (Multi-Sample) | 单像素多次采样几何边缘 |
| 2009 | **MLAA** (Morphological) | CPU SSE，post-process 边缘检测 |
| 2010 | God of War III SPU MLAA | 索尼 PS3 Cell 实现 |
| 2011 | **SMAA** | GPU 化 + 加 temporal 成分 |
| 2014+ | **TAA** | 时间维度采样 |
| 2018+ | DLSS / FSR | AI / 频域上采样 |

## 4.2 为什么 TAA 取代 MSAA

**Deferred rendering 跟 MSAA 不兼容**：

- MSAA 需要 *每个 sample 独立运行 fragment shader*
- Deferred 的 G-buffer 是 *per-pixel 单值*，MSAA 在 G-buffer 上几乎无意义（要保持 4× 内存）

**TAA 的优势**：

- 跟 deferred 完全兼容
- 内存代价仅 1 个 history texture
- 跟其它 temporal 技术（volumetric fog / SSR）共享 infrastructure
- 画质好（多帧累积比单帧多采样更平滑）

**TAA 的代价**：blurriness、ghosting、handling transparent objects 复杂。

## 4.3 TAA 6 步算法

```
Step 1: Velocity coords
        从当前 pixel 周围 3×3 找 closest depth 的 pixel
Step 2: Velocity texture
        在 closest pixel 位置读 motion vector（linear sampler）
Step 3: History texture
        用 motion vector reproject 到上一帧位置，sample
Step 4: Scene color + neighborhood
        采当前 pixel + 周围 3×3 cache 信息
Step 5: History constraint
        用 neighborhood 限制 history 的范围（防 ghosting）
Step 6: Resolve
        混合 current + constrained history
        
Output → 下一帧的 history texture（双 buffer 切换，无需 copy）
```

## 4.4 最简实现：理解骨架

```glsl
vec3 taa_simplest(ivec2 pos) {
    const vec2 velocity = sample_motion_vector(pos);
    const vec2 screen_uv = uv_nearest(pos, resolution);
    const vec2 reprojected_uv = screen_uv - velocity;
    
    vec3 current_color = sample_color(screen_uv).rgb;
    vec3 history_color = sample_history_color(reprojected_uv).rgb;
    
    return mix(current_color, history_color, 0.9);  // 0.9 history + 0.1 current
}
```

**4 行** = TAA 骨架。但效果会 ghosting + blurry。后续 5 个改进点都在解决这两个问题。

## 4.5 改进 1：Reprojection（Closest Depth）

**问题**：直接用当前 pixel 的 velocity 会有边缘 artifact（边缘 pixel 的 velocity 可能错误）。

**改进**：在 3×3 邻域里找 *closest depth* 的 pixel，用它的 velocity：

```glsl
void find_closest_fragment_3x3(ivec2 pixel, out ivec2 closest_pos, out float closest_depth) {
    closest_depth = 1.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float depth = texelFetch(depth, pixel + ivec2(x, y), 0).r;
            if (depth < closest_depth) {
                closest_depth = depth;
                closest_pos = pixel + ivec2(x, y);
            }
        }
}
```

**直觉**：边缘 pixel 处于"前景与背景之间"。最近的 depth 就是 *前景*，前景的 velocity 通常更准。

## 4.6 改进 2：History Sampling（Catmull-Rom）

**问题**：直接 sample history texture 会有插值 blurriness。

**改进**：用 **Catmull-Rom filter**（5-tap bicubic）替代 bilinear。

```glsl
case HistorySamplingFilterCatmullRom:
    history_color = sample_texture_catmull_rom(reprojected_uv, history_idx);
```

Catmull-Rom 在 *锐度* 和 *平滑* 之间取 sweet spot。比 bilinear 锐、比 bicubic 稳定。

## 4.7 改进 3：Scene Sampling（3×3 Neighborhood + Moments）

```glsl
for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y) {
        vec3 c = sample_current_color(pos + ivec2(x, y));
        
        // Min / max 给"邻域颜色范围"
        neighborhood_min = min(neighborhood_min, c);
        neighborhood_max = max(neighborhood_max, c);
        
        // m1 / m2 是 first / second moments，给统计信息
        m1 += c;
        m2 += c × c;
    }
```

`m1 / 9` = mean，`sqrt(m2/9 - mean²)` = standard deviation。给后续 constraint 提供统计数据。

## 4.8 改进 4：History Constraint（核心防 ghosting）

**问题**：reproject 到上一帧后采到的 history color 可能 *跟当前完全不相干*（disocclusion / 高速运动）。直接 blend 会有强烈 ghosting。

### 4 种约束方式

| 方法 | 原理 | 强度 |
|------|------|------|
| **RGB clamp** | history 强行 clamp 到 [neighborhood_min, max] | 简单但激进 |
| **RGB clip** | 沿 (current → history) 线段裁剪到 AABB | 较稳 |
| **Variance clip** | 用 mean ± k×stddev 算 AABB | 自适应 |
| **Variance clip + clamp** | 上面两者结合 | **最优** |

### Variance Clip 公式

```glsl
float rcp = 1.0 / 9.0;
vec3 mu = m1 × rcp;                          // mean
vec3 sigma = sqrt(abs(m2 × rcp - mu × mu));   // stddev

vec3 minc = mu - gamma × sigma;               // gamma=1
vec3 maxc = mu + gamma × sigma;

vec3 clamped = clamp(history, neighborhood_min, neighborhood_max);
history = clip_aabb(minc, maxc, clamped);
```

**几何含义**：

- 把邻域颜色看作 RGB 空间的 *点云*
- 算它的 "轴对齐边界盒"（用 stddev 控制大小）
- history 必须落在这个盒子里，否则裁剪到边界

`gamma = 1` 是 1σ。增大让约束更宽松（保留更多 history，但也保留更多 ghosting）。

## 4.9 改进 5：Resolve（动态 weight）

**问题**：固定 `0.9 history + 0.1 current` 在不同区域不合适。运动剧烈区应该多用 current，平静区应该多用 history。

### Temporal Filter

```glsl
vec3 temporal_weight = clamp(abs(neighborhood_max - neighborhood_min) / current_sample, 0, 1);
history_weight = mix(0.25, 0.85, temporal_weight);
```

邻域颜色变化大 → 边缘 / 运动 → 多用 history（避免 flicker）；稳定区 → 平衡。

### Inverse Luminance Filter

```glsl
current_weight ×= 1 / (1 + luminance_source);
history_weight ×= 1 / (1 + luminance_history);
```

亮 pixel 权重低、暗 pixel 权重高。**抑制 firefly**（极亮 pixel 在某帧异常出现）。

### Luminance Difference Filter

```glsl
float diff = abs(lum_source - lum_history) / max(lum_source, lum_history, 0.2);
float k = (1 - diff)²;
history_weight = 1 - k;
current_weight = k;
```

亮度差大 → 真实变化 → 用 current；亮度差小 → 噪声 → 用 history。

三个 filter 组合应用。

## 4.10 解决 TAA 的 Blurriness

TAA 把多帧平均，自然变糊。三种缓解：

### Sharpen Post-Process

```glsl
float input_lum = luminance(color);
float avg_lum = sum_luminance(3x3_neighborhood) / 9;
float sharpened_lum = input_lum - avg_lum;
float final_lum = input_lum + sharpened_lum × sharpening_amount;
color ×= (final_lum / input_lum);
```

*High-pass filter*：用 (current - blurred) 增强边缘对比。

### Negative MIP Bias

```cpp
VkSamplerCreateInfo sampler;
sampler.mipLodBias = -0.25f;
```

让 texture sampling *偏向更细* 的 mip level。**全局 sharper**，但有 aliasing 风险。

### Unjitter Texture UVs

```glsl
vec2 unjitter_uv(vec2 uv, vec2 jitter) {
    return uv - dFdxFine(uv) × jitter.x + dFdyFine(uv) × jitter.y;
}
```

texture sampling 时 *消除 camera jitter 的影响*，得到稳定 UV。

## 4.11 跟 Volumetric Fog 的协同

### 共享 temporal 基础设施

| 机制 | TAA 用 | Volumetric Fog 用 |
|------|--------|-------------------|
| Previous-frame view-projection | ✅ | ✅ |
| Motion vectors | ✅ | ✅ |
| Reprojection | ✅ | ✅ |
| Blue noise jitter | ✅ | ✅ |
| History texture（双 buffer） | ✅ | ✅ |
| History 失效检查 | ✅ | ✅ |

工业引擎里这些是 *单一 temporal 子系统*。

### Animated Dithering 配合

Volumetric fog 引入激进 dithering（blue noise 抖动），TAA 跨帧累积平掉噪声。**Dithering 强度可以更高，最终画质更好**。

## 4.12 TAA 的痛点 / 没解决的问题

| 问题 | 现象 | 缓解 |
|------|------|------|
| **Disocclusion** | 新露出区域 history 错 | motion vector 突变检测 |
| **半透明物体** | velocity 不准 | 单独 pass / 跳过 TAA |
| **Specular highlight** | 高光跨帧位置变 | 单独处理 specular |
| **快速旋转 / 镜头切换** | 整片 history 失效 | TAA 重置 |
| **细薄结构** | 1-pixel 线条 jitter 闪烁 | bicubic + variance clip |
| **HDR 极亮 pixel** | firefly 跨帧累积放大 | tonemap-based weighting |

工业 TAA 都是几千行代码处理这些 corner case。本章版本是教学骨架。

## 4.13 LX 现状

LX 完全空白：

- ❌ 没 TAA shader
- ❌ 没 motion vector 路径
- ❌ 没 history texture
- ❌ 没 sharpen post-process

但 [01 temporal 范式](01-temporal-范式.md) 已经把 *基础设施* 的需求统一抽出，REQ-A 一次实现两个特性都用。

## 4.14 接下来读什么

- [05 LX 当前状态对照](05-LX当前状态对照.md) — 综合 gap 分析
