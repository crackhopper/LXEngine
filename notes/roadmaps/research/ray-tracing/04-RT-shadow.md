# 04 · RT Shadow

> 阅读前提：[03 RT 管线与 Ray Query](03-RT管线与ray-query.md) 讲清 ray query 调用范式。本文展开 *Ch13 的两种 RT shadow 实现*：朴素版 + RT Gems 4-pass advanced 版。

## 4.1 为什么 RT 替代 Shadow Map

Shadow map（Ch8 / shadows 调研）的根本限制：

- **Depth buffer 分辨率**：远处 / 大场景 → acne / peter-panning / swimming
- **Cascades 复杂度**：CSM 需要 4-6 层，每层 rerender 整个场景
- **256 光源代价**：每光源 1 张 cube map → ~16 GB（必须 sparse residency）
- **软阴影是假的**：PCF / VSM 只是 *近似*，无法准确重现物理软影
- **Area light 不支持**：shadow map 只懂 point light

RT shadow 解决全部：

- 几何精度（无 depth buffer）
- 256 光源 = TLAS 1 个 + 256 次 ray query
- 软阴影通过 *多 ray + Poisson disk* 物理正确
- Area light native 支持（多 ray 撒在面上）

代价：

- 硬件门槛（RTX/RDNA2 only）
- 每 fragment ray query 数 × 光源数 = 巨大 ray 总量
- 噪声 → 必须时空滤波

## 4.2 朴素版：4 行代码

### 算法

```
对每个 fragment:
    对每个光源 light:
        cast 1 ray from world_pos → light_pos
        terminate on first hit
        visibility = (没命中) ? 1 : 0
    
    direct_lighting += light.contribution × visibility
```

### GLSL 实现

```glsl
#extension GL_EXT_ray_query : enable

float compute_shadow(vec3 world_pos, vec3 light_pos) {
    vec3 light_dir = light_pos - world_pos;
    float dist = length(light_dir);
    light_dir /= dist;
    
    rayQueryEXT q;
    rayQueryInitializeEXT(q, as,
        gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
        0xff,
        world_pos, 0.001,    // 0.001 偏移防 self-intersection
        light_dir, dist);
    
    rayQueryProceedEXT(q);
    
    return (rayQueryGetIntersectionTypeEXT(q, true) ==
            gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;
}
```

### 优缺点

| 优 | 缺 |
|---|---|
| 4 行实现 | 只支持 point light 硬阴影 |
| 完全无 ghost / flicker（per-frame fresh） | Area light 需要多 ray |
| 直接接到现有 lighting shader | Soft shadow 需要 jitter + 时空滤波 |
| 无 history 维护 | 256 光源每 fragment 256 ray → 性能崩 |

朴素版适合 *早期 demo / 单光源场景*；多光源 / soft shadow 必须升级到 advanced 版。

### Self-intersection 偏移

`tmin = 0.001` 是 *魔法数字*：

- 太小 → fragment 自己跟自己相交，全黑
- 太大 → small mesh 跨过整个物体，shadow 错

工业做法：*per-mesh adaptive epsilon*，或沿 normal 推一小段：

```glsl
vec3 origin = world_pos + normal * 0.001;
```

## 4.3 RT Gems Advanced 版：4 Pass

来自 *Ray Tracing Gems · Ray Traced Shadows*（Akenine-Möller 等）。**核心思想：方差驱动的自适应采样数 + 时空滤波**。

### 总体流程

```
Pass 1: Motion vector 特化版
        depth-aware 失效检测，存 vec2 / 哨兵 (-1,-1)

Pass 2: Visibility variance
        最近 4 帧 visibility 的 max-min
        3D dispatch (W, H, num_lights)

Pass 3: Adaptive ray cast + history 更新
        per-fragment per-light:
          1. 5×5 max + 13×13 tent + temporal blend → filtered_variance
          2. delta-driven sample count (1..MAX)
          3. Poisson disk per sample → multi-ray
          4. visibility = mean(rays)
          5. 更新 visibility / variance / sample_count history (4 帧 ring)

Pass 4: Spatio-temporal filter
        temporal: 4 帧 mean
        spatial: 5×5 Gaussian + normal-aware skip (dot < 0.9 → skip)
        输出供 lighting shader 用
```

### 数据结构

3 张 *3D texture*（关键设计）：

```
visibility_cache_3d   : RGBA16, layout (W, H, num_lights), per-channel = 4 帧
variance_cache_3d     : RGBA16, layout (W, H, num_lights), per-channel = 4 帧
sample_count_cache_3d : RGBA8U, layout (W, H, num_lights), per-channel = 4 帧

motion_vectors_2d     : RG16, layout (W, H), 哨兵 (-1,-1) 表 disocclusion
```

