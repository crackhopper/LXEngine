# 03 · Volumetric Fog Pipeline

> 阅读前提：[02](02-volumetric-fog-原理.md) 讲清原理 + froxel 数据结构。本文展开 *5 步 compute pipeline 的实施*。

## 3.1 5 步算法概览

```
Step 1: Data injection (compute)         → froxel: scattering + extinction
        (常量雾 + 高度雾 + 体积雾)

Step 2: Light scattering (compute)       → 算每 froxel 的 in-scattering
        (复用 clustered lighting + phase func + shadow)

Step 3: Spatial filter (compute)         → X/Y 高斯模糊去噪

Step 4: Temporal filter (compute)        → 跟上一帧 blend 去 banding

Step 5: Light integration (compute)      → ray marching：累加 scattering，算 transmittance

Step 6: Lighting pass apply              → 用 froxel sample 应用到 scene color
```

CPU 仅 5 次 dispatch + 1 次 lighting pass。

## 3.2 Step 1：Data Injection

### 三种雾的叠加

```glsl
// 1. 常量雾（整个场景均匀）
fog_density += density_modifier;

// 2. 高度雾（地面浓 / 高空稀）
height_fog = height_density × exp(-falloff × max(world_y, 0));
fog_density += height_fog;

// 3. 体积雾（在指定 box 内）
if (world_pos in box) {
    fog_density += box_color × box_density;
}
```

### Color/Density → Scattering/Extinction

```glsl
vec4 scattering_extinction_from_color_density(vec3 color, float density) {
    float extinction = scattering_factor × density;
    return vec4(color × extinction, extinction);
}
```

打包成 RGBA：rgb = scattering，a = extinction。

### Perlin Noise 扰动

```glsl
vec3 sampling_coord = world_position × multiplier + (1, 0.1, 2) × frame × speed;
float fog_noise = texture(volumetric_noise_3d, sampling_coord).x;
fog_density × = fog_noise;
```

3D Perlin noise + 时间偏移 → 雾自然抖动 + 飘动。

## 3.3 Step 2：Light Scattering

### 复用 Clustered Lighting

跟 clustered lighting 的 light loop *几乎完全一样*：

```glsl
// 算 froxel 在 cluster 体系里的 bin
float linear_d = froxel_z / num_z_slices;
int bin_index = int(linear_d / BIN_WIDTH);
uint bin_value = bins[bin_index];

uint min_light_id = bin_value & 0xFFFF;
uint max_light_id = (bin_value >> 16) & 0xFFFF;

// 在 bin 范围内迭代光源
for (light_id in [min_light_id, max_light_id]) {
    if (tile_bitfield[address] has bit light_id) {
        accumulate_volumetric_lighting(...);
    }
}
```

### Phase Function 取代 BRDF

```glsl
// 正常 surface lighting:
contribution = albedo × max(N·L, 0) × shadow × attenuation;

// Volumetric lighting:
contribution = phase(view_dir, -light_dir) × shadow × attenuation;
```

Volumetric 没有法线，所以用 phase function 代替 BRDF 的方向反应。

### Shadow Map 采样

```glsl
shadow = sample_cubemap_shadow(light_index, world_pos);
contribution × = shadow;
```

**这是 volumetric fog 最具视觉冲击的部分**：

- 没 shadow → 雾整体被光照亮，平淡
- 有 shadow → 看到 god rays、光斑、动态阴影

## 3.4 Step 3：Spatial Filter

X/Y 方向 5×5 高斯模糊（不模糊 Z 因为 Z 切分本身就是非线性的）：

```glsl
for (i in [-2, 2])
    for (j in [-2, 2]) {
        weight = gaussian(distance(i, j))
        accumulator += sampled_value × weight
    }
```

**轻微平滑** — 不能太重否则细节丢失。

## 3.5 Step 4：Temporal Filter

