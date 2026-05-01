# 03 · Compute Shader 基础

> 阅读前提：[02](02-多queue机制.md) 已经讲清多 queue 提交。本文聚焦 compute shader 自身的执行模型与编程注意事项。

## 3.1 SIMT 执行模型

GPU 不是几千个独立 CPU，而是一种特殊的并行架构：

| 模型 | 含义 |
|------|------|
| **SIMD**（Single Instruction Multiple Data） | CPU AVX 指令，一条指令同时算 8 个 float |
| **SIMT**（Single Instruction Multiple Threads） | GPU 模型，一条指令在几十甚至几百个 *看似独立* 的 thread 上同时执行 |

GPU 的 "thread" 比 CPU thread 弱：

- 不能任意分支（同 group 内分支会损失性能 —— 部分 thread 闲置等其他 thread）
- 但灵活度比纯 SIMD 高 —— 每个 thread 有自己的寄存器和分支状态

不同厂商对"一组 thread"叫法不同：

| 厂商 | 名字 | 默认大小 |
|------|------|---------|
| NVIDIA | warp | 32 |
| AMD | wave | 32 或 64（看代际） |
| Intel | subgroup | 8/16/32 |
| 通用术语 | thread group | — |

写 shader 时不直接接触这个粒度，但 shader 写得好不好会影响性能。

## 3.2 两层 Group Size

Compute shader 的工作组织有 *两个* 维度：

### Local Group Size（shader 里声明）

```glsl
layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
```

意思："**单次 shader 调用** 内部跑 8×8×1 = 64 个 thread"。

这是 *shader 编译期常量*。一次 shader 调用 = 一个 thread group。group 内 thread：

- 可以共享 local memory（`shared` 变量）
- 可以同步（`barrier()`）
- 可以使用 subgroup 操作（warp-level 原语）

### Global Group Size（dispatch 时给）

```cpp
gpu_commands->dispatch(width / 8, height / 8, 1);
```

意思："发起 `(width/8) × (height/8) × 1` 次 shader 调用"。

每次调用都是一个完整 thread group。所以总 thread 数 = global × local。

### 用例子算清楚

要处理一张 1280×720 图像，每个 thread 处理一个像素：

```
每个 thread group 处理 8×8 = 64 像素
需要 group 数：(1280 / 8) × (720 / 8) = 160 × 90 = 14400 groups
总 thread 数：14400 × 64 = 921,600 = 1280 × 720 ✓
```

dispatch 调用：

```cpp
dispatch(1280 / 8, 720 / 8, 1);
```

shader 里查"我是哪个像素"：

```glsl
ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
```

`gl_GlobalInvocationID` = local + global 的组合 ID，每个 thread 都不同。

### 边界处理

如果图像不是 8 的整数倍（比如 1281×720），dispatch 用 `ceil(1281/8) = 161`，会多算一行 thread。shader 里要判断：

```glsl
if (pos.x >= width || pos.y >= height) return;
```

否则 thread 会越界访问。这是 compute shader 的常见 boilerplate。

### Local Group Size 的选取

每个 GPU 有 *最优 thread group 大小*。常见选择：

| 用途 | 推荐 local size |
|------|----------------|
| 1D 工作（buffer 处理） | 64 / 128 / 256 |
| 2D 工作（image 处理） | 8×8 / 16×16 / 32×32 |
| 3D 工作 | 4×4×4 / 8×8×8 |

NVIDIA 偏好 32 的倍数（warp 对齐）；AMD 偏好 64 的倍数（wave64 对齐）；Intel 偏好 8/16 的倍数。**通用安全选择：64 或 256**。

## 3.3 Storage Block (SSBO)

跟 vertex / fragment shader 主要用 uniform buffer 不同，compute shader 大量用 *storage buffer*：

```glsl
layout (set = 0, binding = 1) buffer PhysicsMesh {
    uint index_count;
    uint vertex_count;
    PhysicsVertex physics_vertices[];   // runtime array
};
```

特点：

| 维度 | Uniform Buffer | Storage Buffer |
|------|---------------|----------------|
| 读写 | 只读 | 可读写 |
| 大小上限 | 一般 64KB | 几 GB |
| Runtime array | 不支持 | 支持（且必须最后） |
| 性能 | 更优（缓存友好） | 略慢（更灵活） |

最后一条限制（runtime array 必须是 struct 最后一个成员，且每个 storage block 只能有一个）是 *为什么布料模拟例子要把不同数据拆成多个 storage block*：

```glsl
buffer PhysicsMesh   { ... PhysicsVertex physics_vertices[]; };
buffer PositionData  { float positions[]; };
buffer NormalData    { float normals[]; };
buffer IndexData     { uint indices[]; };
```

不能塞一个 block 里。

## 3.4 SoA vs AoS

```cpp
// AoS: Array of Structures
struct Vertex { vec3 pos; vec3 normal; vec3 tangent; };
Vertex mesh_vertices[N];

// SoA: Structure of Arrays
vec3 positions[N];
vec3 normals[N];
vec3 tangents[N];
```

