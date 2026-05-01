# 02 · Cubemap Shadow

> 阅读前提：[01](01-shadow技术全景.md) 已经讲清 shadow mapping 选择。本文展开 *point light 的 cubemap 数据结构*。

## 2.1 为什么 point light 需要 cubemap

Directional light（如太阳）只往一个方向照 → 单张 2D shadow map 足够。

Point light（灯泡）向 *所有方向* 发光 → 需要 6 张 shadow map（六面体的 6 个面）：

```
       +Y (上)
        ┌───┐
   -X   │   │   +X
   ┌───┐│ +Z│┌───┐
   │   ││(前)││   │
   └───┘└───┘└───┘
        ┌───┐
        │ -Z│  (后)
        └───┘
        ┌───┐
        │ -Y│  (下)
        └───┘
```

GPU 原生支持 cubemap 采样：给一个 3D 方向向量，硬件自动选哪一面采样并双线性插值。

## 2.2 Cubemap 寻址

GLSL 采样：

```glsl
texture(samplerCube, vec3 direction);
```

GPU 内部：

1. `direction` 找 *绝对值最大的分量* → 决定主轴（face）
2. 另两个分量除以主轴 → UV 坐标 [-1, 1]，归一化到 [0, 1]
3. 从对应 face 采样

写时需要更复杂的设置（layered rendering，见 §2.4）。

## 2.3 Cubemap Array：N 光源的关键

```
Cubemap：     6 layers per cubemap
                 layer 0 = +X
                 layer 1 = -X
                 ...
                 layer 5 = -Z

Cubemap Array：N 个 cubemap 拼成
                 cubemap 0 (light 0): layers 0-5
                 cubemap 1 (light 1): layers 6-11
                 ...
                 cubemap N-1:         layers (N-1)*6 .. N*6-1
```

**只用 1 个 image 资源**容纳全部 256 光源的 shadow。读时通过 `vec4(direction, layer_index)` 寻址：

```glsl
texture(samplerCubeArray, vec4(direction, light_index));
```

GPU 自动算 `cubemap_index = light_index`、`face_within_cubemap = direction → face`。

**这是为什么 cubemap array 比 6N 个独立 texture 便宜**：

- descriptor 集合小（一个 binding 而不是 6N 个）
- 内存连续，cache friendly
- 单一资源，barrier / sync 简单

## 2.4 Layered Rendering

### 写 cubemap array 的两种方式

#### 方式 A：传统方式 — 6 个独立 render pass

```
for face in 0..6:
    bind framebuffer with face_view
    render scene
```

每光源 6 个 render pass × N 光源 = 6N 个 pass。CPU + driver 开销爆炸。

#### 方式 B：Layered Rendering — 1 个 render pass

通过 **`gl_Layer` 输出**，让一个 vertex / primitive 决定写到 cubemap array 的哪一层：

```glsl
gl_MeshPrimitivesNV[i].gl_Layer = light_index * 6 + face_index;
```

shader 决定每个 primitive 的目标 layer。Rasterizer 自动写到 framebuffer 对应 attachment 层。

**前提**：framebuffer 创建时必须用 `VkImageView` 覆盖整个 cubemap array 而不是单个 face。

### 性能差异

| 维度 | 6N 个 pass | Layered rendering |
|------|-----------|-------------------|
| Render pass 数 | 6N | 1 |
| Driver overhead | 高 | 低 |
| Mesh shader 适配 | ❌ | ✅ |
| 适合多光源 | ❌ | ✅ |

Mesh shader 路径几乎只能走 layered rendering，因为 mesh shader 的输出粒度是 primitive，per-primitive 决定 layer 是天然形态。

## 2.5 Multiview Rendering：进一步优化

正常情况下，*一个 vertex 输出到一个 layer*。如果一个 vertex 要在 6 个 layer 都画（cubemap shadow 的常见情况），需要写 6 次。

**Multiview rendering** 让 *一个 vertex 自动 fan-out 到多个 layer*：

```
shader 计算一次 → GPU 自动复制到 view_mask 指定的所有 layer
```

`VK_KHR_multiview` 是常见扩展，VR 立体渲染（左右眼）的标准技术。

### 在 mesh shader 上的局限

书里指出：*NV mesh shader 扩展不支持 multiview*。所以 Ch8 选了 *6 个独立 indirect command* 的方案，而不是 1 个带 multiview 的命令。

KHR mesh shader extension 之后会支持，届时 Ch8 的 6× command 开销可以省掉。

### LX 启示

`VK_EXT_mesh_shader`（KHR 的实际名字）+ multiview 是未来。但 v1 实施时按 *6 commands per light* 走，跟 Ch8 一致。等扩展成熟再切。

## 2.6 写 Cubemap 的几个细节

### View / Projection 矩阵

每个 cubemap face 一组 view-projection 矩阵，全部存 GPU buffer：

```
view_projections[layer_index]   // light_index * 6 + face_index
```

mesh shader 用 `layer_index` 索引拿正确的矩阵。

### Face 的标准方向

按 Vulkan / OpenGL 约定（书里用的）：

| Face | View direction | Up direction |
|------|---------------|---------------|
| +X | (1, 0, 0) | (0, -1, 0) |
| -X | (-1, 0, 0) | (0, -1, 0) |
| +Y | (0, 1, 0) | (0, 0, 1) |
| -Y | (0, -1, 0) | (0, 0, -1) |
| +Z | (0, 0, 1) | (0, -1, 0) |
| -Z | (0, 0, -1) | (0, -1, 0) |

注意 Y 方向 up 是负的 — Vulkan / D3D 风格。OpenGL 风格相反。

## 2.7 Vector → Depth 转换

shadow 测试时需要把 *3D 方向向量* 转回 *NDC depth* 跟 cubemap 上存的值比较：

```glsl
float vector_to_depth_value(vec3 Vec, float radius) {
    float LocalZcomp = max(abs(Vec.x), max(abs(Vec.y), abs(Vec.z)));
    const float f = radius;
    const float n = 0.01f;
    return -(f / (n - f) - (n * f) / (n - f) / LocalZcomp);
}
```

**逐步解析**：

1. `LocalZcomp = max(|x|, |y|, |z|)` — 取主轴方向上的距离（即"在 cubemap face 坐标系下的 z"）
2. `f`、`n` — far / near plane（这里 near=0.01, far=light radius）
3. 公式来自 perspective projection 矩阵的 z 分量公式：
   ```
   depth_NDC = (f * z + n * f) / (z * (n - f))
   ```
   重排后就是上面的形式

这一步是 cubemap shadow 比 2D shadow 复杂的关键 — 必须 *还原投影矩阵的 depth 公式*。

## 2.8 接下来读什么

- [03 Mesh Shader Shadow](03-mesh-shader-shadow.md) — 4 步阴影渲染流水线
