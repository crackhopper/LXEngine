# 01 · Ray Tracing 范式

> 阅读前提：理解 Vulkan / 现代 GPU 管线（vertex → fragment / compute pipeline）。
>
> 本文是 [02 Acceleration Structure](02-acceleration-structure.md)、[03 RT 管线与 Ray Query](03-RT管线与ray-query.md) 以及 04-06 三个特性章的 *共享基础*。

## 1.1 Hardware Ray Tracing 是什么

**Hardware ray tracing = GPU 内置 *ray-AS 求交单元*（NVIDIA RT Core / AMD Ray Accelerator / Intel Xe-RT），让 ray-triangle / ray-AABB 求交从 shader 软件实现降到 fixed-function 硬件**。

时间线：

| 年份 | 事件 |
|------|------|
| 2018 | NVIDIA RTX 20 系（Turing）首次硬件 RT，Vulkan 通过 `VK_NV_ray_tracing` 扩展 |
| 2020 | Khronos 标准化，`VK_KHR_acceleration_structure` + `VK_KHR_ray_tracing_pipeline` |
| 2021 | `VK_KHR_ray_query` 加入，让 fragment / compute shader 也能调 ray |
| 2022+ | DXR 1.1 / Vulkan ray query 成主流，Lumen / RTX Direct Illumination 等系统兴起 |

**关键意义**：硬件 RT *不是一个新 API，是 raster 的并列管线*——跟 graphics pipeline / compute pipeline 三足鼎立。

## 1.2 跟 Raster 的根本不同

| 维度 | Raster | Ray Tracing |
|------|--------|-------------|
| 入口 | vertex shader（每三角形 3 vertex） | ray generation shader（每 ray 1 thread） |
| 数据组织 | mesh + index buffer 直接喂 GPU | mesh 必须先打包成 *acceleration structure*（BVH） |
| 求交方式 | rasterization（屏幕空间扫描线） | ray-BVH 遍历（hardware accelerated） |
| Shader 调度 | 固定 vertex → fragment 流水 | *shader binding table* (SBT) 决定哪个 shader 在哪个事件触发 |
| 跨 shader 通信 | varying / interpolation（自动） | `payload` 变量 + location（显式） |
| 可恢复递归 | 无（fragment 不能再调 vertex） | 有（closest-hit 里可再 traceRay） |
| 拓扑约束 | 只能"几何 → 像素" | 反向："像素 → 几何"也行 |
| 内存数据流 | mesh 流入，pixel 流出 | mesh 静态（在 AS），ray 流式访问 |

最大的概念跨越：

- **Raster 是 "geometry → pixels"**（投影）
- **Ray Tracing 是 "ray → geometry"**（查询）

所以 RT 需要：

- *AS* 来加速 ray-geometry 求交（O(n) → O(log n)）
- *SBT* 来让 shader 之间互相调用（不是 fixed pipeline）
- *Payload* 来在不同 shader 之间传值（没有自动 interpolation）

## 1.3 BVH（Bounding Volume Hierarchy）

AS 的底层数据结构。把场景几何包到嵌套的 bounding box 里，让 ray 求交从 *逐三角形* 降到 *逐 box 剪枝*。

```
       [root AABB]
        /        \
   [child A]   [child B]
   /    \         /    \
 [A1]  [A2]   [B1]   [B2]
 ...    ...   ...    ...
 (叶子节点存三角形列表)
```

**Ray 遍历**：

```
1. 测 ray vs root AABB → 不命中直接返回 miss
2. 测 ray vs 两个 child AABB → 只进入命中的子树
3. 递归到叶子节点 → 测 ray vs 叶子里的三角形
```

复杂度：O(log n)，n 是三角形数。对 100 万三角形场景，遍历 ~20 层就找到命中。

GPU 硬件 RT core *专门加速这一过程*——AABB 求交 + 三角形求交都是 fixed-function 单元。

## 1.4 Vulkan 的两层 AS：BLAS + TLAS

Vulkan 把 AS 切两层：