跟 [01 §1.5](01-temporal-范式.md#15-reprojection) 的 reprojection 相同思路：

```glsl
// 1. 把当前 froxel 的世界坐标 reproject 到上一帧的 NDC
vec3 prev_ndc = previous_view_projection × world_pos;
vec3 history_uv = ndc_to_uv(prev_ndc);

// 2. 采上一帧的 light scattering 结果
vec4 history = texture(prev_light_scattering, history_uv);

// 3. 混合
result = mix(history, current, blend_factor);  // 典型 blend = 0.05
```

`blend_factor = 0.05` 意思 *95% 上一帧 + 5% 当前帧*，时间窗口 ~20 帧。

### History sample 失效检查

```glsl
if (history_uv outside [0, 1]³) skip blend;
```

否则鬼影。

### 跟 TAA 的关系

Temporal filter 是 *TAA 在 volumetric 维度的应用*。共享 [01](01-temporal-范式.md) 的全部基础设施。

## 3.6 Step 5：Light Integration（Ray Marching）

### 核心公式

每个 froxel 沿 Z 轴累加 scattering 和 transmittance：

```glsl
integrated_scattering = vec3(0);
integrated_transmittance = 1.0;

for (z in [0, depth_slices]) {
    sampled_scattering = texture(scattering_3d, froxel_xyz);
    sampled_extinction = sampled_scattering.a;
    
    z_step = next_z - current_z;
    transmittance = exp(-extinction × z_step);  // Beer-Lambert
    
    // analytical integration of 散射 over z_step
    scattering = (sampled_scattering - sampled_scattering × transmittance) / extinction;
    
    integrated_scattering += scattering × integrated_transmittance;
    integrated_transmittance × = transmittance;
    
    write_to_3d_texture(froxel_xyz, integrated_scattering, integrated_transmittance);
}
```

### Pre-Integrated Ray Marching

这步存的是 **"从相机到这个 froxel 的累积结果"**，不是 *单个 froxel 的属性*。

类似 *prefix sum*：每个 froxel 已经把自己 + 前面所有 froxel 的累加 baked 好。

后续 lighting pass 应用时只要 *单 sample*：

```glsl
vec4 fog = sample_3d_texture(integrated_volumetric_fog, froxel_xyz);
final_color = scene_color × fog.a + fog.rgb;
//             ↑ transmittance     ↑ scattering
```

**O(1) 应用**。这是让 volumetric fog real-time 的核心。

## 3.7 Step 6：Apply to Scene

```glsl
vec3 apply_volumetric_fog(vec2 screen_uv, float raw_depth, vec3 color) {
    float linear_depth = raw_depth_to_linear_depth(raw_depth, near, far);
    float depth_uv = linear_depth_to_uv(near, far, linear_depth, num_slices);
    vec3 froxel_uvw = vec3(screen_uv, depth_uv);
    
    vec4 fog = texture(volumetric_fog_3d, froxel_uvw);
    
    return color × fog.a + fog.rgb;
}
```

最后一行就是 *Beer-Lambert 应用*：

- 场景 color × transmittance（雾遮挡场景）
- 加 scattering（雾本身的颜色）

无论 forward+ 还是 deferred 都在 lighting shader 末尾调一次。

## 3.8 Banding 抑制：Noise 双子星

### Perlin Noise（动画化雾密度）

启动一次 compute shader 烘焙 3D Perlin noise 到 texture，运行期采样 + 时间偏移。

### Blue Noise（采样位置抖动）

[01 §1.7](01-temporal-范式.md#17-blue-noise--golden-ratio) 已讲。在 *froxel sample 位置* 加 blue noise 偏移：

```glsl
vec2 jitter = blue_noise(pixel, frame) × pixel_size;
sample_pos += jitter;
```

让 banding 变成 *像噪声*（人眼可接受）而不是 *像横纹*（极敏感）。配合 temporal filter 多帧累加平掉，最终 *无 banding 又无 noise*。

## 3.9 资源依赖图

```
启动期一次:
  Perlin noise 3D 烘焙
  Blue noise 2D 加载

每帧:
  [Step 1 injection] →  froxel_data_3d (scattering+extinction)
  [Step 2 scattering] → light_scattering_3d
                          ↑ clustered lighting bins/tiles + cubemap shadow
  [Step 3 spatial filter] → froxel_data_3d (复用)
  [Step 4 temporal filter] → light_scattering_3d (复用)
                              ↑ history_light_scattering_3d (跨帧)
  [Step 5 integration] → integrated_3d (final)
  [Step 6 lighting apply] → final scene color
```

注意：

- **history_light_scattering_3d 跨帧 retire** → multi-threading/08 RetirePoint 模型
- **跟 cubemap shadow 共享** → 需要 frame graph 跨 pass 资源管理

## 3.10 共需要几张 3D Texture

```
froxel_data_3d            (8 MB)   — Step 1 / 3 用
light_scattering_3d        (8 MB)   — Step 2 / 4 用
history_light_scattering   (8 MB)   — Step 4 history
integrated_volumetric_fog  (8 MB)   — Step 5 输出
volumetric_noise_3d        (静态 4 MB) — 烘焙 Perlin
─────────────
总 ~36 MB
```

可以接受，跟 G-buffer 内存量级相近。

## 3.11 接下来读什么

- [04 TAA](04-TAA.md) — Temporal Anti-Aliasing 算法 + 5 大改进点
