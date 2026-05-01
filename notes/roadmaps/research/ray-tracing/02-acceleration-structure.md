# 02 · Acceleration Structure

> 阅读前提：[01](01-ray-tracing-范式.md) 讲清 RT 范式 + BLAS/TLAS 概念。本文展开 *AS 的数据结构 + 两步构建 + 跨帧更新策略*。

## 2.1 BLAS（Bottom Level Acceleration Structure）

**包含 *单个 mesh 的几何信息*，跟 mesh 同生命周期**。

### 输入数据

`VkAccelerationStructureGeometryKHR` 描述 *每个* mesh 的几何：

```cpp
VkAccelerationStructureGeometryKHR geom = {};
geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
geom.flags = mesh.is_opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;

geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
geom.geometry.triangles.vertexData.deviceAddress = pos_buffer_address;
geom.geometry.triangles.vertexStride = sizeof(float) * 3;
geom.geometry.triangles.maxVertex = vertex_count;
geom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
geom.geometry.triangles.indexData.deviceAddress = idx_buffer_address;
```

跟 raster 的 vertex/index buffer *同一份数据*——**RT 不需要重新上传几何**，只是给 BVH builder 一个 device address 让它读。

### Geometry 类型

`geometryType` 只有 3 种：

| 类型 | 用途 |
|------|------|
| `TRIANGLES_KHR` | 99% 情况，标准三角形 mesh |
| `AABBS_KHR` | 自定义几何（球 / SDF / particle）→ 需要 intersection shader |
| `INSTANCES_KHR` | *仅 TLAS 用*，引用 BLAS 列表 |

### Opaque flag 的意义

`VK_GEOMETRY_OPAQUE_BIT_KHR` 让 BVH 标记此 geometry 为 *不透明*——ray 命中时跳过 any-hit shader 直接走 closest-hit。**对透明物体（玻璃 / 树叶）必须不设此 flag**，否则 alpha test 走不到。

## 2.2 BLAS 两步构建

构建过程 *无法一步完成*——必须先 query 大小，分配 buffer，再真正 build。

### Step 1：Query Size

```cpp
VkAccelerationStructureBuildGeometryInfoKHR as_info = {};
as_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
as_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
as_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
              | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
as_info.geometryCount = N;
as_info.pGeometries = geometries;

VkAccelerationStructureBuildSizesInfoKHR size_info = {};
vkGetAccelerationStructureBuildSizesKHR(device,
    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
    &as_info, max_primitive_counts, &size_info);

// size_info 包含三个值：
// - accelerationStructureSize：AS 数据本身需要的 device memory
// - buildScratchSize：本次构建时需要的 scratch memory
// - updateScratchSize：将来 update 时需要的 scratch memory
```

`flags` 里的两个常用：

- `PREFER_FAST_TRACE`：BVH 构建时多花时间换 *遍历更快*（典型选择）
- `PREFER_FAST_BUILD`：BVH 构建快，但遍历慢（动态场景每帧 rebuild 用）
- `ALLOW_UPDATE`：允许将来 *refit*（比 rebuild 快但精度降低）
- `ALLOW_COMPACTION`：允许构建后压缩（节省 ~50% 内存，但需要二次操作）

### Step 2：分配 Buffer + Build

```cpp
// 1. AS data buffer
BufferDesc as_buf = {
    .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
    .size = size_info.accelerationStructureSize,
};
auto as_buffer = create_buffer(as_buf);

// 2. Scratch buffer
BufferDesc scratch_buf = {
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
           | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR,
    .size = size_info.buildScratchSize,
};
auto scratch_buffer = create_buffer(scratch_buf);

// 3. AS handle
VkAccelerationStructureCreateInfoKHR create = {};
create.buffer = as_buffer.vk_buffer;
create.size = size_info.accelerationStructureSize;
create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
vkCreateAccelerationStructureKHR(device, &create, nullptr, &blas);

// 4. Build command
as_info.dstAccelerationStructure = blas;
as_info.scratchData.deviceAddress = scratch_buffer.device_address;
vkCmdBuildAccelerationStructuresKHR(cmd, 1, &as_info, &build_ranges);
```

### 关键 Buffer Usage Flag

| Flag | 用于 |
|------|------|
| `VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR` | AS 数据本身的 buffer |
| `VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR` | TLAS 的 instance buffer |
| `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR` | scratch buffer + 任何要给 RT 读的 vertex/index buffer |
| `VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR` | SBT |

**RT 强制依赖 buffer device address**：BVH builder + RT shader 都用 64-bit GPU 虚拟地址直接读 buffer，不走 descriptor set。Vulkan extension `VK_KHR_buffer_device_address` 必须启用。

## 2.3 TLAS（Top Level Acceleration Structure）

**包含 *BLAS instance 列表 + per-instance transform + shading 信息*，每帧重建**。

### Instance 数据结构

```cpp
VkAccelerationStructureInstanceKHR instance = {};
// 4×3 行主序 transform（最后一行隐含 0,0,0,1）
instance.transform.matrix = ... ; // mat3x4

instance.instanceCustomIndex = mesh_id;       // 24-bit，hit shader 用
instance.mask = 0xFF;                         // 8-bit，跟 ray cullMask 做 AND
instance.instanceShaderBindingTableRecordOffset = sbt_offset;  // 24-bit
instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
instance.accelerationStructureReference = blas_device_address;  // 64-bit
```

**4 个关键字段**：

- `instanceCustomIndex`：hit shader 里用 `gl_InstanceCustomIndexEXT` 拿到，定位 mesh 元数据（material / transform / albedo handle）
- `mask`：per-instance 8-bit 掩码，shadow ray 可用 `cullMask = 0x01` 来跳过装饰物
- `sbtRecordOffset`：选 SBT 哪一栏的 closest-hit（金属 / 玻璃 / 透明）
- `accelerationStructureReference`：BLAS 的 device address