**3D 第三轴 = light index** 是关键——每 light 独立的 history。

## 4.4 Pass 1: Motion Vector（特化版）

跟 [temporal-techniques 01](../temporal-techniques/01-temporal-范式.md#14-motion-vector-计算) 的 motion vector *几乎相同但不一样*：

```glsl
// 1. depth ratio（不是绝对差）
float depth_diff = abs(1.0 - prev_ndc.z / curr_ndc.z);

// 2. epsilon 跟 view normal 相关（grazing angle 容错）
float eps = 0.003 + 0.017 * abs(view_normal.z);

// 3. 失效返回哨兵
visibility_motion = (depth_diff < eps)
    ? (curr.xy - prev.xy)
    : vec2(-1);
```

为什么不直接复用 TAA 的 motion vector：

- TAA 的 history 失效顶多 ghost；shadow 的 history 失效会 *阴影闪烁*——更不能容忍
- depth ratio 比绝对差对远处更敏感
- normal-aware epsilon 在掠射角更宽松

**但底层 motion vector 共享**——TAA 的 motion vector 作为输入，这里只是 *再加一层失效检测*。

## 4.5 Pass 2: Visibility Variance

```glsl
// 3D dispatch (width, height, active_lights)
vec4 last4 = texelFetch(visibility_cache_3d, [x, y, light_idx]);
float variance = max(last4) - min(last4);  // 简单 4-tap range
```

- **最近 4 帧 visibility 的极差**——粗略估计 noise level
- 高方差区 → fragment 处于 shadow 边缘 / 高速运动
- 低方差区 → fragment 稳定，可以降采样

为什么不用 *standard deviation*：

- 4 个样本算 std 计算量大
- max-min 已经够区分"稳定 vs 抖动"
- 跟后续 max filter 形态一致

## 4.6 Pass 3: Adaptive Ray Cast（核心）

### Step 3.1: Filtered Variance

```glsl
// LDS 缓存 5×5 邻域 variance
local_variance[ly][lx] = texelFetch(variance_3d, ...);
barrier();

// 5×5 max filter
local_max[ly][lx] = max_5x5(local_variance, ly, lx);
barrier();

// 13×13 tent filter on max
spatial_filtered = sum(local_max[y][x] * tent[y+6][x+6])
                   for (y, x) in [-6..6];

// blend with 4-frame variance history
last4_var = texelFetch(variance_cache_3d, ...);
filtered_variance = 0.5 * (spatial_filtered
                          + 0.25 * sum(last4_var));
```

**为什么 max + tent**：

- max filter 让 *附近高方差区* 影响当前 fragment——shadow 边缘不会突然从"高 sample"跳到"低 sample"
- tent 让权重渐变，避免 max 的硬边界
- 两者结合给出 *空间平滑的 sample budget*

### Step 3.2: Sample Count 决策

```glsl
sample_count = sample_count_history.x;  // 上一帧
bool stable = all 4 同 == sample_count;

if (filtered_variance > 0.2 && sample_count < MAX) {
    sample_count += 1;
} else if (stable && sample_count >= 1) {
    sample_count -= 1;
}

// 死锁保护：避免黑屏
if (sample_count == 0 && all 4 history == 0) {
    sample_count = 1;
}
```

**直觉**：

- 高方差 → 边界 / 运动 → 加 ray
- 4 帧稳定 → 收敛 → 减 ray
- 全 0 → 永远不会再投 ray，但场景黑掉 → 强制 1

### Step 3.3: Poisson Disk Multi-Ray

```glsl
visibility = 0;
for (s = 0; s < sample_count; ++s) {
    vec2 jitter = POISSON_SAMPLES[s * 4 + frame_idx];
    
    // 把 jitter 投到 light radius 球面
    vec3 dir = normalize(light_dir
                        + jitter.x * x_axis * scaled_radius
                        + jitter.y * y_axis * scaled_radius);
    
    rayQueryEXT q;
    rayQueryInitializeEXT(q, as, ..., dir, dist);
    rayQueryProceedEXT(q);
    
    visibility += (no hit) ? 1 : 0;
}
visibility /= sample_count;
```

**Poisson disk 而非 random**：

- 低 discrepancy（同 Halton 思路）
- `s * FRAME_HISTORY + frame_idx` → 跨帧 *不重复*，4 帧累积形成完整 disk
- 比 white noise 收敛快 4-8 倍

### Step 3.4: History 维护

```glsl
if (motion_vector valid) {
    last4 = shift_left(last4); last4.x = curr;
} else {
    last4 = vec4(curr);  // disocclusion → 全部重置
}
```

3 张 history（visibility / variance / sample_count）都用同样 ring 模式。

## 4.7 Pass 4: Spatio-Temporal Filter

```glsl
// LDS 缓存
local_visibility[ly][lx] = visibility_temporal_filter(global_idx);
local_normal[ly][lx]    = get_normal(global_idx);
barrier();

// Temporal: 4 帧 mean
vec4 last4 = texelFetch(visibility_cache_3d, ...);
filtered_t = 0.25 * (last4.x + last4.y + last4.z + last4.w);

// Spatial: 5×5 Gaussian + normal-aware skip
vec3 p_normal = local_normal[ly][lx];
for (y in [-2..2]) for (x in [-2..2]) {
    vec3 q_normal = local_normal[ly+y][lx+x];
    if (dot(p_normal, q_normal) <= 0.9) continue;  // ← 防 leak
    
    accum += local_visibility[ly+y][lx+x] * gaussian[y+2][x+2];
}
```

### Normal-Aware 跳过的意义

`dot(p_normal, q_normal) <= 0.9` 阻止 *阴影越过几何边缘漏过去*——RT shadow 经典 artifact。

不加这个：墙角 / 物体边缘的阴影会 *越界*，因为 5×5 filter 把对面表面的样本也混进来。

加这个：filter 自动按 *几何拓扑* 切割，不同 surface 各自滤波。

## 4.8 应用到 Lighting

```glsl
// 在 calculate_point_light_contribution
float shadow = texelFetch(global_textures_3d[shadow_visibility_idx],
                          ivec3(screen_uv, light_idx), 0).r;
float attenuation = attenuation_square_falloff(...) * shadow;
```

跟现有 lighting shader 几乎无侵入——只是把 cubemap shadow sample 替换成 visibility 3D texture sample。

## 4.9 性能数字（参考）

RTX 30 / 1080p：

- 朴素版（10 lights × 1 ray）：~0.3 ms
- Advanced 版总开销：~1.5-2.5 ms（包括 4 个 compute pass）
- 每 fragment 平均 ray 数：1-3（自适应后）

跟 cubemap shadow 对比（256 光源）：

- 256 cube shadow rasterization：~5-8 ms（即使有 sparse residency）
- 256 light RT shadow：~3-5 ms（前提：cluster 剔除把 active lights 限制到 5-15）

**关键**：RT shadow 性能 *跟光源数线性相关*；cluster lighting 必须配套，否则崩。

## 4.10 跟 Cluster Lighting 的协同

每 fragment *只对 cluster 命中的光源* 投 shadow ray：

```glsl
uint bin = compute_cluster_bin(view_pos);
bitfield mask = tile_bitfield[address];

for (light_id in cluster.lights[bin]) {
    if (mask has light_id) {
        float shadow = sample_shadow_3d(visibility_3d,
                                        screen_uv, light_id);
        contribution += light.color * shadow;
    }
}
```

**没有 cluster → 256 光源所有 fragment 都查 = 4.4 亿 ray/帧**，必崩。
有 cluster → 每 fragment 只查 5-15 lights = 几千万 ray/帧，可以接受。

详见 [gpu-driven-rendering 的 clustered lighting](../gpu-driven-rendering/README.md)。

## 4.11 Disocclusion 闪烁

`motion_vector = -1`（disocclusion）时，4 帧 history 全部重置 → 瞬时 visibility = 当前帧单 sample → 可能 1 帧极亮 / 极黑。

工业缓解：

- *firefly suppression*（clamp 到邻域 max）
- *disocclusion 区域增加 sample count*（不靠 history）
- 接受少量 1-2 帧瞬时 artifact（人眼难察觉）

书没强调，但 LX 落地必须处理。

## 4.12 LX 现状

| 维度 | LX | 需要 | gap |
|------|----|----|-----|
| `VK_KHR_ray_query` | 无 | 启用 | 小 |
| `rayQueryEXT` GLSL | 无 | shader extension | 小 |
| 3D texture (W, H, lights) | 无（vol fog 已铺一部分） | 复用 | 小 |
| Motion vector（特化版） | 无 | 在 temporal REQ-A 基础上加一层 | 小 |
| Adaptive sample count | 无 | 4-pass compute chain | 中 |
| Poisson disk 资产 | 无 | 静态 SBT | 小 |
| Cluster lighting 整合 | 无 | gpu-driven REQ-F | 中 |
| Cubemap shadow 共存 / fallback | 无 | shadows REQ-F | 中 |

## 4.13 一句话总结

**RT Shadow = ray query 把 4 行代码替换 shadow map。朴素版立刻可用但只支持 point light hard shadow；RT Gems advanced 4-pass + 3D texture history + Poisson disk + 时空滤波，soft shadow + scalable。Cluster lighting 配合是性能可控的关键。**

## 4.14 接下来读什么

- [05 RT DDGI](05-RT-DDGI.md) — Ch14：完整 RT pipeline 的第一个 use case
