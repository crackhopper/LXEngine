# 01 · Temporal 范式

> 阅读前提：理解 fragment shader / compute shader 基础。
>
> 本文是 [02 Volumetric Fog](02-volumetric-fog-原理.md) 和 [04 TAA](04-TAA.md) 的 *共享基础*。两个特性都消费这套机制。

## 1.1 Temporal Techniques 是什么

**Temporal techniques = 跨多帧累积样本的渲染范式**。

每帧渲染的样本数有限（一像素一次 fragment shader）。把 *多帧的样本累积起来*，等价于 super-sampling，但成本只增加一帧的累积开销。

应用：

- **TAA**：跨帧 sub-pixel sample → anti-aliasing
- **Volumetric Fog**：跨帧 froxel 累积 → 减 banding
- **SSR**（Screen-Space Reflections）：跨帧 ray sample → 平滑反射
- **TAAU / DLSS**：跨帧 + AI → 上采样

工业引擎里这些是 *单一 temporal 子系统*，共享基础设施。

## 1.2 三个核心机制

### 机制 1：Camera Jitter

每帧轻微偏移 camera projection matrix，让 *相邻帧采样不同 sub-pixel 位置*：

```cpp
// 偏移在 [-1, 1] 的 sub-pixel 范围
camera.projection.m20 += jitter_x / screen_width;
camera.projection.m21 += jitter_y / screen_height;
```

**关键**：偏移必须 *小于 1 个 pixel*。除以分辨率把 [-1, 1] 映射到 [-1/W, 1/W]。

每帧重置 projection 后再加 jitter，否则 jitter 会累积。

### 机制 2：Motion Vectors

每个 pixel 记录 *上一帧在哪*：

```
motion_vector[pixel] = current_screen_pos - previous_screen_pos
```

来源 *两类*：

- **Camera motion**：相机自己移动 → per-pixel 算
- **Object motion**：物体自己移动 → vertex/mesh shader 输出 per-vertex

### 机制 3：History Texture（双 Buffer）

```
History buffer:    上一帧的输出
Current buffer:    本帧渲染中
   ↓
本帧结束:
  history = current
  swap buffer
```

两个 buffer 轮流当 history / current，无需 copy。

## 1.3 Halton 序列 + Quasi-Random Jitter

**为什么不用纯随机**：纯随机会让 jitter 集中或漏（uniform 分布 + 低相关性是 super-sampling 的要求）。

**Quasi-random sequences**（Halton / Hammersley / R2 / Interleaved Gradients）：低 discrepancy，相邻样本最大化分散。

```cpp
f32 jitter_x = halton(jitter_index, 2);  // base 2
f32 jitter_y = halton(jitter_index, 3);  // base 3
jitter_index = (jitter_index + 1) % jitter_period;  // typical 4-16
```

Period 短收敛快，period 长样本多。`4` 是 common 默认。

## 1.4 Motion Vector 计算

### Camera Motion (compute shader)

```glsl
// 1. 从 depth 重建 world position
vec3 world_pos = world_position_from_depth(screen_uv, depth, inv_view_proj);

// 2. 用上一帧的 view-projection 算它在上一帧的位置
vec4 prev_ndc = previous_view_projection × vec4(world_pos, 1.0);
prev_ndc.xyz /= prev_ndc.w;

// 3. 减去 jitter difference
vec2 jitter_diff = (jitter_xy - prev_jitter_xy) × 0.5;
vec2 velocity = current_ndc.xy - prev_ndc.xy - jitter_diff;
```

**关键**：`jitter_diff` 必须减掉，否则 motion vector 包含 jitter 抖动 → TAA 不收敛。

### Object Motion (vertex/mesh shader)

```glsl
vec4 world_pos = model × vec4(position, 1.0);
vec4 prev_ndc = previous_view_projection × world_pos;
prev_ndc.xyz /= prev_ndc.w;

vec2 jitter_diff = (jitter_xy - prev_jitter_xy) × 0.5;
vec2 velocity = curr_ndc.xy - prev_ndc.xy - jitter_diff;

vTexcoord_Velocity[i] = velocity;
```

骨骼动画 / 物理仿真物体必须做这步。

### Velocity Buffer Format

```
R16G16   — 2 channel half float per-pixel
1080p ≈ 8 MB
```

精度足够（half float 在 [-1, 1] 范围内有 ~0.0005 精度），内存可控。

## 1.5 Reprojection

```glsl
vec2 prev_uv = current_uv - velocity;
vec3 history_color = sample_texture(history_buffer, prev_uv);
```

**Linear sampler**（不是 nearest）：velocity 不是整数 pixel，需要插值。

**关键**：必须用 *上一帧的 history buffer*，不是上一帧的 raw scene color。history 已经包含 *N 帧累积* 的结果，连续 history blend 才能让样本数指数增长。

## 1.6 History 失效检查

reproject 到上一帧 *可能失败*：

| 失效情形 | 表现 | 检测 |
|---------|------|------|
| **Out-of-frustum** | reproject 出 [0,1] | 直接 skip blend |
| **Disocclusion** | 新露出的区域 history 是错的物体 | 检测 motion vector 突变 / depth 突变 |
| **Camera cut** | 切镜头，整张 history 失效 | trigger TAA reset |

不检查 → ghosting（鬼影）。

## 1.7 Blue Noise + Golden Ratio

### Blue Noise

高频均匀分布的随机噪声。视觉上 *人眼对高频不敏感*，比白噪声好。

### Golden Ratio 时间偏移

```cpp
const float GR = 0.61803398875;
float frame_offset = (frame % 256) × GR;
float blue_noise_animated = fract(static_blue_noise + frame_offset);
```

Golden ratio 让相邻帧的 noise 序列 *最大化不相关*，TAA 收敛极快（4-8 帧）。

### Triangular Mapping

```cpp
float triangular = noise0 + noise1 - 1.0;  // 两个 [0,1] 均匀 → [-1,1] 三角分布
```

钟形分布比矩形更柔和，artifact 少。

## 1.8 Animated Dithering 配方

```
1. 渲染时引入激进 dithering（blue noise 抖动）→ 看起来很噪
2. Temporal accumulation 跨帧平均噪声 → 收敛干净
3. 净效果：等效更多采样，画质提升
```

这是 stochastic rendering 的核心配方。Volumetric fog（[02-03](02-volumetric-fog-原理.md)）和 TAA（[04](04-TAA.md)）都用。

## 1.9 LX 现状

LX 完全没有 temporal 基础设施：

- ❌ 没 jitter 路径
- ❌ 没 motion vector buffer
- ❌ 没 history texture 双 buffer
- ❌ 没 previous-frame view-projection 缓存
- ❌ 没 blue noise 资产
- ❌ 没 golden ratio jitter

这些都是 [06 演进路径](06-演进路径.md) 的 REQ-A 内容（共享基础设施，最先建）。

## 1.10 接下来读什么

- [02 Volumetric Fog 原理](02-volumetric-fog-原理.md) — 体积渲染物理基础 + froxel 数据结构
