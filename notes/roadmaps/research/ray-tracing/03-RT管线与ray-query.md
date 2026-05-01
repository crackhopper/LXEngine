# 03 · RT 管线与 Ray Query

> 阅读前提：[01](01-ray-tracing-范式.md) 讲清调用范式概览；[02](02-acceleration-structure.md) 讲清 AS 结构。本文展开 *两条调用路径的具体机制* + 何时用哪个。

## 3.1 两种范式

```
RT Pipeline 路径                          Ray Query 路径
═══════════════                          ════════════════
独立 raygen shader                        在 fragment / compute shader 里
  ↓                                       ↓
vkCmdTraceRaysKHR                         rayQueryInitializeEXT
  ↓                                       ↓
SBT dispatch:                             rayQueryProceedEXT (同步)
  - intersection                            ↓
  - any-hit                               rayQueryGetIntersectionTypeEXT
  - closest-hit                             ↓
  - miss                                  自己继续算
  ↓
payload 写回 raygen
```

|  | RT Pipeline | Ray Query |
|------|-----------|-----------|
| 调用位置 | 独立 raygen 入口 | *任何 shader 内随时调* |
| Shader 数 | ≥ 3（raygen + closest-hit + miss） | **0 个 RT shader** |
| SBT | 必需 | 不需要 |
| 递归 | 支持（最多 `maxRecursionDepth`） | 不支持 |
| 调度命令 | `vkCmdTraceRaysKHR` | 普通 `vkCmdDispatch` / draw |
| 求交时 hit shader | 可调（material 切换） | 不可调，自己手写后处理 |
| 适用 | 复杂 hit shader / 多材质 dispatch | 简单可见性查询 |
| 扩展 | `VK_KHR_ray_tracing_pipeline` | `VK_KHR_ray_query` |

## 3.2 Ray Query 路径（最简单）

### GLSL 语法

```glsl
#extension GL_EXT_ray_query : enable

void main() {
    rayQueryEXT q;
    rayQueryInitializeEXT(
        q,                                // query 对象
        as,                               // TLAS
        gl_RayFlagsOpaqueEXT
        | gl_RayFlagsTerminateOnFirstHitEXT,  // shadow ray 用法
        0xff,                             // cullMask
        world_pos,                        // origin
        0.001,                            // tmin
        light_dir,                        // direction
        light_distance);                  // tmax
    
    rayQueryProceedEXT(q);
    
    if (rayQueryGetIntersectionTypeEXT(q, true) ==
        gl_RayQueryCommittedIntersectionNoneEXT) {
        // ray miss → 可见
        shadow = 1.0;
    } else {
        shadow = 0.0;
    }
}
```

### 关键观察

- **同步调用**：`rayQueryProceedEXT` 像普通函数 return，不需要 callback shader
- **结果查询**：`rayQueryGetIntersectionTypeEXT` 拿命中类型，`rayQueryGetIntersectionTEXT` 拿命中距离
- **可控遍历**：`rayQueryProceedEXT` 在 `for` 里反复调可以遍历 *多个* candidate hit（用于 alpha test）
- **直接写 fragment / compute**：不需要专门 raygen，直接在 lighting shader 里加几行

### 性能特点

- *几乎零开销*：无 SBT 调度，无 shader 切换
- *局部寄存器压力*：query 对象在 shader 里持有，复杂 case 会增加 register usage
- *无递归*：所以 reflection / GI 这种"hit 后再 trace"的场景必须用 RT pipeline

### 何时用 ray query

✅ **适合**：

- Shadow（binary 可见性，4 行代码）
- AO（ambient occlusion，一束 ray，简单累积）
- 距离查询（"这点到最近 surface 多远？"）
- 简单 reflection（一次反弹，无 material 复杂度）

❌ **不适合**：

- DDGI / 光线追踪 GI（hit shader 要算复杂 lighting）
- 多材质 reflection（不同 BLAS 走不同 shader）
- Path tracing（需要递归）

## 3.3 RT Pipeline 路径（完整版）

### Shader Binding Table（SBT）

SBT 是一张表，*告诉 GPU 在每种事件下调哪个 shader*。布局：

```
SBT (单个 buffer，按 stage 分段)
┌──────────────────────────────────────┐
│ raygen 区   (1 条记录)                │
├──────────────────────────────────────┤
│ miss 区     (M 条记录)                │
│   miss[0]: 默认 sky                   │
│   miss[1]: shadow miss (无遮挡)       │
├──────────────────────────────────────┤
│ hit 区      (H × geometry 条记录)     │
│   hit[0]: 金属 closest-hit            │
│   hit[1]: 玻璃 closest-hit + any-hit │
│   hit[2]: SSS closest-hit             │
├──────────────────────────────────────┤
│ callable 区 (可选)                    │
└──────────────────────────────────────┘
```