```
TLAS (Top Level)
  ├── instance 0 → BLAS A + transform M0  + shading mask + SBT offset
  ├── instance 1 → BLAS A + transform M1  ← 同一 BLAS 多实例
  ├── instance 2 → BLAS B + transform M2
  └── ...

BLAS (Bottom Level)
  ├── BLAS A: triangles of mesh A (静态局部坐标)
  ├── BLAS B: triangles of mesh B
  └── ...
```

**为什么分两层**（关键设计）：

- **BLAS 一次构建多次复用**：100 棵同样的树共享 1 个 BLAS
- **TLAS 每帧重建（cheap）**：物体动了只更新 instance transform，BLAS 不变
- **BLAS rebuild 是性能黑洞**：取决于 mesh 三角形数，几百 K 三角形可能毫秒级
- **TLAS rebuild 是 cheap**：只重排 instance（几千个），<1 ms

类比 raster 的 instancing，但是 *几何端* 的实例化（不是 draw 端）。

详见 [02 Acceleration Structure](02-acceleration-structure.md)。

## 1.5 Ray 怎么发出来

每条 ray 由 8 个参数定义：

```glsl
traceRayEXT(
    accelStructure,        // 查哪个 TLAS
    rayFlags,              // gl_RayFlagsOpaqueEXT / TerminateOnFirstHitEXT 等
    cullMask,              // 跟 instance.mask 做 AND，匹配才命中（per-light shadow 用）
    sbtRecordOffset,       // SBT 偏移（多 hit shader 时用）
    sbtRecordStride,       // SBT 步长
    missIndex,             // miss shader 索引（多 miss 时用）
    origin,                // ray 起点（world space）
    Tmin,                  // 最小命中距离（防 self-intersection，典型 0.001）
    direction,             // 方向（normalized world space）
    Tmax,                  // 最大命中距离（光源距离 / 100.0）
    payload                // payload location 索引
);
```

`rayFlags` 常用：

- `gl_RayFlagsOpaqueEXT`：跳过 any-hit shader（透明物体不参与）
- `gl_RayFlagsTerminateOnFirstHitEXT`：找到任意命中即停（shadow ray 用）
- `gl_RayFlagsCullBackFacingTrianglesEXT`：剔除背面（性能优化）

## 1.6 6 个 Shader Stage

RT pipeline 有 6 个新的 shader stage（vs raster 只有 vertex/fragment/...）：

| Shader Stage | 触发时机 | 用途 | Vulkan flag |
|-------------|---------|------|-----|
| **Ray generation** | 每 thread 1 次（per pixel/probe） | 决定从哪儿发 ray、用什么参数 | `VK_SHADER_STAGE_RAYGEN_BIT_KHR` |
| **Intersection** | ray 进入某个 AABB | *自定义几何求交*（球 / SDF / particle）；triangle 用内建 | `VK_SHADER_STAGE_INTERSECTION_BIT_KHR` |
| **Any-hit** | ray 命中三角形（候选） | 决定是否忽略此命中（透明 alpha test） | `VK_SHADER_STAGE_ANY_HIT_BIT_KHR` |
| **Closest-hit** | ray 找到 *最近* 命中 | 算 shading（颜色 / 法线 / reflection 递归） | `VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR` |
| **Miss** | ray 没命中任何东西 | sky color / background / env map | `VK_SHADER_STAGE_MISS_BIT_KHR` |
| **Callable** | shader 主动调 | 类似函数指针，跨 shader 共享代码 | `VK_SHADER_STAGE_CALLABLE_BIT_KHR` |

调用流程：

```
ray gen → traceRayEXT
            ↓
       (BVH 遍历 + intersection)
            ↓
       命中?
       ↙        ↘
   any-hit       miss
   ignore?       ↓
   ↙   ↘       payload 写完 → 返回 ray gen
  no   yes
   ↓     ↓
closest- skip
hit
   ↓
 payload 写完 → 返回 ray gen
```

**心智模型**：SBT 像 C++ 虚函数表 vtable。RT pipeline 定义 *interface*（"我要 1 个 raygen + 1 个 miss + 1 个 closest-hit"），SBT 决定每种事件 *具体调哪段代码*——而且 *每个 BLAS instance 可以选不同的 closest-hit*（金属 BRDF / 玻璃 / SSS 等）。

