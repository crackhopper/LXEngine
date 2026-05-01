# 03 · Mesh Shader Shadow 流水线

> 阅读前提：[02](02-cubemap-shadow.md) 已经讲清 cubemap array + layered rendering。本文展开 *用 mesh shader 渲染 shadow* 的 4 步流水线。
>
> 强相关：[gpu-driven-rendering 调研](../gpu-driven-rendering/README.md) 提供了 mesh shader / meshlet / indirect / compute culling 的完整基础。本文复用这些能力。

## 3.1 4 步流水线总览

```
Step 1: Per-light mesh culling (compute)
        ↓
        per_light_meshlet_instances[每光源的 meshlet 列表]

Step 2: Indirect draw 命令生成 (compute)
        ↓
        meshlet_draw_commands[N×6 个命令，每光源 6 个 cubemap face]

Step 3: 用 indirect mesh tasks 画 shadow (mesh shader)
        ↓
        layered cubemap shadow texture

Step 4: Lighting pass 采样 cubemap shadow
```

CPU 仅发 indirect 命令。

**这是 GPU-driven 范式在 shadow 维度的应用**。每一步都跟主渲染共用基础设施（compute pipeline / indirect / mesh shader / cubemap）。

## 3.2 Step 1：Per-Light Mesh Culling

### Dispatch 形态

每个 thread 对应一对 (mesh_instance, light)：

```glsl
uint light_index         = gl_GlobalInvocationID.x % active_lights;
uint mesh_instance_index = gl_GlobalInvocationID.x / active_lights;
```

**这是 2D 矩阵线性化**：每个 thread 是矩阵的一格。

### 测试逻辑

简单 sphere-sphere intersection：

```glsl
bool mesh_intersects_sphere = sphere_intersect(
    mesh_world_bounding_center, mesh_radius,
    light.world_position, light.radius);
```

如果交叉，把 mesh 的所有 meshlet 写入 `meshlet_instances[light_index][...]`。

### 半透明 mesh 跳过

```glsl
if (mesh_draw.flags & (DrawFlags_AlphaMask | DrawFlags_Transparent)) {
    return;  // shadow map 不写半透明
}
```

理由：shadow map 写半透明会让光线阻挡但又透光，物理上不合。半透明物体 shadow 通常需要专门技术（stochastic shadow / hair shadow），v1 跳过。

### 为什么不用 frustum culling

光源是 *点 + 球范围*，没有 frustum 概念。frustum + back-face culling 留到 task shader（per-meshlet 级）。

### 跟 gpu-driven-rendering 调研的复用

这一步跟 [`gpu-driven-rendering/03 culling-pipeline.md`](../gpu-driven-rendering/03-culling-pipeline.md) §3.3 frustum culling 复用 *bounding sphere + culling math*。区别：

- gpu-driven culling：sphere vs frustum
- shadow culling：sphere vs sphere（因为光源是球）

代码工具可共享。

## 3.3 Step 2：Indirect Command Generation

### 6 commands / light

每光源 6 个 cubemap face 各一条 indirect command：

```glsl
uint packed_light_index = (light_index & 0xffff) << 16;

meshlet_draw_commands[command_offset + 0] = uvec4(..., packed_light_index | 0);
meshlet_draw_commands[command_offset + 1] = uvec4(..., packed_light_index | 1);
// ... face 2-5
```

### Packing trick

```
packed = (light_index << 16) | face_index
         ↑                    ↑
         高 16 位 = light_id   低 4 位 = face_id (0..5)
```

把两个标识装进 *一个 uint*，传给 task / mesh shader 解包：

```glsl
uint light_index = packed >> 16;
uint face_index  = packed & 0xf;
```

GPU 上节省 register 和带宽的常见手法。

### 为什么不用 instanced indirect

理论上一条 command + `instance_count = 6` 能跑 6 face。但 *NV mesh shader 扩展不支持 multiview*，所以只能 6 条独立命令。

