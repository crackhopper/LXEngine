# 03 · Culling Pipeline

> 阅读前提：[02](02-meshlet资产层.md) 已经讲清 meshlet 数据。本文展开 Ch6 的 culling 算法、Hi-Z、indirect drawing、以及 mesh shader vs compute 两条路径。

## 3.1 完整渲染管线总览

```
CPU 每帧:
    vkCmdDispatch (compute culling phase 1)
    vkCmdDrawIndexedIndirectCount (graphics phase 1)
    vkCmdDispatch (Hi-Z 更新)
    vkCmdDispatch (compute culling phase 2)
    vkCmdDrawIndexedIndirectCount (graphics phase 2)

GPU 自动展开:
    Phase 1 culling:
        Mesh 级 frustum + occlusion (用上一帧 Hi-Z) → mesh 候选列表
        Meshlet 级 frustum + back-face cone → 写 draw commands
    Phase 1 draw:
        画通过 cull 的物体，更新 depth buffer
    Hi-Z 更新:
        max-reduce mip chain
    Phase 2 culling:
        Phase 1 被剔除的候选 → 用新 Hi-Z 重测 → 写补充 draw commands
    Phase 2 draw:
        画 false negative
    Final depth → 下一帧用
```

CPU 一帧只发 5 个命令，剩下都在 GPU 上。

## 3.2 Cone Culling（back-face 剔除）

每个 meshlet 自带 cone（axis + cutoff）。书里的代码：

```glsl
bool coneCull(vec3 center, float radius, vec3 cone_axis,
              float cone_cutoff, vec3 camera_position) {
    return dot(center - camera_position, cone_axis) >=
           cone_cutoff * length(center - camera_position) + radius;
}
```

朴素版本是 `dot(view_dir, axis) > cutoff`，但加 radius 项使球边缘 triangle 也被正确处理（球越近越严格）。

**几何意义**：cone 半角 θ 由 meshlet 内法线发散度决定。`cone_cutoff = sin(θ)`。

| θ | cull 率 | 适合 |
|---|--------|------|
| 0° | 50%（半空间） | 平面 patch |
| 30° | 33% | 缓曲面 |
| 60° | 17% | 高曲率 |
| ≥90° | 0% | 急弯 / sharp edge 跨界 |

cone 是 *back-face 维度* 的剔除工具。其它维度（frustum / occlusion）需要单独的测试链路。

## 3.3 Frustum Culling（视锥剔除）

```glsl
center = world_to_camera * center;
for (uint i = 0; i < 6; ++i) {
    frustum_visible &= dot(frustum_planes[i], center) > -radius;
}
```

球-平面测试：球心到 6 个 frustum 平面的距离都不超过 -radius，即"球至少部分在平面正侧"。

球-平面比 AABB-平面快很多（1 dot vs 8 个 vertex 投影 + min/max）。

## 3.4 Hi-Z (Hierarchical Depth)

### 为什么不直接用 depth buffer

Depth buffer full-resolution（1920×1080）。一个大 bounding sphere 覆盖几百×几百 pixel，逐 pixel max 测试 = 几万次纹理读取。太慢。

Hi-Z 是 depth buffer 的 *max-reduced mipmap*，每级是上一级 2×2 块的最大深度：

```
Level 0: 1920×1080 (原始)
Level 1:  960×540
Level 2:  480×270
...
Level 10:    1×1 (全图最远 depth)
```

测试 *只读 1 个 pixel*（在合适的 level）。

### 为什么用 max（不是 bilinear）

```
Bilinear: 插值出"虚假"深度值 → 可能在不该剔的地方误剔 → false negative → bug
Max:      保留区域内最远的 fragment → 占用空间扩大但不缩 → 保守 → 永不 false negative
```

**深度纹理一般不用 bilinear 采样**。要么 nearest，要么 `VkSamplerReductionMode` 指定 min/max。

inverted-z 时改 min（"最远"在 inverted-z 里数值小）。

### Hi-Z 生成 compute shader

```glsl
ivec2 pos = ivec2(gl_GlobalInvocationID.xy) * 2;
float color00 = texelFetch(src, pos, 0).r;
float color01 = texelFetch(src, pos + ivec2(0,1), 0).r;
float color10 = texelFetch(src, pos + ivec2(1,0), 0).r;
float color11 = texelFetch(src, pos + ivec2(1,1), 0).r;
float result = max(max(max(color00, color01), color10), color11);
imageStore(dst, gl_GlobalInvocationID.xy, vec4(result, 0, 0, 0));
```

每级独立 dispatch + barrier。共 log2(width) 级，几毫秒级总开销。

## 3.5 Sphere → Screen-Space AABB

要在 Hi-Z 上查 occlusion，需要球在屏幕上覆盖哪片区域。

### 错误做法

简单投 sphere center 拿 (u,v) 后算 `radius_pixels = radius / depth * focal`：**错的**。3D sphere 投到 2D 是椭圆，不是圆。

### 正确做法

参考论文 *2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere*。从相机原点向 sphere 作切线，切点的屏幕坐标即极值。

```glsl
vec2 cx = vec2(C.x, -C.z);
vec2 vx = vec2(sqrt(dot(cx, cx) - r*r), r);
vec2 minx = mat2(vx.x, vx.y, -vx.y, vx.x) * cx;
vec2 maxx = mat2(vx.x, -vx.y, vx.y, vx.x) * cx;
// y 同理
aabb = vec4(minx.x/minx.y * P00, miny.x/miny.y * P11,
            maxx.x/maxx.y * P00, maxy.x/maxy.y * P11);
aabb = aabb.xwzy * vec4(0.5, -0.5, 0.5, -0.5) + 0.5;  // → UV space
```