### TLAS 构建跟 BLAS 区别

```cpp
VkAccelerationStructureGeometryKHR tlas_geom = {};
tlas_geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;  // ← 不是 triangles
tlas_geom.geometry.instances.arrayOfPointers = false;
tlas_geom.geometry.instances.data.deviceAddress = instance_buffer_address;

VkAccelerationStructureBuildGeometryInfoKHR as_info = {};
as_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;  // ← TOP_LEVEL
// ...其他构建过程同 BLAS
```

### BLAS 和 TLAS 必须分两次 submit

```cpp
// Submit 1: build all BLAS
vkCmdBuildAccelerationStructuresKHR(cmd, blas_count, blas_infos, blas_ranges);
gpu.submit_immediate(cmd);

// Submit 2: build TLAS (依赖 BLAS 已完成)
vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlas_info, &tlas_ranges);
gpu.submit_immediate(cmd);
```

**原因**：TLAS 依赖 BLAS device address 已经稳定，不能在同一 submit 里。但是 *多个 BLAS 之间可以并行 build*。

## 2.4 跨帧更新策略

| 场景 | 推荐策略 | 成本 |
|------|---------|------|
| 静态 mesh（建筑、地形） | BLAS 加载时构建 1 次，永不更新 | 0/帧 |
| 角色（骨骼动画） | BLAS *每帧 refit*（用 update mode） | 中 |
| 形变 mesh（cloth / morph target） | BLAS *每帧 rebuild*（精度需要） | 高 |
| 物体移动（位移 / 旋转） | *不动 BLAS*，只更新 TLAS instance.transform | 0（仅 TLAS rebuild） |
| 添加 / 删除物体 | TLAS rebuild | <1 ms（几千 instance） |

**关键洞察**：80% 的场景动态都是 *transform 变化* → 只 rebuild TLAS。BLAS rebuild 只在骨骼 / 形变时必要，此时用 *refit*（`VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR`）：

```cpp
as_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
as_info.srcAccelerationStructure = blas;  // ← 旧 BLAS
as_info.dstAccelerationStructure = blas;  // ← in-place 更新
```

Refit 不重建 BVH 树结构，只更新叶子节点的三角形位置。比 rebuild 快 10 倍，但精度会 *渐进劣化*——典型工业做法是每 30-60 帧来一次 full rebuild。

## 2.5 Compaction（构建后压缩）

BLAS 构建完通常有 30-50% 浪费空间。`ALLOW_COMPACTION` flag 允许调用：

```cpp
vkCmdCopyAccelerationStructureKHR(cmd, &copy_info);
copy_info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
```

得到压缩版 BLAS（典型节省 50% 内存）。但需要 *二次 submit*——查 `VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR` 拿压缩后大小，再 copy。

工业引擎对静态 BLAS *几乎都做 compaction*；动态 BLAS 不做（rebuild 频繁，浪费）。

## 2.6 内存量级

```
1 个 mesh BLAS（10 万三角形）≈ 5-10 MB（构建后）→ 2-4 MB（compacted）
1 个场景 TLAS（5000 instance）≈ 1-2 MB
1 个 mesh BLAS scratch ≈ 1-3 MB（peak）
```

大场景 AS 总量：

```
1000 unique mesh × 5 MB = 5 GB（不 compaction）
                       → 2 GB（compaction 后）
+ TLAS：~2 MB（实例数不影响 BLAS 总量）
```

**这是 RT 的真实内存代价**。8 GB VRAM 的 GPU 上，AS 可能吃掉 30%。

## 2.7 跨 Frame 资源管理

AS 是跨帧资源，跟 frame graph 协作：

- **静态 BLAS**：永久 resource，frame graph 永不 retire
- **动态 BLAS**（refit）：每帧 *update* 而不 *retire*——双 buffer 不需要
- **TLAS**：每帧 rebuild，需要 *双 buffer*（一份给当前帧 build，一份给上一帧 GPU 在用）

跟 multi-threading/08 的 RetirePoint 模型对接：

```
TLAS_current  ──→ [GPU 在用]
TLAS_next     ──→ [CPU 在 build] ──→ next frame retire 旧的
```

详见 [03 RT 管线与 Ray Query](03-RT管线与ray-query.md) 的资源调度部分。

## 2.8 关键性能数字（RTX 30/40 量级）

| 操作 | 典型时间 |
|------|---------|
| BLAS build（10 万三角形） | 0.5-2 ms |
| BLAS refit（10 万三角形） | 0.05-0.2 ms |
| BLAS compaction copy | 0.1-0.3 ms |
| TLAS build（5000 instance） | 0.3-0.8 ms |
| 单 ray traverse（典型场景） | <1 μs（硬件 RT core） |

预算参考：60 FPS 的 16.6 ms 帧里，AS 相关工作 *上限 1-2 ms*；超过就吃掉关键预算。

## 2.9 LX 现状

LX 完全没有 AS 概念：

- ❌ Mesh 加载完直接给 raster 用，不构建 BLAS
- ❌ 没有 buffer device address 路径
- ❌ 没有 AS handle / lifetime 管理
- ❌ 没有 compaction 支持
- ❌ 没有 TLAS 跨帧调度

REQ-A 必须把 *mesh asset → BLAS* 路径打通，REQ-B 必须建 TLAS 调度。

## 2.10 接下来读什么

- [03 RT 管线与 Ray Query](03-RT管线与ray-query.md) — RT pipeline + SBT + payload **vs** ray query 调用范式