## 1.7 两种调用范式：RT Pipeline vs Ray Query

Vulkan 提供 *两条独立* 的 RT 调用路径：

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
  - miss                                  自己继续算，无 callback
  ↓
payload 写回 raygen
```

|  | RT Pipeline | Ray Query |
|------|-----------|-----------|
| 调用位置 | 独立 raygen 入口 | *任何 shader 内随时调* |
| Shader 数 | ≥ 3（raygen + closest-hit + miss） | **0 个 RT shader** |
| SBT | 必需 | 不需要 |
| 递归 | 支持 | 不支持 |
| 调度命令 | `vkCmdTraceRaysKHR` | 普通 `vkCmdDispatch` / draw |
| 适用 | 复杂 hit shader / 递归 / 多材质 | 简单可见性查询（shadow / AO） |
| 扩展 | `VK_KHR_ray_tracing_pipeline` | `VK_KHR_ray_query` |

**两者必须并存，互补不互替**：

- Shadow（Ch13）→ ray query（4 行代码就够，不需要 shader 调度）
- DDGI / Reflection（Ch14/15）→ RT pipeline（hit shader 算复杂 lighting）

详见 [03 RT 管线与 Ray Query](03-RT管线与ray-query.md)。

## 1.8 Payload：跨 Shader 通信

Raster 里 vertex → fragment 通过 varying 自动插值。RT 里 raygen → closest-hit/miss 通过 *显式 payload*：

```glsl
// 在 raygen
layout(location = 0) rayPayloadEXT vec4 payload;
traceRayEXT(..., 0);  // payload location = 0
// 调用返回后 payload 已被 hit/miss shader 写过
imageStore(out, gl_LaunchIDEXT.xy, payload);

// 在 closest-hit
layout(location = 0) rayPayloadInEXT vec4 payload;
hitAttributeEXT vec2 barycentric_weights;  // 自动填充
void main() {
    payload = vec4(1, 0, 0, 1);
}
```

可以同时持有多个 payload（不同 location）——做 reflection / refraction 时常见：原 ray 的 payload + reflection ray 另一个 payload。

`hitAttributeEXT` 是 hit shader 自动收到的命中信息（重心坐标用于三角形）。

## 1.9 Ray Tracing 的硬件依赖

Hardware RT *仅* 这些 GPU 支持：

| 厂商 | 最早世代 | 注意 |
|------|---------|------|
| NVIDIA | RTX 20（Turing，2018） | Pascal / Maxwell 完全无 |
| AMD | RDNA 2（RX 6000，2020） | RDNA 1 / GCN 完全无 |
| Intel | Arc A 系（2022） | 老 UHD / Iris 完全无 |
| Apple Silicon | M3（2023） | Metal API，跟 Vulkan 不同 |
| 移动端 | 极少（Adreno 740+ 才有） | 实质性能也很差 |
| **WebGPU** | **无（截至 2026-04）** | 跟 Phase 1 跨平台目标 *直接冲突* |

**重要意义**：LX 引入 RT 必须设计 *双路径*：

- Desktop + RTX/RDNA2/Arc → RT 路径
- 其它（包括 Web）→ 传统 raster 路径（shadow map / VXGI / lightmap / SSR）

详见 [08 演进路径](08-演进路径.md)。

## 1.10 LX 现状

LX 完全没有 RT 基础设施：

- ❌ 未启用 `VK_KHR_acceleration_structure`
- ❌ 未启用 `VK_KHR_ray_tracing_pipeline`
- ❌ 未启用 `VK_KHR_ray_query`
- ❌ 未启用 `VK_KHR_buffer_device_address`（RT 强依赖）
- ❌ 没有 BLAS / TLAS handle 类型
- ❌ 没有 RT pipeline 类型（仅 graphics pipeline）
- ❌ ShaderReflector 不识别 RT shader stage
- ❌ FrameGraph 不知道怎么调度 RT pass

这些都是 [08 演进路径](08-演进路径.md) 的 REQ-A 内容（基础设施，最先建）。

## 1.11 接下来读什么

- [02 Acceleration Structure](02-acceleration-structure.md) — BLAS / TLAS 数据结构 + 两步构建 + 跨帧更新