边界情况：sphere 跨近裁剪面时直接返回 false（保守，不剔）。

## 3.6 选 Hi-Z Level

```glsl
float width = (aabb.z - aabb.x) * pyramid_size.x;
float height = (aabb.w - aabb.y) * pyramid_size.y;
float level = floor(log2(max(width, height)));
```

选的 level 让 AABB 在该级 *只占 1-2 pixel*。occlusion 测试就变成单 pixel lookup。

`floor` 偏小 level（高分辨率） — 保守，false positive 更少。

## 3.7 Occlusion Test

```glsl
float depth = textureLod(depth_pyramid, aabb_center, level).r;
float depth_sphere = z_near / (view_bounding_center.z - radius);
occlusion_visible = (depth_sphere <= depth);
```

`depth_sphere` = sphere 最近点的 NDC depth。`depth` = Hi-Z 该位置最远 fragment depth。

如果 sphere 最近点 ≤ Hi-Z 最远点 → sphere 至少部分在已画物体之前 → 可能可见 → 保留。

## 3.8 两阶段 occlusion 算法

### 算法步骤（Aaltonen & Haar 2015）

```
1. 用上一帧的 Hi-Z 做 culling → visible_list_1 + 没把握的 list_2
2. Indirect draw visible_list_1 → 部分 depth buffer
3. 更新 Hi-Z（用部分 depth 算 mip chain）
4. list_2 的物体用新 Hi-Z 重测 → visible_list_2（修正第一阶段误剔）
5. Indirect draw visible_list_2 → 完整 depth buffer
6. Final depth 给下一帧用
```

### 为什么 *两阶段* 而不是 depth pre-pass

| 方案 | 渲染次数 | 准确性 | 性能 |
|------|---------|--------|------|
| 无 occlusion | 1 次（全画） | n/a | 低（overdraw 大） |
| Depth pre-pass | 2 次（全场景画两遍） | 100% | 中 |
| **两阶段** | 1.x 次（visible + 修正） | 99%+ | 高 |

第二阶段只画 *第一阶段漏掉的少数 mesh*，比 pre-pass 便宜得多。

### 误判类型

| 类型 | 含义 | 处理 |
|------|------|------|
| False positive | stale depth 认为可见，实际本帧被挡 | 不修正（多画一帧，画面正确，性能小损） |
| **False negative** | stale depth 认为被挡，实际本帧可见 | **必须修正**（否则画面少东西，bug） |

第二阶段就是为了消除 false negative。False positive 不修正。

## 3.9 Indirect Drawing

通过所有 cull 测试的 mesh，compute shader 直接写 indirect draw command：

```glsl
draw_commands[draw_index].drawId = mesh_instance_index;
draw_commands[draw_index].taskCount = (mesh_draw.meshlet_count + 31) / 32;
draw_commands[draw_index].firstTask = mesh_draw.meshlet_offset / 32;
```

CPU 端：

```cpp
vkCmdDrawIndexedIndirectCount(cmd, draw_commands_buffer, 0,
                               draw_count_buffer, 0, max_draw_count, stride);
```

`draw_count_buffer` 也是 GPU 写的 → 完全 GPU 自治。

## 3.10 Mesh Shader vs Compute Culling 两条路径

### Mesh shader 路径（NV / EXT）

```
CPU: vkCmdDrawMeshTasksIndirectCountNV(...)
    ↓
GPU: Task shader (filter cull) → Mesh shader (emit verts/tris) → Fragment
```

优点：
- 一条管线全包，最快
- Task shader 内部 subgroup ballot 做 stream compaction，零 atomic 开销

缺点：
- NVIDIA 多年独占（Pascal+）
- AMD RDNA 2+、Intel Arc 才支持 KHR 扩展
- 移动 GPU 普遍不支持

### Compute culling + 传统 indirect draw 路径（cross-vendor）

```
CPU: vkCmdDispatch(culling) + vkCmdDrawIndexedIndirectCount(...)
    ↓
GPU: Compute shader (cull → 写 indirect draw commands) → 传统 vertex/fragment
```

优点：
- Cross-vendor，所有支持 Vulkan 1.1+ 的硬件都能跑
- 跟现有 vertex/fragment shader 路径兼容

缺点：
- 比 mesh shader 慢一些（多一次 indirect command 解析）
- 需要 *两个 pipeline 类型* 切换（compute + graphics）

### 工业实践：两条路都做

引擎里通常 *两个路径并存*，启动时按硬件能力选：

```cpp
if (device->supports_mesh_shader_ext()) {
    use_mesh_shader_path();
} else {
    use_compute_culling_path();
}
```

这是 fallback 设计。两条路径输出相同的画面，性能差 5-30%。

## 3.11 LX 现状对照

| 维度 | LX 现状 | 本调研需要 |
|------|---------|----------|
| Compute culling pipeline | 无 | compute shader + storage buffer 路径 |
| Hi-Z 生成 | 无 | mip chain + max-reduce compute |
| Sphere → AABB 投影 | 无 | shader 工具函数 |
| Two-phase rendering | 无 | frame graph 多 pass + 跨 pass 资源 |
| Indirect draw | 无 | `vkCmdDrawIndexedIndirectCount` 封装 |
| Mesh shader pipeline | 无 | 第三种 pipeline 类型（task+mesh+fragment） |
| Hardware fallback | 无 | mesh shader vs compute 路径选择 |

## 3.12 接下来读什么

- [04 Clustered deferred lighting](04-clustered-deferred-lighting.md) — Ch7 内容（G-buffer + light culling）
