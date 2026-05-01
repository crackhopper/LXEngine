# 04 · Clustered Deferred Lighting (Ch7)

> 阅读前提：[01](01-gpu-driven-范式.md) ~ [03](03-culling-pipeline.md) 讲完了 Ch6（GPU-driven）。本文转向 Ch7 — clustered deferred rendering，跟 GPU-driven 正交但实施时机重合。

## 4.1 Ch7 的两个独立主题

Ch7 处理两件事，两者独立但通常一起做：

- **Deferred rendering 路径**：G-buffer + 光照分离的渲染方式
- **Clustered light culling**：高效 light culling 算法

clustered lighting 算法 *也能用在 forward+ 上*，不强依赖 deferred。Ch7 选 deferred 是因为它跟其它后期效果（TAA、SSR）协同好。

## 4.2 Forward vs Deferred 历史 + tradeoffs

### 历史脉络

| 时期 | 主流 | 关键事件 |
|------|------|---------|
| 2000s 早期 | Forward | 4-8 光源上限 |
| 2007-2010 | Deferred | Stalker / CryEngine 3 推开市场 |
| 2012 | Forward+ 复活 | AMD Leo demo + compute culling |
| 2014+ | Clustered | Just Cause 3 / Chalmers paper |
| 2017 | 1D bin + 2D tile | Activision SIGGRAPH（Ch7 算法） |

### Forward 的特点

| 优点 | 缺点 |
|------|------|
| 材质灵活（任意 shader） | 复杂度 N×L（物体×光源） |
| 半透明天然支持 | shader 复杂，register pressure 高 |
| MSAA 自然 | 大场景需要 depth pre-pass |
| 内存带宽低 | early-Z 容易失效 |

### Deferred 的特点

| 优点 | 缺点 |
|------|------|
| 复杂度 N+L（解耦） | 高内存带宽（G-buffer） |
| shader 简单 | 半透明必须 forward 单走 |
| 不需要 depth pre-pass | MSAA 需要 per-sample G-buffer |
| | 材质参数必须装进 G-buffer |
| | normal 精度因压缩损失 |

### Forward+（Forward + tile-based light culling）

```
1. Depth pre-pass（搞定 early-Z）
2. Compute culling 算 per-tile light list
3. Forward shading 用 per-tile light list（不再遍历全部光源）
```

复杂度变 N×L'，其中 L' << L（per-tile 平均光源数，通常 5-20）。

### 现状

两条路都活，按项目偏好选。

| 偏好 | 选 |
|------|-----|
| 复杂材质 / 半透明多 / 移动端 | Forward+ |
| 大量物体 / 简单 PBR / 桌面端 | Deferred |
| 工业引擎（兼顾） | 两套都做，按场景切 |

## 4.3 Tiles vs Clusters 比较

无论 forward+ 还是 deferred，都要解决"哪些光源影响这个 fragment"。

### 2D Tiles

```
+----+----+----+
| L1 |L1L2|    |
| L3 |    |    |    每格记录"哪些光源覆盖我"
+----+----+----+    Render 时 fragment 查自己 tile，迭代该 tile 光源列表
|    |    |    |
+----+----+----+
```

需要 depth pre-pass 拿到 tile 的 [zmin, zmax]。深度差大时 tile 内会有"无关光源"被错误保留（depth discontinuity）。

### 3D Clusters

```
深度方向再切几层：
+----+----+
| C1 | C2 |   z=[0..10]
+----+----+
+----+----+
| C3 | C4 |   z=[10..20]
+----+----+
```

不需要 depth pre-pass，AABB 投影到 cluster 即可。但 *3D 网格内存爆炸*：16×9×24 cluster × per-light bitfield = 几 MB。

### Activision 混合方案（Ch7 选这个）

**洞察**：3D cluster 内存问题来自 X×Y×Z 笛卡尔积。让 **Z 维度独立** 不进 X×Y 笛卡尔积，内存大幅降。

```
1. 1D Depth Bins（Z 方向独立）：
   把 light 按 z 排序，分 bin。
   每个 bin 只记 [min_light_id, max_light_id]，2 个 u16 = 4 字节。

2. 2D Screen Tiles（XY 平面）：
   每个 tile 一个 bitfield，bit i 表示 light i 影响本 tile。

3. Render 时：
   - 用 fragment 的 z 查 bin → 拿 [min, max] 范围
   - 用 (x, y) 找 tile → 读 bitfield
   - 在 [min, max] 范围内逐个查 bitfield bit
```

