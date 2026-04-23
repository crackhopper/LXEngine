# 03 · nonuniform 与 warp 执行模型

> 这一章回答一个很容易被忽略但非常关键的问题：
> **为什么 bindless 的 shader 要写 `nonuniformEXT()`？传统绑定为什么不用？**
>
> 答案不在 bindless 本身，在 GPU 的 SIMD 执行模型。

## 3.1 GPU 怎么执行 shader：warp / wave / subgroup

CPU 上一个"线程"就是一个独立执行流。GPU 不是 —— **GPU 把一堆线程绑成一束，锁步跑同一条指令**。

| 厂商 / API | 叫法 | 一束的大小（典型） |
|------|------|-----------------|
| NVIDIA | **warp** | 32 |
| AMD (GCN) | **wavefront / wave** | 64 |
| AMD (RDNA) | **wave** | 32 或 64 |
| Intel | EU thread / SIMD8-16-32 | 8 / 16 / 32 |
| Vulkan / DX12 通用术语 | **subgroup** | 硬件相关 |

一个 fragment shader 跑起来，GPU 实际上是把 32 个相邻的像素打包成一个 warp，一次发射一条指令让 32 个像素一起执行。这是 **SIMD（Single Instruction, Multiple Data）**。

### 为什么关键：Uniform 和 Divergent

GPU 的寄存器分两类：

- **Scalar register (SGPR)** —— 整个 warp 共享一个值
- **Vector register (VGPR)** —— warp 里 32 个 lane 各自一个值

如果某个值在整个 warp 内"所有 lane 都一样"（比如从 uniform buffer 读出来的相机矩阵），它可以放 scalar register，一次 load 给全员用，省寄存器、省带宽、省指令。这叫 **uniform**。

如果这个值在 warp 内 "lane 之间不同"（比如每个像素的 UV 坐标），它必须放 vector register，32 份独立存着。这叫 **divergent** / non-uniform。

采样贴图时走哪条路很重要：

- **Uniform 采样**：32 lane 都采样"同一张"贴图的"各自 UV"。descriptor handle 进 scalar register，一次取，全员用。
- **Divergent 采样**：32 lane 采样"不同张"贴图的"各自 UV"。descriptor handle 本身在 vector register 里，每个 lane 的 handle 可能都不同。

后者硬件需要特殊处理，性能也天然更慢（最坏情况要循环处理 32 种 unique 值）。

## 3.2 传统绑定为什么天然 uniform

看看传统绑定的代码流：

```cpp
// CPU 端
vkCmdBindDescriptorSets(cmd, ..., set_for_material_A);  // ← 绑定 A
vkCmdDraw(cmd, ...);                                     // 画一批用 A 的物体

vkCmdBindDescriptorSets(cmd, ..., set_for_material_B);  // ← 换成 B
vkCmdDraw(cmd, ...);                                     // 画一批用 B 的物体
```

```glsl
// Shader 端
layout (set = 0, binding = 0) uniform sampler2D myTex;

void main() {
    color = texture(myTex, uv);  // myTex 编译时就是 descriptor set 0 的 binding 0
}
```

**关键观察**：

1. 一次 draw call 内，descriptor set 是 CPU 在 draw 之前就 bind 死的
2. `myTex` 这个变量在 shader 里是一个**编译期常量符号** —— 它永远指向"当前被 bind 的那个 set 的 binding 0"
3. 一个 warp 内的 32 个像素都来自这次 draw call，看到的"当前被 bind 的 set"**完全一样**

结论：**`myTex` 对应的 descriptor handle 在整个 warp 内必然相同**，驱动可以放心把它放进 scalar register。不存在 divergent 的可能，也就不需要告诉编译器 "小心，可能发散"。

> 一句话：**传统模型把"哪张贴图"编码进 pipeline state / CPU 绑定状态，shader 里是常量符号，天然 uniform。**

## 3.3 Bindless 为什么会 divergent

同样的场景换成 bindless：

