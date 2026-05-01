# 06 · RT Reflection 与 SVGF

> 阅读前提：[03 RT 管线与 Ray Query](03-RT管线与ray-query.md) + [05 RT DDGI](05-RT-DDGI.md) 讲清 RT pipeline 和 hit shader。本文展开 *Ch15 的 RT 反射*：1 ray/pixel + GGX VNDF + importance sampling lights + **SVGF 3-pass denoiser**。
>
> SVGF 不只反射用——它是 LX 处理 *所有 RT 噪声* 的通用 denoiser 框架。

## 6.1 SSR vs RT Reflection

| 维度 | SSR (Screen-Space Reflection) | RT Reflection |
|------|-------------------------------|---------------|
| 数据源 | depth buffer + scene color | TLAS + 重新算 lighting |
| 屏幕外反射 | ❌ fallback env map | ✅ 完整支持 |
| 步进方式 | screen-space ray march N 步 | hardware ray-AS 求交 |
| 性能 | 固定（步数 × 像素数） | 动态（取决于 BVH 命中） |
| 一致性 | 相机一动反射会闪 | 稳定 |
| 硬件门槛 | 通用 | RT GPU only |
| 噪声来源 | 步进精度 | 单 ray 采样 |

**工业做法：双路径**——大粗糙度 / 屏幕内 → SSR；屏幕外 → RT fallback。本章只讲纯 RT 路径。

## 6.2 RT Reflection 5 步算法

```
1. Roughness 门槛       → roughness > 0.3 直接跳过（粗糙表面无反射）
2. Pick reflection dir  → mirror 反射 OR GGX VNDF 采样（物理正确）
3. Cast 1 ray           → traceRayEXT 到 hit
4. Hit 处算 lighting    → importance sampling 选 light + shadow ray
5. SVGF denoise         → 3 pass 时空滤波（噪声 → 可用）
```

**1 ray/pixel 是必须的**——半分辨率 + 1 ray 才能 real-time。结果 100% 是噪声，*denoiser 不是 polish 而是核心组成部分*。

## 6.3 反射方向：Mirror vs GGX VNDF

### Mirror（roughness = 0）

```glsl
vec3 dir = reflect(view, normal);
```

完美镜面，但只对 *绝对光滑表面* 物理正确。任何粗糙度都需要扰动。

### GGX VNDF（Visible Normals Distribution Function）

```glsl
vec3 micro_normal = sampleGGXVNDF(view, roughness, U1, U2);
vec3 dir = reflect(view, micro_normal);
```

**VNDF 思想**：

- 真实表面的法线 *不是 macro normal*，而是 *微表面法线分布*
- 不同 roughness 下分布形状不同（roughness 大 → 法线分布广 → 反射方向乱）
- VNDF 只采 *从 view direction 看得见的* 微法线（剔除被遮挡的微面）→ 比 NDF 直采样高效

VNDF 是物理 BRDF 反演——保证 importance sampling 的概率密度跟 BRDF 一致，最终图像 *无偏*。

来自 Heitz 2018 paper。

### 随机数：PCG + Position Seed

```glsl
uint seed(uvec2 p) {
    return 19u * p.x + 47u * p.y + 101u;
}
uint rand_pcg() {
    uint state = rng_state;
    rng_state = rng_state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// 初始化
rng_state = seed(gl_LaunchIDEXT.xy) + current_frame;
float U1 = rand_pcg() * rnd_normalizer;  // [0, 1)
float U2 = rand_pcg() * rnd_normalizer;
```

每 pixel 独立 RNG，每帧 frame 偏移 → 跨帧采到不同 GGX 微法线、不同 importance light。**SVGF 跨帧累积 16+ 帧才收敛**。

来自 *Hash Functions for GPU Rendering* 论文。

## 6.4 Hit Shader 的 Importance Sampling Lights

每 fragment 只能投 1 个 shadow ray → 必须挑 *最重要的光源*：

```glsl
// 算每 light 的 importance
for (uint l = 0; l < active_lights; ++l) {
    Light light = lights[l];
    vec3 to_light = light.world_position - p_world;
    float distance_sq = dot(to_light, to_light);
    float r_sq = light.radius * light.radius;
    
    // 角度
    float angle_cos = dot(normalize(to_light), triangle_normal);
    float theta_i = acos(angle_cos);
    
    // 光源朝向（area light 边缘）
    float theta_u = asin(light.radius / sqrt(distance_sq));
    float theta_prime = max(0, theta_i - theta_u);
    float orientation = abs(cos(theta_prime));
    
    // 是否激活
    bool active = (angle_cos > 1e-4) && (distance_sq <= r_sq);
    
    // importance = (intensity × orientation) / distance²
    float importance = active
        ? (light.intensity * orientation) / distance_sq
        : 0.0;
    
    lights_importance[l] = importance;
    total_importance += importance;
}

// 归一化
for (uint l = 0; l < active_lights; ++l)
    lights_importance[l] /= total_importance;

// CDF 选 light
float r = rand_pcg() * rnd_normalizer;
float accum = 0;
for (light_index = 0; light_index < active_lights; ++light_index) {
    accum += lights_importance[light_index];
    if (accum > r) break;
}
```