### 内存对比

假设 16×9 tile, 24 z bin, 256 lights:

| 方案 | 内存 |
|------|------|
| 3D cluster | 16×9×24 × ceil(256/32) × 4 ≈ 110 KB |
| Activision 混合 | 16×9 × ceil(256/32) × 4 + 24 × 4 ≈ 4.7 KB |

**省 20+ 倍**。这是为什么这个算法成了 state of the art。

## 4.4 G-Buffer 实现

### Render target 布局

```
Layout 0: albedo (RGBA8)        — 颜色
Layout 1: normal (RG8)          — octahedral 编码法线
Layout 2: ORM (RGBA8)           — occlusion + roughness + metalness
Layout 3: emissive (RGBA8)      — 自发光
Depth: 标准 depth buffer
```

总 1920×1080 × 5 × 4 字节 ≈ 41 MB。

### 优化方向

把 normal 拆进其它 RT 的空闲通道，省一个 RT：

```
RT0 RGBA8: r, g, b, normal_1
RT1 RGBA8: normal_2, roughness, metalness, occlusion
RT2 RGBA8: emissive
```

### VK_KHR_dynamic_rendering

老 API 需要：
1. 创建 VkRenderPass（attachment 配置）
2. 创建 VkFramebuffer（绑 image view）
3. vkCmdBeginRenderPass / EndRenderPass

每次 attachment 变化要重建前两步。

新 API（Vulkan 1.3 core）：
1. Pipeline 创建时 inline 声明 attachment formats
2. vkCmdBeginRenderingKHR 直接传 image view
3. vkCmdEndRenderingKHR

**不再需要 VkRenderPass / VkFramebuffer 对象**。简化大幅。

| 维度 | 老 API | 动态 rendering |
|------|--------|----------------|
| 提前对象 | RenderPass + Framebuffer | 无 |
| 灵活性 | 低（attachment 集合固定） | 高（每帧可变） |
| 代码复杂度 | 高 | 低 |

LX 启示：[REQ-034](../../../requirements/034-render-target-desc-and-target.md) 的 RenderTarget 拆分后，binding 端可以直接用 dynamic rendering 实现，省掉 framebuffer 对象池。

## 4.5 Octahedral Normal Encoding

### 问题

Normal 占 vec3 = 12 字节。G-buffer 里太贵。

简单方案 `(x, y) → z = sqrt(1 - x² - y²)` 精度不均匀，且无法表达 z<0。

### 算法

```
3D 单位球 → 投影到 8 面体 (octahedron) → 展开成 2D 正方形 [-1,1]²
```

```glsl
vec2 octahedral_encode(vec3 n) {
    vec2 p = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));   // 投到 octahedron
    return (n.z < 0.0)
         ? (1.0 - abs(p.yx)) * sign_not_zero(p)         // 下半球折叠
         : p;                                            // 上半球直接
}
```

### 精度对照

| 存储 | 误差 |
|------|------|
| float vec2（4 字节） | ~0.0001° |
| half vec2（2 字节） | ~0.01° |
| **8-bit vec2（2 字节）** | ~1° |

8-bit 对 normal 够用，省 6 倍空间。G-buffer 标配。

## 4.6 Activision 算法详解

### 三个数据结构

```cpp
// 1. 排序后的光源（CPU 算，GPU 读）
struct SortedLight {
    u16 light_index;        // 指向原始 lights[]
    f32 projected_z;        // 球心 z（NDC）
    f32 projected_z_min;    // 球最近点
    f32 projected_z_max;    // 球最远点
};
SortedLight sorted_lights[N];   // 按 z 升序

// 2. Depth bins
struct Bin {
    u16 min_light_id;       // 最小覆盖本 bin 的 light id
    u16 max_light_id;       // 最大
};
Bin bins[NUM_BINS];         // 16-24 bins

// 3. Screen tile bitfields
u32 tile_bitfields[NUM_TILES_X * NUM_TILES_Y * (N + 31) / 32];
```

### 为什么 bin 只存 [min, max]

**关键洞察**：sorted_lights 按 z 排序后，覆盖某 z 范围的 lights 在数组里 *连续*。

