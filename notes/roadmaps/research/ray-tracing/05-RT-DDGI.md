# 05 · RT DDGI

> 阅读前提：[03 RT 管线与 Ray Query](03-RT管线与ray-query.md) 讲清 RT pipeline + SBT + payload。本文展开 *Ch14 的 DDGI（Dynamic Diffuse Global Illumination）算法*——LX 引入完整 RT pipeline 的真正动机。

## 5.1 Global Illumination 是什么

**Direct lighting**：光源 → 表面 → 相机（一次反弹）
**Indirect / Global illumination**：光源 → 表面 A → 表面 B → 相机（多次反弹）

间接光是 *视觉真实感* 的核心：

- 红色地毯把红光反射到墙上 → 房间偏红
- 阳光从窗户进入 → 整个室内从白墙获得亮度
- 树叶反射的绿光填充树下阴影

传统方案：

| 方案 | 时间 | 动态 |
|------|------|------|
| Lightmap baking | 离线（小时级） | ❌ 静态 |
| VXGI（voxel cone tracing） | 实时 | ✅（NV demo） |
| Lumen（UE5） | 实时（screen + world space） | ✅ |
| **DDGI**（本章） | 实时 | ✅ |
| Path tracing | 实时（仅 RTX 4090） | ✅ |

DDGI 的定位：*入门级实时 GI 标准答案*——复杂度可控，画质中等，工程实用。

## 5.2 核心概念

| 概念 | 含义 |
|------|------|
| **Light probe** | 空间中一个点，记录"四面八方收到的光" |
| **Irradiance volume** | 3D 网格 probe，固定间距 |
| **Octahedral mapping** | 把球面展开成正方形 2D 纹理 |
| **Spherical Fibonacci** | 球面准随机点分布（3D 版 Halton） |
| **Hysteresis** | `result = mix(curr, prev, h)`，h 通常 0.97（极慢 temporal blend） |
| **Trilinear blending** | 采 8 个最近 probe 加权 |
| **Chebyshev visibility** | Variance Shadow Map 思路防漏光 |

**心智模型**：

- Lightmap = *烘焙* 静态 GI 到 mesh UV
- DDGI = *运行时* 用 RT 烘焙 GI 到 *3D probe 网格*

probe 网格分辨率 *远低于* 屏幕像素（典型 16×16×16 = 4096 probe），配合 octahedral 6×6 patch（含 1 像素 border）→ 单 grid ~150 KB。**用极少 ray + 极小存储换 GI**。

## 5.3 算法总览（5 步）

```
Pass 1: Ray trace per probe
        每 probe N 个 ray（spherical Fibonacci 方向）
        hit shader 算 direct lighting + 上一帧 irradiance（infinite bounce）
        miss shader 返回 sky color
        输出：radiance + distance 2D texture（行 = probe，列 = ray）

Pass 2: Irradiance update（octahedral）
        每 probe 一个 6×6 八面体 patch（含 1 像素 border for bilinear）
        per-texel 用 cosine weighted sum 累加 N ray 的 radiance
        hysteresis blend with previous frame

Pass 3: Visibility update（octahedral，独立 texture）
        每 probe 同样 patch，但存 (mean_dist, mean_dist²)
        用于 Chebyshev 测试 → 减少光泄漏

Pass 4 (可选): Probe offset
        统计 backface ray 比例
        > 25% → probe 在几何里 → 算偏移推出来
        cell-bound 限制：偏移 < probe spacing 的一半

Pass 5: Indirect lighting sample
        屏幕每像素：
          找 8 个最近 probe（trilinear weight）
          每 probe：octahedral 解 normal 方向 → 采 irradiance + visibility
          Chebyshev 测试给 probe 加权（防穿墙采错 probe）
          累加 → indirect diffuse
        通常 quarter resolution
```

## 5.4 Pass 1: Probe Ray Trace

### Ray Generation Shader

```glsl
layout(location = 0) rayPayloadEXT RayPayload payload;
struct RayPayload {
    vec3 radiance;
    float distance;
};

void main() {
    int probe_index = gl_LaunchIDEXT.y;
    int ray_index   = gl_LaunchIDEXT.x;
    
    ivec3 grid_idx = probe_index_to_grid(probe_index);
    vec3 origin = grid_indices_to_world(grid_idx, probe_index);
    
    // 球面准随机方向 + 每帧旋转
    vec3 dir = normalize(mat3(random_rotation)
                       * spherical_fibonacci(ray_index, probe_rays));
    
    payload.radiance = vec3(0);
    payload.distance = 0;
    traceRayEXT(as, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0,
                origin, 0.0, dir, 100.0, 0);
    
    imageStore(global_images_2d[radiance_output_idx],
               ivec2(ray_index, probe_index),
               vec4(payload.radiance, payload.distance));
}
```