KHR mesh shader extension 成熟后这个开销可省掉（[02 §2.5](02-cubemap-shadow.md#25-multiview-rendering进一步优化)）。

## 3.4 Step 3：Mesh Shader Shadow 渲染

### Task Shader：3D culling

```glsl
// 1. cone culling (back-face)
accept = !coneCull(world_center, radius, cone_axis, cone_cutoff, light_pos);

// 2. sphere culling (light range)
if (accept) accept = sphere_intersect(world_center, radius, light_pos, light_radius);

// 3. cubemap face culling (only visible faces of THIS light)
if (accept) {
    uint visible_faces = get_cube_face_mask(light_pos, aabb_min, aabb_max);
    accept = (visible_faces & (1 << face_index)) != 0;
}
```

3 维 cull：back-face × light range × cubemap face。

`get_cube_face_mask` 通过 6 个 frustum 平面 dot product 测试，返回 6-bit mask 指示该 mesh 在哪几个 cubemap face 可见。

### 为什么必要

Step 1 输出的是 *per-light* 列表，没区分 face。一个 meshlet 可能只在 +X face 可见，但仍被列入 light 的 visible list。task shader 这步把"per-face 不可见"的 meshlet 进一步剔除。

### Mesh Shader：写 layered cubemap

```glsl
const int layer_index = int(CUBE_MAP_COUNT * light_index + face_index);

for (uint i = task_index; i < vertex_count; i += 32) {
    gl_MeshVerticesNV[i].gl_Position = view_projections[layer_index] * (model * vec4(position, 1));
}

// 写 layer index 给每个 primitive
gl_MeshPrimitivesNV[task_index +  0].gl_Layer = layer_index;
gl_MeshPrimitivesNV[task_index + 32].gl_Layer = layer_index;
gl_MeshPrimitivesNV[task_index + 64].gl_Layer = layer_index;
gl_MeshPrimitivesNV[task_index + 96].gl_Layer = layer_index;
```

**`gl_Layer` 是关键**：告诉 rasterizer 这个 primitive 写到 cubemap array 的哪一层。

写 4 次（offset 0/32/64/96）是 *bank conflict 优化* — GPU 的 32 thread per warp，分散 index 让多 warp 协同。

### 没有 fragment shader

shadow map 只写 depth，不需要 fragment shader。开 depth-only pipeline，rasterizer 自动写 depth attachment。

性能翻倍（fragment shading 完全跳过）。

## 3.5 Step 4：Shadow Sampling

```glsl
vec3 shadow_position_to_light = world_position - light.world_position;

float closest_depth = texture(
    cubemap_shadows_array,
    vec4(shadow_position_to_light, shadow_light_index)).r;

float current_depth = vector_to_depth_value(shadow_position_to_light, light.radius);

float shadow = current_depth - bias < closest_depth ? 1.0 : 0.0;
```

注意：

- `vec4(direction, light_index)` 是 cubemap array 寻址（[02 §2.3](02-cubemap-shadow.md#23-cubemap-array---n-光源的关键)）
- `vector_to_depth_value` 还原投影 depth 公式（[02 §2.7](02-cubemap-shadow.md#27-vector--depth-转换)）
- `bias` 防 shadow acne，值视场景调整（典型 0.001-0.005）

这是最终在 lighting pass 里 *per-fragment 调用一次的代码*。前面所有 culling + render 的工作就为了让这一行能查到正确的 depth。

## 3.6 跟 GPU-Driven 调研的复用矩阵

| 能力 | 来源 | 用法 |
|------|------|------|
| Meshlet 资产 | `gpu-driven REQ-A` | shadow culling 也用 meshlet bounds |
| Cone culling | `gpu-driven` 02-meshlet 资产层 | task shader 复用 |
| Frustum / sphere culling | `gpu-driven` 03-culling-pipeline | task shader 复用 |
| Compute pipeline | `async-compute REQ-A` | Step 1 + 2 用 |
| Mesh shader pipeline | `gpu-driven REQ-G` | Step 3 用 |
| Indirect drawing | `gpu-driven REQ-B` | Step 2 输出，Step 3 消费 |
| Cubemap face mask 工具 | **本调研新增** | Step 3 task shader |
| Vector → depth 公式 | **本调研新增** | Step 4 sampling |

**90% 复用 GPU-driven 的能力，10% 是 shadow 自己的**。这是为什么 shadow 跟 mesh shader 路径"原生兼容"。

## 3.7 整体数据流

```
CPU 每帧:
  vkCmdDispatch (per-light mesh culling)        ← Step 1
  vkCmdDispatch (indirect command generation)    ← Step 2
  vkCmdDrawMeshTasksIndirectCountNV (...)        ← Step 3
  vkCmdDispatch (lighting pass，含 shadow 采样)  ← Step 4

GPU 自动展开:
  Step 1 compute: N×M thread → per-light meshlet 列表
  Step 2 compute: 每光源写 6 个 indirect command
  Step 3:
    Task shader: 3 维 cull
    Mesh shader: 输出 vertex + primitive，gl_Layer 决定写到 cubemap array 哪层
    Rasterizer: 自动写 depth 到对应 layer
  Step 4 lighting: 读 cubemap array → vector_to_depth → 比较 → shadow
```

CPU 完全不参与 per-light 决策，是 GPU-driven 的标准形态。

## 3.8 Compute Culling Fallback

如果硬件不支持 mesh shader（移动 GPU / 老硬件），Step 3 改用 compute culling 路径：

```
Step 3 替代:
  Compute shader: 跟 task shader 等价的 cull
  Compute 写 indirect draw command
  vkCmdDrawIndexedIndirect 走传统 vertex + fragment（fragment 跳过）
```

代码差异主要在 Step 3，Step 1/2/4 一致。这是 [`gpu-driven-rendering/03 §3.10`](../gpu-driven-rendering/03-culling-pipeline.md#310-mesh-shader-vs-compute-culling-两条路径) 的两条路径在 shadow 维度的应用。

## 3.9 接下来读什么

- [04 Sparse Resources](04-sparse-resources.md) — Vulkan sparse residency 让 256 光源 shadow 可行