所以 bin 不需要列出所有覆盖它的 light id，只需 *该范围的 id 最小最大值*。中间的 light 都自动属于。

代价：可能误包含一些 *id 在区间内但 z 不覆盖此 bin* 的光源。但 tile bitfield 二次过滤掉。

### CPU 步骤（每帧）

```cpp
// Step 1: 算 light view-space z 范围
for each light:
    compute projected_z, projected_z_min, projected_z_max

// Step 2: 排序（只排索引，不动 lights[]）
qsort(sorted_lights, N, ..., compare_by_z);

// Step 3: 算 light 在屏幕上的 AABB
for each light:
    project 8 corners of light AABB → clip space
    compute 2D screen-space AABB

// Step 4: 设置 tile bitfield
for each light i:
    for tiles in [first_tile, last_tile]:
        tile_bitfield[tile_idx + i/32] |= (1 << (i%32));
```

### GPU 步骤（per fragment）

```glsl
// 1. 算 fragment 的 z 归一化 → bin 索引
float linear_z = (-pos_camera.z - z_near) / (z_far - z_near);
int bin_index = int(linear_z / BIN_WIDTH);

// 2. 读 bin 的 [min, max]
uint bin_value = bins[bin_index];
uint min_light_id = bin_value & 0xFFFF;
uint max_light_id = (bin_value >> 16) & 0xFFFF;

// 3. 算 tile 索引
uvec2 tile = gl_GlobalInvocationID.xy / TILE_SIZE;
uint address = tile.y * stride + tile.x;

// 4. 在 [min, max] 范围内迭代 + 检查 tile bitfield
for (uint light_id = min_light_id; light_id <= max_light_id; ++light_id) {
    if ((tiles[address + light_id / 32] & (1 << (light_id % 32))) != 0) {
        uint global_idx = light_indices[light_id];
        accumulate(lights[global_idx]);
    }
}
```

平均 per-fragment 测试 ~5-20 个 light（vs 朴素方案的 256），**50-100× 加速**。

## 4.7 Memory 总账

256 光源、120×68 tile、24 z bin:

| 结构 | 内存 |
|------|------|
| sorted_lights[256] | ~3 KB |
| bins[24] | 96 B |
| tile_bitfields[120×68×8] | ~256 KB |
| **总计** | ~260 KB |

每帧重新构建。这个开销可接受。

## 4.8 LX 现状对照

| 维度 | LX 现状 | 本章需要 |
|------|---------|---------|
| G-buffer 多 RT 渲染 | 仅 swapchain 单 RT | 4 RT + depth |
| Octahedral encoding | 无 | shader 工具 |
| `VK_KHR_dynamic_rendering` | 无（用老 API） | 强烈推荐切换 |
| Light AABB 投影 | 无 | math 工具 |
| Light depth sort | 无 | 每帧 CPU sort |
| Tile bitfield | 无 | GPU buffer + 上传协议 |
| Z bins | 无 | 同上 |
| Compute light culling | 无 | 等 compute pipeline |

## 4.9 跟 LX 现有 REQ 的关系

| REQ | 跟本章关系 |
|-----|----------|
| [REQ-119 G-Buffer / 延迟渲染](../../main-roadmap/phase-1-rendering-depth.md#req-119--g-buffer--延迟渲染路径) | 直接对应本章 G-buffer 部分 |
| [REQ-109 PointLight + SpotLight + 多光源合同](../../main-roadmap/phase-1-rendering-depth.md#req-109--pointlight--spotlight--统一多光源合同) | 本章为它提供"如何高效处理"的答案 |
| [REQ-101 HDR scene color target](../../main-roadmap/phase-1-rendering-depth.md#req-101--hdr-scene-color-target) | offscreen RT，dynamic rendering 简化它 |
| [REQ-103 Shadow pass](../../main-roadmap/phase-1-rendering-depth.md#req-103--shadow-pass--depth-only-pipeline) | depth-only pipeline，跟 G-buffer 协同 |

**重要**：REQ-109 当前只说"统一多光源合同 + N=8 上限"，没说 *如何 culling*。本章给出的答案应该在演进路径里补进 REQ-109，或新立 REQ-F（详见 [06](06-演进路径.md)）。

## 4.10 接下来读什么

- [05 LX 当前状态对照](05-LX当前状态对照.md) — Ch6 + Ch7 综合 gap 分析