每条记录是 *(shader handle, optional inline data)*。Shader handle 由 `vkGetRayTracingShaderGroupHandlesKHR` 拿。

### Shader Group 概念

不是 *单个 shader* 进 SBT，而是 *shader group*。3 种 group type：

| Group Type | 含义 | 包含 |
|-----------|------|------|
| `GENERAL` | 单 shader | raygen / miss / callable |
| `TRIANGLES_HIT_GROUP` | 三角形命中处理 | closest-hit + 可选 any-hit |
| `PROCEDURAL_HIT_GROUP` | 自定义几何 | intersection + closest-hit + 可选 any-hit |

`hit` 区的每条记录其实是 *一个 hit group*（最多 3 个 shader 打包）。

### 创建 RT Pipeline

```cpp
// 1. shader stage info（标准 VkPipelineShaderStageCreateInfo）
auto stages = {
    {raygen.module, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
    {miss.module,   VK_SHADER_STAGE_MISS_BIT_KHR},
    {hit.module,    VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
};

// 2. shader group info
VkRayTracingShaderGroupCreateInfoKHR groups[] = {
    // raygen group
    {.type = GENERAL_KHR,
     .generalShader = 0,        // index into stages
     .closestHitShader = VK_SHADER_UNUSED_KHR, ...},
    // miss group
    {.type = GENERAL_KHR,
     .generalShader = 1, ...},
    // hit group
    {.type = TRIANGLES_HIT_GROUP_KHR,
     .closestHitShader = 2,
     .anyHitShader = VK_SHADER_UNUSED_KHR, ...},
};

// 3. RT pipeline
VkRayTracingPipelineCreateInfoKHR rt_info = {};
rt_info.stageCount = 3;
rt_info.pStages = stages;
rt_info.groupCount = 3;
rt_info.pGroups = groups;
rt_info.maxPipelineRayRecursionDepth = 1;  // ← 关键！
rt_info.layout = pipeline_layout;
vkCreateRayTracingPipelinesKHR(device, ...);
```

`maxPipelineRayRecursionDepth` *硬件上限通常 31*，但实际 1-2（性能原因）。

### 拿 Shader Handles 填 SBT

```cpp
u32 handle_size = props.shaderGroupHandleSize;       // 32 byte (typical)
u32 handle_aligned = align(handle_size, props.shaderGroupBaseAlignment);

// 3 个 group 各拿 handle
std::vector<u8> handles(handle_size * 3);
vkGetRayTracingShaderGroupHandlesKHR(
    device, pipeline, 0, 3, handles.size(), handles.data());

// 拷到 SBT buffer
auto sbt_raygen = create_buffer({
    .usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
           | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR,
    .size = handle_aligned,
    .data = handles.data() + 0 * handle_size,
});
auto sbt_miss = ... // handles[1]
auto sbt_hit  = ... // handles[2]
```

### Dispatch RT

```cpp
VkStridedDeviceAddressRegionKHR raygen_region = {
    .deviceAddress = sbt_raygen.device_address,
    .stride = handle_aligned, .size = handle_aligned,
};
VkStridedDeviceAddressRegionKHR miss_region = ... ;
VkStridedDeviceAddressRegionKHR hit_region  = ... ;
VkStridedDeviceAddressRegionKHR callable_region = {};  // 空也要传

vkCmdTraceRaysKHR(cmd,
    &raygen_region, &miss_region, &hit_region, &callable_region,
    width, height, depth);
```

`width × height × depth` 是 *thread 数*——跟 compute dispatch 类似，每 thread 跑一次 raygen。

## 3.4 GLSL Shader 写法

### Ray generation

```glsl
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadEXT vec4 payload;
layout(binding = 0) uniform accelerationStructureEXT as;

void main() {
    vec3 origin = camera_pos;
    vec3 dir = compute_ray_dir(gl_LaunchIDEXT.xy, gl_LaunchSizeEXT.xy);
    
    payload = vec4(0);
    traceRayEXT(as,
        gl_RayFlagsOpaqueEXT,
        0xff,                  // cullMask
        0,                     // sbtRecordOffset
        0,                     // sbtRecordStride
        0,                     // missIndex
        origin, 0.001, dir, 100.0,
        0);                    // payload location
    
    imageStore(out_image, ivec2(gl_LaunchIDEXT.xy), payload);
}
```

### Closest-hit