```cpp
// CPU 端
vkCmdBindDescriptorSets(cmd, ..., global_bindless_set);  // 整帧就绑一次

vkCmdDraw(cmd, N_instances);   // 一次 draw 里可能有 N 种不同材质
// 或者更极端：vkCmdDrawIndirect 把一堆 logical draw 合并成一次
```

```glsl
// Shader 端
layout (set = 1, binding = 10) uniform sampler2D global_textures[];

void main() {
    uint idx = material.albedoIndex;              // ← 从 SSBO 读，per-instance
    color = texture(global_textures[idx], uv);    // ← 索引随像素而变
}
```

**关键观察**：

1. 一次 draw call（或一次 multi-draw-indirect）里可能同时画多种材质的物体
2. `idx` 的来源可能是：vertex attribute、SSBO indexed by `gl_InstanceIndex`、push constant 等 —— **都是 per-lane 的数据**
3. 一个 warp 里 32 个像素可能来自两个相邻三角形，而这两个三角形用不同材质 → `idx` 在 warp 内不一样 → **divergent**

这就是 bindless 的核心交易：**"哪张贴图"从 pipeline 状态搬到了 shader 的运行时数据**，代价就是失去了"天然 uniform"的保证。

## 3.4 `nonuniformEXT` 究竟做什么

GLSL 扩展 `GL_EXT_nonuniform_qualifier` 里的 `nonuniformEXT()` 是一个**限定符**，不是函数。它告诉编译器：

> "这个 index 在 subgroup 内**可能**发散，请生成 divergent-safe 的采样代码。"

```glsl
// 不加限定符：编译器可能假设 uniform 路径
texture(global_textures[idx], uv);               // ⚠️ idx 若 divergent → UB

// 加限定符：编译器生成发散安全路径
texture(global_textures[nonuniformEXT(idx)], uv); // ✅ 任何情况都正确
```

底层 SPIR-V 里，这个限定符翻译成一个 `OpDecorate %var NonUniform` 装饰。驱动看到这个装饰，就不会把 descriptor handle 放进 scalar register，而是生成一段"处理每个 lane 独立 handle"的指令序列（通常是 `s_waterfall` 循环，直到所有 unique 值都处理完）。

### 不加会怎么样

硬件/驱动行为取决于厂商：

| 典型表现 | 原因 |
|---------|------|
| 画面看起来"成块渲染"，材质边界像马赛克 | 硬件只读了 lane 0 的 handle，让整个 warp 都用同一张贴图 |
| 偶尔闪烁或纹理错乱 | subgroup 边界和三角形边界对齐时偶然正确，否则不正确 |
| 看起来完全正确 | 某些驱动/硬件（新 NVIDIA）静默处理了，但**规范上仍是 UB** |

所以 "**我试了没问题**" 不能作为不加 `nonuniformEXT` 的理由 —— 换个驱动、换个 GPU 就炸。

## 3.5 什么时候**不用**加？

`nonuniformEXT` 不是无脑加就好。它强制 divergent 路径，会让本来能走 uniform 快速路径的场景变慢。判断规则：

**如果索引在 subgroup 内必然相同，就不需要加。**

典型"天然 uniform"的索引：

```glsl
// (a) 来自 uniform buffer 或 push constant 的值
layout(push_constant) uniform PC { uint texIdx; } pc;
texture(global_textures[pc.texIdx], uv);             // uniform，不加

// (b) 编译期常量或字面量
texture(global_textures[0], uv);                     // uniform，不加

// (c) 来自 gl_DrawID（在 multi-draw-indirect 里，同一个 draw 内所有像素共享）
texture(global_textures[draw_data[gl_DrawID]], uv); // 通常 uniform（取决于驱动对 gl_DrawID 的 subgroup uniform 承诺）
```

典型"需要加"的索引：