**关键设计**：

- `dispatch_size = (rays_per_probe, num_probes, 1)`——每 ray 一个 thread
- `random_rotation` 每帧换 → 跨帧采到不同 ray 集合
- spherical Fibonacci 保证球面均匀分布
- `100.0` Tmax 限制大场景的 ray 走得太远

### Closest-Hit Shader

```glsl
layout(location = 0) rayPayloadInEXT RayPayload payload;
hitAttributeEXT vec2 barycentric;

void main() {
    if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT) {
        // 背面命中 → 负距离哨兵（probe offset 用）
        payload.distance = -0.2 * (gl_RayTminEXT + gl_HitTEXT);
        payload.radiance = vec3(0);
        return;
    }
    
    // 重建命中三角形（参考 [03 §3.5]）
    uint mesh_id = gl_InstanceCustomIndexEXT;
    MeshDraw mesh = mesh_draws[mesh_id];
    // ... 读 index/vertex/uv/normal buffer，重心插值
    vec3 world_pos = ...;
    vec3 normal = ...;
    vec2 uv = ...;
    
    // 采 albedo（bindless！）
    vec3 albedo = texture(global_textures[mesh.albedo_idx], uv).rgb;
    
    // direct lighting
    vec3 diffuse = albedo * direct_lighting(world_pos, normal);
    
    // 可选：infinite bounce — 采上一帧 irradiance
    diffuse += albedo
            * sample_irradiance(world_pos, normal, camera_pos)
            * infinite_bounces_multiplier;
    
    payload.radiance = diffuse;
    payload.distance = gl_RayTminEXT + gl_HitTEXT;
}
```

**关键设计**：

- 背面命中 → 负距离 → probe offset shader 用来检测 probe 在几何内部
- `direct_lighting()` 必须复用 cluster lighting（256 光源不能全跑）
- *infinite bounce* 通过 *采上一帧 irradiance* 实现——递归仅 1 层但等效无限弹

### Miss Shader

```glsl
void main() {
    payload.radiance = vec3(0.529, 0.807, 0.921);  // sky blue
    payload.distance = 1000.0;
}
```

工业版：采 environment cubemap 而不是常量天空色。

## 5.5 Pass 2: Irradiance Update（Octahedral）

### Octahedral 编码思想

把单位球面展开成正方形（菱形拓扑）：

```
  /\
 /  \         单位球面 ↔ 正方形 [-1,1]²
 \  /
  \/
```

每 probe 一个 6×6 patch（4×4 数据 + 1 像素 border）。**patch 第 (u,v) 个 texel 代表"从 probe 看 octahedral 方向 (u,v) 的辐射"**。

### Border 拷贝（关键 trick）

bilinear 采样在 patch 边缘需要邻居 texel；**border 拷贝 *对面* texel 而不是邻居 texel**——因为 octahedral 边缘几何上是 *自身环绕*。

不加 border：bilinear 在 patch 边缘采到隔壁 probe 的脏数据 → 漏光 / 错误。
加 border + 正确拷贝：每 probe 自成一个 closed surface。

shader 在 *同一个 dispatch* 里用 `groupMemoryBarrier` 完成 main + border 拷贝，省一次 dispatch。

### Update Shader

```glsl
// 检查是否 border
bool border_pixel = ((coords.x % 6) == 0 || (coords.x % 6) == 5)
                 || ((coords.y % 6) == 0 || (coords.y % 6) == 5);

if (!border_pixel) {
    vec4 result = vec4(0);
    uint backfaces = 0;
    
    for (int ray_index = 0; ray_index < probe_rays; ++ray_index) {
        vec3 ray_dir = normalize(mat3(random_rotation)
                               * spherical_fibonacci(ray_index, probe_rays));
        vec3 texel_dir = oct_decode(normalized_oct_coord(coords.xy));
        
        float weight = max(0, dot(texel_dir, ray_dir));  // cosine weight
        
        float distance = texelFetch(radiance_output, [ray_index, probe_idx]).w;
        
        if (distance < 0 && use_backfacing_blending()) {
            ++backfaces;
            if (backfaces >= max_backfaces) return;  // 太多 backface → probe 在墙里
            continue;
        }
        
        if (weight >= EPSILON) {
            vec3 radiance = texelFetch(radiance_output, [ray_index, probe_idx]).rgb;
            result += vec4(radiance * weight, weight);
        }
    }
    
    if (result.w > EPSILON) {
        result.xyz /= result.w;
    }
    
    // Hysteresis blend
    vec4 prev = imageLoad(irradiance_image, coords.xy);
    result = mix(result, prev, hysteresis);  // h = 0.97 (extremely slow)
    imageStore(irradiance_image, coords.xy, result);
    return;
}

// border 路径
groupMemoryBarrier();
barrier();
// 计算 source pixel coordinate (复杂的 octahedral 边缘映射)
// 拷贝 source → border
imageStore(irradiance_image, coords.xy, copied_data);
```