**问题**：256 光源 *每 fragment* 算 256 个 importance → 性能崩。**必须跟 cluster lighting 绑定**——只在 cluster 命中的光源里选。书没强调，但工业必走。

来自 *Ray Tracing Gems · Importance Sampling of Many Lights on the GPU*。

## 6.5 SVGF Denoiser（3 Pass）

**Spatiotemporal Variance-Guided Filtering** — 业界 ground truth-quality denoiser，*在 1 ray/pixel 噪声上能逼近 1024 spp 离线渲染*。

### Pass 1: Temporal Accumulation

```
输入：reflections_color (current frame, very noisy)
      motion_vectors_2d
      mesh_id (current + history)
      depth + depth_normal_dd (current + history)
      normal (current + history)
      history_reflections + history_moments

输出：integrated_color, integrated_moments
```

#### Consistency Check（4 道关）

```glsl
bool check_temporal_consistency(uvec2 p) {
    vec2 prev_p = p + 0.5 + motion_vector;
    
    // 1. 在屏幕内
    if (any prev_p out of bounds) return false;
    
    // 2. mesh_id 一致 — 防 disocclusion
    if (mesh_id_curr != mesh_id_prev) return false;
    
    // 3. depth 差异 — 用屏幕空间 depth 导数归一化
    float depth_diff = abs(z - prev_z) / (depth_dd + 1e-2);
    if (depth_diff > 10) return false;
    
    // 4. normal 差异
    float normal_diff = distance(normal, prev_normal) / (normal_dd + 1e-2);
    if (normal_diff > 16) return false;
    
    return true;
}
```

**两个新 G-buffer 通道**：

- `mesh_id`（u32 per-pixel，每 mesh 全局 ID）
- `depth_normal_dd`（屏幕空间 depth + normal 的 ddx/ddy 长度）

这两个通道 *现有 deferred 没有*，是 SVGF 的硬要求。

#### Accumulation

```glsl
if (consistent) {
    integrated_color = α * curr + (1-α) * history;        // α = 0.2
    integrated_moments = α * (lum, lum²) + (1-α) * history_moments;
} else {
    integrated_color = curr;
    integrated_moments = (lum, lum²);
}
```

`α = 0.2` 比 DDGI hysteresis 快得多——反射的高频细节不能等 33 帧。

### Pass 2: Variance Estimate

```glsl
float variance = moments.y - moments.x * moments.x;  // E(X²) - E(X)²
```

存 1 张 R32F texture，给 Pass 3 wavelet filter 当 *adaptive 强度参数*。

### Pass 3: 5× A-trous Wavelet Filter（5×5 Cross-Bilateral）

```glsl
for (5 iterations) {
    for each pixel p:
        new_color = 0; color_weight = 0;
        new_variance = 0; variance_weight = 0;
        
        for (5×5 neighbors q):
            // 三重权重
            float w_n = pow(max(0, dot(n_p, n_q)), σ_n);
            float w_z = exp(-|z_p - z_q| / (σ_z * z_dd + eps));
            float w_l = exp(-|l_p - l_q| / (σ_l * sqrt(gauss(var)) + eps));
            
            float h_q = h[5×5 wavelet kernel][x][y];
            float w = w_n * w_z * w_l * h_q;
            
            new_color    += w * color[q];
            color_weight += w;
            new_variance += w² * variance[q];
            variance_weight += w²;
        
        color[p] = new_color / color_weight;
        variance[p] = new_variance / variance_weight;
}
```

**A-trous（"with holes"）**：5 次迭代每次 *step size 翻倍*（1, 2, 4, 8, 16）→ 等效 *81×81 滤波核* 但只采 25 次 ⇒ O(N) 不是 O(N²)。

**三重权重作用**：

- **Normal weight** `w_n`：法线一致 → 同一表面 → 高权
- **Depth weight** `w_z`：用 *屏幕空间深度导数* 自适应 → 倾斜表面也能正确
- **Luminance weight** `w_l`：用 *variance 引导* → 高方差区域更敏感地保留细节

`σ_n = 128` (典型), `σ_z = 1.0`, `σ_l = 4.0`——书的具体参数可能不同。

### Variance 也要 filter

```glsl
new_variance += w² * variance[q]
```

注意是 `w²` 而不是 `w`——variance 累积要平方权重才数学上正确。