| 维度 | SoA | AoS |
|------|-----|-----|
| 单独访问某属性 | ✅ 可以单独绑定 position | ❌ 必须读整个 struct |
| 内存局部性 | ✅ 连续读 position 缓存友好 | ❌ struct 大时浪费 cache line |
| 描述符绑定数 | ❌ 多个 buffer | ✅ 一个 buffer |
| 思维直观 | ❌ 数据散开 | ✅ 单顶点数据集中 |
| 部分更新 | ✅ 只更新某属性 | ❌ 必须重写整个 struct |

LX 当前的 `VertexBuffer` 应该是 *interleaved AoS*（pos+normal+uv 交错），这是经典做法。但 compute shader 修改顶点时 SoA 通常更顺手，因为：

- 不同 attribute 可以独立更新（compute 改 position，graphics 还在读老 normal 也不冲突）
- depth pass 只读 position，SoA 直接绑 position buffer

两种 layout 可以共存，看具体 pass 的需求。

## 3.5 GLSL 陷阱：没有引用

```glsl
// 错的写法（CPU 思维）
PhysicsVertex& v = physics_vertices[idx];   // GLSL 不支持引用
v.position = new_pos;                        // 实际上写到本地变量

// 对的写法
physics_vertices[idx].position = new_pos;   // 直接写回 buffer
```

GLSL 没有引用。看起来像在改 struct 成员，实际是在 *拷贝出来改本地副本*。必须用完整下标访问才会写回 buffer。

**这是从 CPU 移植代码时最容易踩的坑**。

## 3.6 Padding 陷阱：vec3 vs float[3]

```glsl
// 危险：每个 vec3 实际占 16 字节（padding 4 字节）
vec3 positions[];

// 安全：精确 12 字节 / vertex
float positions[];   // 用 [v*3+0], [v*3+1], [v*3+2] 访问
```

GLSL 的 std140 / std430 layout 规则会给 vec3 padding 4 字节对齐到 16。如果 CPU 端用 `glm::vec3` 是 12 字节，GPU 端读到的会错位。

两种解决方案：

1. CPU 也用 16 字节对齐（`alignas(16)` / `vec4`）— 浪费 25% 空间
2. GPU 端用 `float[]` 平铺访问 — 麻烦但精确

布料模拟例子选 2，理由："百万顶点级别浪费 4 字节加起来不少"。

## 3.7 Compute Shader 内的同步

### 同 Thread Group 内

```glsl
shared float local_data[64];   // group 内共享内存

local_data[gl_LocalInvocationIndex] = compute_something();
barrier();   // 等所有 thread 写完
memoryBarrierShared();  // 让写对其他 thread 可见

float result = local_data[other_index];   // 安全读
```

`barrier()` + `memoryBarrier*()` 配合使用。barrier 管 *执行* 顺序，memory barrier 管 *写入可见性*。

### 跨 Thread Group

**同 dispatch 内的多个 thread group 之间没有同步原语**。

如果需要"先全 group 算完 step1、再全 group 算 step2"，只能：

1. 拆成两次 dispatch（中间靠 pipeline barrier）
2. 用 atomic 原子操作（性能差，慎用）

### Atomic 操作

```glsl
atomicAdd(counter[0], 1);
atomicMin(min_value[0], my_value);
```

跨 thread 读改写一个内存位置时用。注意：

- atomic 操作有性能开销（serialize 同位置访问）
- 高并发下变成瓶颈（counter 这种被所有 thread 抢的场景）
- 优先用 *归约*（reduction）代替 atomic 累加

## 3.8 一个工程选择：1 thread / mesh vs 1 thread / vertex

布料模拟例子里有个有趣的设计：每个 thread 处理一整个 mesh，而不是一个 vertex。

| 方案 | 并行度 | 同步开销 |
|------|--------|---------|
| 1 thread / vertex + split dispatch | 高（百万 thread） | 中等（barrier + 中间 buffer） |
| 1 thread / mesh | 低（mesh 数级别） | 零（mesh 内串行） |

布料模拟选了"1 thread / mesh"，理由：

- vertex 间有依赖（算 force 时读其他 vertex 的 position），需要 split dispatch
- 如果 mesh 数远大于 vertex/mesh 数（很多小 mesh），低并行度的方案 *也能填满 GPU*
- 节省 split dispatch 的 barrier 和中间 buffer

这是一个 *workload-specific* 的判断，不是普遍规律。

## 3.9 LX 当前没有的所有东西

回到 LX 现状对照：

- ❌ Compute shader stage 加载（`*.comp.spv`）
- ❌ Compute pipeline 创建路径
- ❌ Compute shader 反射（local group size 提取、storage block 识别）
- ❌ Storage buffer 作为 `IGpuResource` 角色
- ❌ Dispatch 命令封装
- ❌ Compute descriptor set 类型（与 graphics 不同的绑定模式）

REQ-A 要把这些一次性铺起来。

## 3.10 接下来读什么

- [04 LX 当前状态对照](04-LX当前状态对照.md) — 字段级 / 能力级 gap 分析