```glsl
layout(location = 0) rayPayloadInEXT vec4 payload;
hitAttributeEXT vec2 barycentric;

void main() {
    // gl_InstanceCustomIndexEXT — 来自 TLAS instance.instanceCustomIndex
    // gl_GeometryIndexEXT       — 在 BLAS 内的 geometry 索引
    // gl_PrimitiveID            — 命中的三角形 index
    // gl_HitTEXT                — 命中距离
    // gl_WorldRayDirectionEXT   — ray 方向
    
    uint mesh_id = gl_InstanceCustomIndexEXT;
    vec3 albedo = sample_albedo(mesh_id, barycentric);
    payload = vec4(albedo * direct_lighting(...), 1);
}
```

### Miss

```glsl
layout(location = 0) rayPayloadInEXT vec4 payload;

void main() {
    payload = vec4(sky_color, 1);
}
```

## 3.5 Hit Shader 的"小型 vertex shader"问题

Hit shader 里需要重建命中三角形的 *vertex 属性*（位置 / 法线 / UV / tangent）：

```glsl
// 拿 mesh handle
MeshDraw mesh = mesh_draws[gl_InstanceCustomIndexEXT];

// 用 buffer device address 直接读 index buffer
int_array_t indices = int_array_t(mesh.index_buffer);
int i0 = indices[gl_PrimitiveID * 3 + 0].v;
int i1 = indices[gl_PrimitiveID * 3 + 1].v;
int i2 = indices[gl_PrimitiveID * 3 + 2].v;

// 读 vertex
vec3 p0 = read_position(mesh.position_buffer, i0);
// ... p1, p2

// 重心插值
float a = 1 - barycentric.x - barycentric.y;
float b = barycentric.x;
float c = barycentric.y;
vec3 world_pos = mesh.transform * (a * p0 + b * p1 + c * p2);
vec3 normal = normalize(mesh.normal_transform * (a * n0 + b * n1 + c * n2));
vec2 uv = a * uv0 + b * uv1 + c * uv2;
```

**重要意义**：

- hit shader = "迷你 vertex shader + fragment shader"
- 必须靠 `buffer_device_address` 读 mesh data
- 必须靠 `bindless texture` 采 albedo（不同 mesh 不同贴图）
- 材质代码必须维护 *两份*——raster vertex/fragment + RT closest-hit

工业引擎用 *shader graph + 两端代码生成* 解决这个问题。LX 的 shader system 没这能力——RT pipeline 落地需要 shader system 升级。

## 3.6 决策矩阵：用哪个

| 特性 | 推荐 | 理由 |
|------|------|------|
| Shadow | **ray query** | binary 可见性，4 行代码 |
| AO | **ray query** | 累积单 ray，简单 |
| 简单 mirror reflection | **ray query** | 单弹无递归 |
| DDGI | **RT pipeline** | hit shader 算 direct lighting + 上一帧 GI |
| Glossy reflection | **RT pipeline** | hit shader 算 material BRDF + shadow ray |
| Refraction | **RT pipeline** | 递归 + 多 ray |
| Path tracing | **RT pipeline** | 递归 + material dispatch |

**LX 路线**：必须 *两条都做*——REQ-B（ray query）+ REQ-C（RT pipeline）。

## 3.7 资源调度（跟 Frame Graph 对接）

### Pass kind 扩展

`FramePass` 需要新增 RT 类型：

```
PassKind:
- Graphics (现有)
- Compute  (async-compute REQ-A)
- RayTracing (本调研引入)
```

每个 RT pass 的 declared 资源：

- input: TLAS（+ 隐式所有 BLAS）
- input: 多张 texture（albedo / normal / roughness via bindless）
- output: 1+ image / buffer

### Barrier 推导

RT pass 的 barrier 处理 *跟 compute pass 几乎相同*：

- AS read：`VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR`
- AS write（build）：`VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR`
- Stage：`VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR`

frame-graph REQ-038（barrier 推导）必须 handle 这些 access mask 和 stage flag。

### 跨帧资源

- TLAS：双 buffer（current/next），跟 multi-threading/08 RetirePoint 对接
- Reflection / DDGI history texture：跟 temporal-techniques 共享 history 模型

## 3.8 LX 现状

| 机制 | LX | gap |
|------|----|-----|
| Ray query 路径 | 无 | 中（单点扩展，相对易加） |
| RT pipeline 类型 | 无 | 高（pipeline cache / shader system 都要扩展） |
| SBT 资源类型 | 无 | 中 |
| Shader stage 枚举（6 个新 stage） | 无 | 小 |
| `gl_InstanceCustomIndexEXT` 等 RT built-ins | 无 | 小 |
| Hit shader 重建 vertex 路径 | 无 | 中（依赖 buffer device address） |
| Bindless from RT shader | 无（bindless 整体也未完成） | 中 |
| RT pass kind in frame graph | 无 | 中 |

## 3.9 接下来读什么

- [04 RT Shadow](04-RT-shadow.md) — Ch13：ray query 的第一个 killer use case
