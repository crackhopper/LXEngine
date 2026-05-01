# 02 · Volumetric Fog 原理

> 阅读前提：[01](01-temporal-范式.md) 讲清 temporal 共享基础。本文展开 *volumetric rendering 的物理基础 + froxel 数据结构*。
>
> 实施层 5 步 pipeline 见 [03 Volumetric Fog Pipeline](03-volumetric-fog-pipeline.md)。

## 2.1 Volumetric Rendering 是什么

**描述光在 *参与介质* 中的行为**。参与介质 = 雾 / 云 / 大气 / 烟尘等含有 *局部密度变化* 的体积。

跟传统 surface rendering 的区别：

| Surface rendering | Volumetric rendering |
|------------------|---------------------|
| 光线碰到 surface 反射 / 吸收 | 光线穿过 volume 累积变化 |
| BRDF 描述方向反应 | Phase function 描述散射方向 |
| 单 fragment 独立计算 | 沿视线累积 |
| 几何 = mesh | 几何 = 3D 密度场 |

## 2.2 三种光-介质相互作用

光线穿过参与介质时发生：

| 现象 | 含义 | 能量收支 |
|------|------|---------|
| **Absorption**（吸收） | 光被介质吸收成为热量 | 净损失 |
| **Out-scattering**（散出） | 光被介质散射到其它方向 | 净损失（对当前视线） |
| **In-scattering**（散入） | 其它光源的光被介质散射进当前视线 | 净增加 |

简化的核心：**沿一条视线走，光强变化 = -吸收 - 散出 + 散入**。

## 2.3 Phase Function：散射方向分布

光散射 *不是各向同性*。**Phase function** 描述方向分布。

最常用的 **Henyey-Greenstein**：

```
        1 - g²
P(θ) = ─────────────────────
       4π · (1 + g² - 2g·cos θ)^1.5
```

- `θ` = view vector 跟 light vector 的夹角
- `g` = anisotropy 参数

| g 值 | 含义 | 用途 |
|------|------|------|
| 0 | 各向同性（球形分布） | 简单雾 |
| 0 < g < 1 | 前向散射偏好 | 真实雾 / 云（典型 g=0.3-0.7） |
| -1 < g < 0 | 后向散射偏好 | 罕见 |

## 2.4 Extinction & Transmittance

| 量 | 含义 | 用途 |
|----|------|------|
| **Extinction**（消光系数 σ） | 单位长度上 *吸收 + 散出* 的量 | 算法中间存储 |
| **Transmittance**（透射率 T） | 光穿过一段后剩多少 | 最终应用 |

**Beer-Lambert 定律** 把两者关联：

```
T(d) = e^(-σ·d)

T = transmittance（剩多少）
σ = extinction
d = 距离
```

距离越远 / 介质越浓 → transmittance 越小。

## 2.5 应用到场景的公式

```
final_color = scene_color × transmittance + scattering
              ↑                              ↑
              雾遮挡场景                       雾自身散射光的贡献
```

雾把场景颜色 *衰减*（× T）+ 雾本身 *发光*（+ S）。

## 2.6 Froxel 数据结构

**Froxel = Frustum + Voxel**：把视锥切成 3D 网格，每格存一份雾的属性。

```
Camera frustum
  ┌─────────────┐ ← 屏幕近处（高密度 froxel）
  │ . . . . . . │
  │ .  .  .  .  │
  │ . . . . . . │
  │  .   .   .  │
  │ .  .  .  .  │
  └─────────────┘ ← 远处（同样 froxel 数，但每个覆盖更大世界空间）
```

3D texture，分辨率 128×128×128（书选）或 160×90×64（更现代，跟 1080p tile 化对齐）。

## 2.7 为什么是 Frustum-Aligned

**Cartesian volume**（世界空间均匀网格）：

- 远近 same texel size
- 屏幕上远处占 pixel 少 → 浪费分辨率
- 视锥外 texel 完全浪费

**Frustum-Aligned volume**：

- texel 跟视锥对齐 → 屏幕上每像素对应固定 texel 数
- 视锥外没 texel
- 跟 *clustered lighting* 的 frustum 切分思路一致 — 可以共用 cluster 数据结构

## 2.8 Z 方向的非线性分布

Z 方向均匀切片 → 远处 texel 太大（精度差）。改用 *指数分布*：

```glsl
// id Tech 风格
z_world = near × (far / near)^(slice / num_slices)
```

效果：

```
slice 0  → z = 0.1m   （高精度近处）
slice 1  → z = 0.15m
...
slice 60 → z = 50m
slice 63 → z = 100m  （粗精度远处）
```

跟人眼对深度的敏感度（近敏感 / 远不敏感）匹配。

## 2.9 Froxel 存储格式

```
RGBA16F (8 bytes / froxel)
  RGB = scattering（颜色 × 浓度）
  A   = extinction（吸收 + 散出系数）
```

128³ × 8 字节 ≈ 16 MB。可接受。

实际 pipeline 会有 *多个 3D texture*（5 步 pipeline 中的中间结果），见 [03 §3.1](03-volumetric-fog-pipeline.md#31-5-步算法概览)。

## 2.10 Froxel ↔ World 坐标转换

### World → Froxel

```glsl
// 1. World → Camera space
vec4 view_pos = view × world_pos;

// 2. Camera → NDC
vec4 ndc = projection × view_pos;
ndc.xyz /= ndc.w;

// 3. NDC → froxel UVW (XY 直接映射，Z 取指数 slice)
vec2 froxel_uv = ndc.xy × 0.5 + 0.5;
float depth_slice = depth_to_slice(linear_depth, near, far, num_slices);
vec3 froxel_uvw = vec3(froxel_uv, depth_slice / num_slices);
```

### Froxel → World

逆过程，给 froxel 中心算它对应的世界坐标：

```glsl
vec3 froxel_uvw = froxel_coord / froxel_dimensions;
vec3 ndc = vec3(froxel_uvw.xy × 2 - 1, slice_to_exponential_depth(froxel_uvw.z));
vec4 world_pos = inverse_view_projection × vec4(ndc, 1.0);
world_pos.xyz /= world_pos.w;
```

## 2.11 跟 Clustered Lighting 的关系

Volumetric fog 的 *light scattering* step（[03 §3.3](03-volumetric-fog-pipeline.md#33-step-2light-scattering)）跟 clustered lighting 几乎一致：

- 同样 frustum 切分
- 同样 1D bin + 2D tile bitfield 数据结构
- 同样 per-froxel/fragment 迭代光源

**直接复用 clustered lighting 的 GPU 数据**，只是把 surface BRDF 换成 phase function。

## 2.12 为什么 froxel 让 real-time 成为可能

**朴素 ray marching**：每像素沿视线走 N 步，每步采样雾密度 + 光照。1080p × 64 步 × 蛋白函数计算 = 离 real-time 很远。

**Froxel pre-integration**：

- 5 步 compute pipeline 在 froxel 上累积一次（128³ 总计算量 << 每像素 64 步）
- 应用阶段 *单 sample*，O(1)

**这是 Wronski 算法的核心创新**。后续 RDR2 / Cyberpunk / UE5 都用同样思路。

## 2.13 接下来读什么

- [03 Volumetric Fog Pipeline](03-volumetric-fog-pipeline.md) — 5 步 compute pipeline 实施