### Hysteresis = 0.97 的意义

```
h = 0.97 → 33 帧才换 50% 权重（vs TAA 的 0.9 = 7 帧）
```

为什么这么慢：

- probe 数据要 *cross-frame stable*，闪烁直接影响整个屏幕
- low ray count（典型 64-128 ray/probe）必须靠超长时间窗口收敛
- 代价：动态光源响应慢（光开关后 *数十帧* 才稳定）

工业改良：检测 *剧变* 时临时降 hysteresis（不在 v1 范围）。

## 5.6 Pass 3: Visibility Update

跟 irradiance update 用 *同一个 shader*，只是存的不一样：

```glsl
// 不是 radiance，是 (mean_dist, mean_dist²)
float distance = min(abs(distance), probe_max_ray_distance);
vec3 value = vec3(distance, distance * distance, 0);
result += vec4(value * weight, weight);
```

Visibility texture 给 [§5.8 Chebyshev 测试](#58-chebyshev-可见性权重防漏光的关键) 用。

## 5.7 Pass 4: Probe Offset

如果 probe 卡墙体内，会"看到"全是 backface，输出错的 GI。Offset 把它沿 backface 反方向推出。

```glsl
// 统计 backface 比例
for (int ray_index = 0; ray_index < probe_rays; ++ray_index) {
    float dist = texelFetch(radiance, ...).w;
    if (dist <= 0) ++backfaces;
}

bool inside_geometry = (float(backfaces) / probe_rays) > 0.25;

if (inside_geometry && closest_backface_idx != -1) {
    // 计算 backface 方向
    vec3 backface_dir = closest_backface_dist
                      * normalize(spherical_fibonacci(closest_backface_idx, ...));
    
    // cell-bound 限制
    vec3 cell_limit = max_offset * probe_spacing;
    
    // 在 cell 内反向移动
    full_offset = current_offset - backface_dir * direction_scale;
}
```

**只在场景加载 / 网格变化时跑**，不能每帧跑（probe 会 *漂移*）。书建议 ~5 帧迭代收敛。

## 5.8 Chebyshev 可见性权重（防漏光的关键）

类似 *Variance Shadow Map* 思路：

```glsl
// visibility texture 存 (mean_dist, mean_dist²)
vec2 visibility = textureLod(visibility_tex, uv, 0).rg;
float mean_dist = visibility.x;

if (distance_to_probe > mean_dist) {
    float variance = visibility.x * visibility.x - visibility.y;
    float diff = distance_to_probe - mean_dist;
    chebyshev = variance / (variance + diff * diff);
    chebyshev = max(chebyshev * chebyshev * chebyshev, 0);  // 提高对比度
}
chebyshev = max(0.05, chebyshev);
weight *= chebyshev;
```

**直觉**：墙后面的 probe 的 visibility 表示"它能看到的东西离它多远"。如果当前 fragment 离 probe 比 visibility 远 → probe 跟 fragment *中间隔了墙* → 降权。

这是 DDGI 防 *light leak through walls* 的核心。

## 5.9 Pass 5: Indirect Lighting Sample

### 主流程（compute shader）

```glsl
void main() {
    // quarter resolution → 找 closest depth in 2x2
    vec2 uv = ...;
    vec3 world_pos = world_position_from_depth(uv, depth, inv_view_proj);
    vec3 normal = ...;
    
    vec3 irradiance = sample_irradiance(world_pos, normal, camera_pos);
    imageStore(indirect_output, coords, vec4(irradiance, 1));
}
```

### sample_irradiance 函数

```glsl
vec3 sample_irradiance(vec3 world_pos, vec3 normal, vec3 cam_pos) {
    // bias 沿 normal + view 推一小段（防 self-shadow）
    vec3 V = normalize(cam_pos - world_pos);
    vec3 bias = (normal * 0.2 + V * 0.8) * 0.75 * self_shadow_bias;
    vec3 biased_pos = world_pos + bias;
    
    ivec3 base_grid = world_to_grid_indices(biased_pos);
    vec3 base_pos = grid_indices_to_world(base_grid);
    vec3 alpha = clamp(biased_pos - base_pos, 0, 1);
    
    vec3 sum_irradiance = vec3(0);
    float sum_weight = 0;
    
    // 8 个最近 probe
    for (int i = 0; i < 8; ++i) {
        ivec3 offset = ivec3(i, i >> 1, i >> 2) & 1;
        ivec3 probe_grid = clamp(base_grid + offset, 0, probe_counts - 1);
        int probe_idx = probe_indices_to_index(probe_grid);
        vec3 probe_pos = grid_indices_to_world(probe_grid, probe_idx);
        
        // Trilinear weight
        vec3 trilinear = mix(1 - alpha, alpha, offset);
        float weight = 1.0;
        
        // Chebyshev visibility test
        vec3 to_biased = biased_pos - probe_pos;
        float dist = length(to_biased);
        vec3 to_dir = to_biased / dist;
        
        vec2 vis_uv = get_probe_uv(to_dir, probe_idx, ...);
        vec2 vis = textureLod(visibility_tex, vis_uv, 0).rg;
        
        if (dist > vis.x) {
            float var = abs(vis.x * vis.x - vis.y);
            float diff = dist - vis.x;
            weight *= max(0.05, max(0, var / (var + diff * diff)));
        }
        
        // Sample irradiance
        vec2 uv = get_probe_uv(normal, probe_idx, ...);
        vec3 probe_irr = textureLod(irradiance_tex, uv, 0).rgb;
        
        weight *= trilinear.x * trilinear.y * trilinear.z + 0.001;
        sum_irradiance += weight * probe_irr;
        sum_weight += weight;
    }
    
    return 0.5 * PI * sum_irradiance / sum_weight;
}
```

### 应用到 Lighting

```glsl
// 在主 lighting shader 末端
vec3 indirect_irradiance = textureLod(indirect_lighting_tex, uv, 0).rgb;
vec3 kD = (1.0 - F) * (1.0 - metallic);  // diffuse 占比
vec3 indirect_diffuse = indirect_irradiance * base_colour;
final_color += kD * indirect_diffuse * ao;
```

## 5.10 内存量级

```
16×16×16 probes × octahedral 6×6 patch × RGBA16 (irradiance) ≈ 150 KB
+ visibility (RG16) ≈ 75 KB
+ probe offset texture
+ radiance/distance ray texture: 128 × 4096 × RGBA16 ≈ 4 MB
─────────────
单 grid 总 ~5 MB
```

*单 grid* 不大，但 *多 cascade*（户外）就上 MB 级。

## 5.11 性能数字（参考）

RTX 30 / 16×16×16 probes / 64 rays/probe：

| Pass | 时间 |
|------|------|
| Probe ray trace | 0.5-1 ms |
| Irradiance + visibility update | 0.2 ms |
| Probe offset (only on load) | 0.1 ms |
| Indirect sample (quarter res) | 0.3 ms |
| **Total** | ~1.5 ms |

## 5.12 LX 现状

| 维度 | LX | 需要 | gap |
|------|----|----|-----|
| RT pipeline | 无 | REQ-C | 高 |
| Buffer device address | 无 | REQ-A | 中 |
| Bindless texture from RT | 无 | REQ-A + bindless 整体 | 中 |
| Mesh data 直接 GPU 访问 | 无 | mesh asset 重组 | 中 |
| 3D probe grid 资源 | 无 | 新资源类型 | 中 |
| Octahedral 编码 utils | 无 | shader util | 小 |
| Spherical Fibonacci utils | 无 | shader util | 小 |
| Hysteresis blend | 无 | shader util | 小 |
| Cluster lighting in hit shader | 无 | gpu-driven REQ-F | 中 |
| Probe offset compute | 无 | one-shot compute | 小 |

## 5.13 跟其它特性对比

| 维度 | Ch13 Shadow | Ch14 DDGI | Ch15 Reflection（下一篇） |
|------|------------|-----------|-----------------|
| RT 调用 | ray query | RT pipeline + SBT | RT pipeline + SBT |
| Ray 来源 | per-fragment | per-probe | per-fragment |
| Ray 数 | adaptive 1-N | per-probe 64-128 | 1 |
| 输出空间 | screen | 3D probe grid | screen |
| Hit shader 算 | 不算（binary） | direct lighting + 上一帧 GI | direct lighting via importance sampling |
| 滤波 | max + tent + Gaussian | hysteresis blend (slow) | SVGF（最复杂） |
| 历史窗口 | 4 帧 | 30+ 帧（h=0.97） | ~10 帧（α=0.2） |
| 防漏 | normal-aware 5×5 | Chebyshev visibility | mesh_id + depth_dd |

## 5.14 接下来读什么

- [06 RT Reflection 与 SVGF](06-RT-reflection与SVGF.md) — Ch15：1 ray/pixel + GGX VNDF + SVGF 通用 denoiser
