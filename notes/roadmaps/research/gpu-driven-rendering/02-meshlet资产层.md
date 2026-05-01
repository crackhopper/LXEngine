# 02 · Meshlet 资产层

> 阅读前提：[01](01-gpu-driven-范式.md) 已经讲清 GPU-driven 范式。本文讨论资产侧的 meshlet 切分。

## 2.1 Meshlet 的概念

**Meshlet = mesh 的子集，包含一组 vertex（≤64）和一组 triangle（≤124）**。

```
原 mesh:                Meshlet 切分后:
┌─────────────┐        ┌──┬──┬──┬──┐
│             │        │M1│M2│M3│M4│
│   100k      │   →    ├──┼──┼──┼──┤
│   vertices  │        │M5│M6│M7│M8│
│             │        ├──┼──┼──┼──┤
└─────────────┘        │..│..│..│..│
                       └──┴──┴──┴──┘
                       ~1500 个 meshlet
```

Meshlet 是 GPU 上 *一个 thread group* 处理的最小单位，跟 GPU warp 大小（32/64）对齐。

## 2.2 数字约束的来源

| 数字 | 来源 |
|------|------|
| 64 vertices | NVIDIA mesh shader thread group 大小（一组 thread / vertex） |
| 124 triangles | "126 上限减 2 留给 primitive count" — Vulkan 规范要求每 meshlet 留点空间 |

不同 GPU 厂商微调，但量级都是 64 / 128 这一档。

## 2.3 Meshlet 自带元数据

每个 meshlet 不只是 vertex/triangle，还附带：

```cpp
struct Meshlet {
    u32 vertex_offset;     // 在共享 vertex 索引数组里的偏移
    u32 triangle_offset;   // 在共享 triangle 索引数组里的偏移
    u32 vertex_count;      // ≤ 64
    u32 triangle_count;    // ≤ 124
};

struct MeshletBounds {
    vec3 center;           // bounding sphere
    f32  radius;
    i8   cone_axis[3];     // back-face cull 用，s8 量化
    i8   cone_cutoff;      // sin(half_angle)，s8 量化
};
```

这些 metadata 让 GPU culling 可以 per-meshlet 操作。

## 2.4 关键设计选择

### Sphere vs AABB

- Sphere: vec4 = 16 bytes
- AABB: 24 bytes

Sphere 比 AABB *bounding 不紧*（~10-20% 多余空间），但：

- 内存省 33%
- Frustum cull 是 1 个 dot product vs AABB 的 8 个 vertex projection
- Occlusion cull 也更快

百万 meshlet 时这些差异叠加可观。

### Triangle index 用 u8

每个 triangle 用 3 个 index 表示（指向 meshlet 内部的 vertex）。max 64 vertex → index 范围 [0, 63] → u8 够。

| 类型 | 单 triangle 大小 | 124 triangle / meshlet |
|------|------------------|------------------------|
| u8 | 3 字节 | 372 字节 |
| u16 | 6 字节 | 744 字节 |
| u32 | 12 字节 | 1488 字节 |

**省 4 倍**。注意 u8 只在 meshlet 内部有效，全局 vertex index 仍是 u32。

### Vertex 数据压缩

| 字段 | 全精度 | 压缩 | 节省 |
|------|--------|------|------|
| position (vec3) | 12 字节 | 12 字节（不压缩） | 0 |
| normal (vec3) | 12 字节 | 3 字节（每维 s8） | 9 字节 |
| UV (vec2) | 8 字节 | 4 字节（每维 f16） | 4 字节 |
| tangent (vec4) | 16 字节 | 4 字节（quaternion 压缩） | 12 字节 |

**Position 不压缩** — 位置精度对几何正确性最敏感。其它字段可激进压缩。

#### Normal 压缩细节

```
(normal + 1.0) * 127.0  →  存 u8
读回时:  normal = (u8 / 127.0) - 1.0
```

精度 1/127 ≈ 0.79°，对法线足够（光照差异看不出）。

更高级方案：**octahedral encoding**（Ch7 G-buffer 也用），把 vec3 unit normal 投影到 vec2，精度更均匀，存 2 字节。Ch6 没用这个，Ch7 用了。LX 实施时建议统一走 octahedral。

#### Cone 压缩

```
cone_axis_s8[3]    // 每维 s8，4 字节（含 cutoff）
cone_cutoff_s8     // s8
```

省 12 字节 / meshlet。

## 2.5 meshoptimizer 库

NVIDIA 出的开源库（zeux 是作者），事实标准：

- `meshopt_buildMeshletsBound(...)` — 算 meshlet 数上限
- `meshopt_buildMeshlets(...)` — 真正切分
- `meshopt_computeMeshletBounds(...)` — 算 sphere + cone

使用约定：

```cpp
// 三个数组配套：
Array<meshopt_Meshlet>  meshlets;          // 描述每个 meshlet
Array<u32>              meshlet_vertices;  // meshlet → global vertex 映射
Array<u8>               meshlet_triangles; // meshlet 内 triangle indices

// 切分:
size_t count = meshopt_buildMeshlets(
    meshlets.data, meshlet_vertices.data, meshlet_triangles.data,
    indices, indices_count,
    vertices, vertices_count, sizeof(vec3),
    max_vertices = 64, max_triangles = 124,
    cone_weight = 0.0f);
```

`cone_weight` 是切分时的法线一致性偏好：

| cone_weight | 切分倾向 | 后果 |
|-------------|---------|------|
| 0.0 | 纯按 vertex 局部性切 | meshlet 数少，但 cone 宽（cull 率低） |
| 0.5 | 平衡 | 折中 |
| 1.0 | 强烈倾向法线一致 | meshlet 数多，cull 率高 |

默认 0.0，但平面较多的场景（建筑 / 地形）可调到 0.25 - 0.5 提升 cull 率。

## 2.6 LX 现状对照

```cpp
// LX 当前 Mesh 类
class Mesh : public IRenderable {
    IGpuResourceSharedPtr m_vertexBuffer;
    IGpuResourceSharedPtr m_indexBuffer;
};
```

跟 meshlet 化的 gap：

| 维度 | LX 现状 | 需要 |
|------|---------|------|
| Mesh 切分 | 整 mesh 一块 | meshlet 切分 |
| Bounds | 没有 / 只有 mesh 级 | per-meshlet sphere + cone |
| Vertex 数据格式 | 全精度 vec3 / vec2 | 压缩 |
| Triangle index | u32 | meshlet 内 u8 |
| 资产 pipeline | 加载 OBJ/glTF → GPU | + meshoptimizer 切分 |
| meshoptimizer 依赖 | 无 | 加 CMake 依赖 |

## 2.7 接下来读什么

- [03 Culling pipeline](03-culling-pipeline.md) — 如何利用 meshlet 数据做 GPU culling