```glsl
// (a) 来自 SSBO + per-instance 索引
uint idx = object_data[gl_InstanceIndex].matIdx;
texture(global_textures[nonuniformEXT(idx)], uv);    // ✅ 加

// (b) 来自 vertex attribute / interpolant
in flat uint vMatIdx;
texture(global_textures[nonuniformEXT(vMatIdx)], uv); // ✅ 加（flat 不等于 uniform）

// (c) 任何根据屏幕位置 / fragment 局部数据算出来的索引
uint idx = computeIdxFromUV(uv);
texture(global_textures[nonuniformEXT(idx)], uv);     // ✅ 加
```

**保守做法**：不确定就加。divergent 路径在"实际不发散"时，好的驱动也会自动降级成 uniform 路径。损失通常可以忽略。

### Vulkan 规范的精确定义

规范里用的术语是 **dynamically uniform**：

> 一个值在某个 program point 处 dynamically uniform，当且仅当**同一 subgroup 的所有活跃 invocation 在该点处具有相同的值**。

如果 dynamically uniform → 不需要 `nonuniformEXT`。否则必须加。

## 3.6 D3D12 对应物：`NonUniformResourceIndex()`

完全等价的概念在 D3D12 HLSL / SM 6.0+ 是：

```hlsl
Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(idx)];
// 或老写法
Texture2D textures[] : register(t0, space1);
float4 c = textures[NonUniformResourceIndex(idx)].Sample(s, uv);
```

语义、规则、违反后果都一样。所以 **bindless 的 shader 写法在两个 API 之间高度同构**，一套心智模型通用。

## 3.7 为什么 Godot 的 bindless 卡在这里

[Godot 的 nonuniform 提案 #10423](https://github.com/godotengine/godot-proposals/issues/10423) 还没实现，意味着 Godot 的 shader 语言目前**无法表达 divergent 索引**。

对于"地形按位置采样不同 heightmap"这类场景，用户现在只能用 `switch` 手写：

```glsl
// 没有 nonuniformEXT 的 workaround
switch (idx) {
    case 0: color = texture(heightmaps[0], uv); break;
    case 1: color = texture(heightmaps[1], uv); break;
    case 2: color = texture(heightmaps[2], uv); break;
    // ... 无穷无尽
}
```

这也是为什么 Godot 现在没法真正落实 bindless —— 底层 Vulkan 支持在，但 shader 语言层抽象没补齐。详见 [05 · 业界现状对比](05-业界现状对比.md)。

## 3.8 对 LX Engine 的含义

如果 LX Engine 将来上 bindless：

1. **shader 编译管线要强制开 `GL_EXT_nonuniform_qualifier`**（可以 `#extension` 内嵌到编译器前置模板）
2. **material DSL / shader graph 要识别 "索引是否 dynamically uniform"**，自动决定是否插 `nonuniformEXT`
3. **不要让用户手写 `nonuniformEXT`**——这是容易漏的点，属于编译器应该承担的义务

这正是 Godot `.gdshader` 抽象的价值（参见 [05](05-业界现状对比.md#godot-最有参考价值的设计)）：用户写逻辑，编译器管细节。

## 小结

- **传统绑定 uniform 是天然的** —— "哪张贴图"是 pipeline state，shader 里是编译期符号
- **Bindless 可能 divergent** —— "哪张贴图"变成运行时 per-lane 数据
- **`nonuniformEXT` 告诉驱动"生成发散安全代码"**
- **不加的后果是 UB**，典型表现是"材质成块"马赛克
- **何时该加**：索引来自 SSBO / vertex attribute / 任何 per-lane 源 → 必须加
- **何时不加**：索引来自 uniform / push constant / 字面量 → 天然 uniform，不加

## 参考

- [SPIR-V 规范 · NonUniform Decoration](https://www.khronos.org/registry/SPIR-V/specs/unified1/SPIRV.html#_a_id_nonuniform_a_nonuniform)
- [GL_EXT_nonuniform_qualifier 扩展](https://github.com/KhronosGroup/GLSL/blob/master/extensions/ext/GL_EXT_nonuniform_qualifier.txt)
- [godot-proposals#10423](https://github.com/godotengine/godot-proposals/issues/10423)
- [Vulkan 规范 · Shaders Chapter · Uniformity](https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#shaders-scope-subgroup)