## 6.6 跟现有 Lighting 整合

```glsl
// 在 lighting.h 末端
vec3 F = fresnel_schlick_roughness(NoV, F0, roughness);
vec3 indirect_specular = textureLod(global_textures[reflection_tex],
                                    screen_uv, 0).rgb;
final_color += F * indirect_specular;
```

通常半分辨率，主像素 sample 时做 bilateral upscale。

## 6.7 SVGF 的通用性

SVGF 不只反射用——是 LX 处理 *所有 RT 噪声* 的通用 denoiser：

| 输入噪声 | SVGF 适用 |
|---------|----------|
| RT reflection (Ch15) | ✅ 标准用法 |
| RT AO（未涉及） | ✅ 直接用 |
| RT 透射 / 折射 | ✅ |
| Path-traced GI | ✅ |
| RT shadow advanced 版 | ⚠️ Ch13 用专门的滤波（更轻量），但 SVGF 也行 |
| DDGI | ❌（DDGI 自己有 hysteresis blend） |

**架构意义**：把 SVGF 实现成 *可复用 compute pass*——参数化输入 / 输出 / 权重，而不是写死 reflection only。LX 演进路径里 SVGF 应该作为 *单独 REQ*，多个 RT 特性共享。

## 6.8 性能数字（参考）

RTX 30 / 半分辨率 1080p：

| 阶段 | 时间 |
|------|------|
| GGX VNDF + 1 ray cast | 0.5-1 ms |
| Importance sampling + shadow ray | 0.2-0.5 ms |
| SVGF Pass 1 (accumulation) | 0.1 ms |
| SVGF Pass 2 (variance) | 0.05 ms |
| SVGF Pass 3 (5× wavelet) | 0.5-0.8 ms |
| **Total** | ~1.5-2.5 ms |

跟 SSR 对比：SSR 0.5-1 ms（更便宜），RT 1.5-2.5 ms（贵但物理正确）。

## 6.9 LX 现状

### 共用基础（跟 Ch13/14 共享）

- ❌ RT pipeline + SBT
- ❌ Buffer device address
- ❌ Bindless from RT shader
- ❌ Cluster lighting

### 反射特有

| 维度 | LX | 需要 | gap |
|------|----|----|-----|
| `mesh_id` G-buffer 通道 | 无 | u32 per-pixel，每 mesh 唯一 ID | 中（mesh asset 加 stable ID） |
| Screen-space depth/normal 导数 | 无 | ddx/ddy 提取到 G-buffer | 小 |
| GGX VNDF sampler shader util | 无 | shader lib | 小 |
| PCG random + position seed | 无 | shader util | 小 |
| Light importance sampling | 无 | + cluster 整合 | 中 |
| History reflections texture | 无 | 跟 TAA history 同生命周期 | 小 |
| History moments texture（RG16F） | 无 | per-pixel 1st/2nd moments | 小 |
| Variance texture（R32F） | 无 | 中间 ping-pong | 小 |
| **SVGF 5-pass shader chain** | 无 | compute shader chain | **中-高** |

## 6.10 关键风险

### 1. SVGF 调参陷阱

- `α = 0.2` (history weight)
- `σ_n = 128` (normal sigma)
- `σ_z = 1.0` (depth sigma)
- `σ_l = 4.0` (luminance sigma)
- `mesh_id_diff = 0` 容忍度
- `depth_diff > 10` threshold
- `normal_diff > 16` threshold

每个魔法数字都跟内容相关。需要：

- 调试 UI（可视化 variance / weight / history）
- 对比测试（reference frame）
- profiling 工具

### 2. mesh_id 稳定性

`mesh_id` 必须 *跨帧稳定*——如果 mesh asset 重新加载导致 ID 变化，整张反射会闪。需要：

- 全局 stable mesh ID 系统（不只是 instance ID）
- 跟 LX 现有 asset 系统整合

### 3. Importance Sampling 跟 Cluster 必须一起做

256 光源全采样 = 性能崩。但 cluster lighting 跟 importance sampling 的整合 *书没讲*——需要自己设计：

```
cluster.bin → 候选 lights (5-15)
       ↓
candidate set 内做 importance sampling
       ↓
选 1 个 → cast shadow ray
```

## 6.11 一句话总结

**Ch15 RT Reflection = 1 ray/pixel + GGX VNDF + importance sampling lights + SVGF 3-pass denoiser。SVGF 用 mesh_id + depth_dd + normal + luminance variance 四重权重，在极少样本上重建出干净图像。SVGF 是 LX 处理 *所有 RT 噪声* 的通用 denoiser 框架——不止反射用，将来 RT AO / RT 透射也用。**

## 6.12 接下来读什么

- [07 LX 当前状态对照](07-LX当前状态对照.md) — 综合 gap 分析
