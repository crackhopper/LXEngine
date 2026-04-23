# 04 · Pipeline 与 Shader 变量策略

> Bindless 经常被误解成 "PSO 数量自动下降" —— 这不对。
> 让 PSO 从 10 万打到 500 的是 **bindless + ubershader + GPU-driven** 三件套组合。这一章讲它们的分工。

## 4.1 Bindless 本身的 pipeline 收益

**Bindless 单独使用并不会减少 Pipeline 数量。** 它真正消除的是 **pipeline layout** 和 **CPU 绑定开销**，而不是 pipeline state object (PSO) 本身。

| 能被 bindless 消除 | 不能被 bindless 消除 |
|---|---|
| `VkDescriptorSetLayout` 的数量（通常全局 1 个） | Shader 代码差异（不同 BRDF、不同特性开关） |
| `VkPipelineLayout` 的数量（通常全局 1~2 个） | Raster state / Blend state / Depth state 差异 |
| VertexInputState（顶点数据也进 SSBO） | Render target format / MSAA 样本数差异 |
| 大量 `vkCmdBindDescriptorSets` 调用 | Pass 本身的结构差异（forward / deferred / shadow） |

所以 bindless 的主要价值是 **layout 收敛** 和 **CPU 绑定成本降低**，不是自动消灭 PSO 爆炸。

## 4.2 关键对比数字

| 引擎 | Shader 数 | PSO 数 | 策略 |
|-----|----------|-------|------|
| **Doom Eternal (id Tech 7)** | ~100 | ~350 | bindless + ubershader + GPU-driven 三件套 |
| **典型 UE 项目** | — | 100,000+ | 传统 per-material permutation |

差异来自 shader 代码策略，不是 bindless 本身。

## 4.3 Shader 变量差异的三种处理路线

### (a) 静态 Permutation（传统 UE 路线）

每个特性组合编译成独立 shader → 独立 PSO。

```glsl
#ifdef HAS_NORMAL_MAP
    vec3 n = texture(normalTex, uv).xyz * 2.0 - 1.0;
#else
    vec3 n = vec3(0, 0, 1);
#endif
```

- ✅ 死代码真正消失、寄存器占用最低
- ❌ PSO 爆炸（有法线 × 有金属度 × 有 AO × ... → 组合爆炸）
- ❌ 首次运行 stutter、编译时间长、内存占用大

UE 的 10 万+ PSO 就是这个代价。

### (b) 纯 Ubershader + 动态分支（Doom Eternal 路线）

把所有特性塞进一个大 shader，用 UBO/SSBO 里的 `materialFlags` 做 **uniform branching**：

```glsl
if ((material.flags & HAS_NORMAL_MAP) != 0) {
    n = texture(global_textures[material.normalIdx], uv).xyz * 2.0 - 1.0;
} else {
    n = vec3(0, 0, 1);
}
```

同一 draw 内所有 fragment 走相同路径（wave/warp 不分叉），现代 GPU 上几乎零成本。

- ✅ PSO 数量大幅下降，~100 shaders / ~350 PSO 搞定全游戏
- ✅ 首帧 stutter 消失、编译时间短
- ⚠️ 寄存器占用偏高（编译器要为所有分支都预留）
- ⚠️ 驱动会悄悄帮你把 uniform 分支 flatten，不同驱动行为可能不一致

### (c) Specialization Constants —— 中间选项

Vulkan 的 **Specialization Constants** 是一个折中：一份 GLSL 源码，在 pipeline 创建时把 constant 固化进去，driver 可以做常量传播和死代码消除。

```glsl
layout (constant_id = 0) const bool HAS_NORMAL_MAP = false;

void main() {
    if (HAS_NORMAL_MAP) {
        // 创建 pipeline 时这个分支要么被保留要么被消除
    }
}
```

本质还是 permutation（每个常量组合一个 PSO），但源码层面像 ubershader。类似 C++ 的 `constexpr` 切换。适合"少量重要变体 + 大量热路径共享"的场景。

### (d) 混合（当前多数新引擎的推荐路线）

按 "差异的性质" 分层：

| 差异类型 | 处理方式 | 原因 |
|---------|---------|------|
| Vertex layout、MSAA、blend mode、cull mode、RT format、depth 模式 | **必须 permute**（硬件 state 差异） | 这些是 pipeline state，不能分支 |
| 大结构差异：forward / deferred / visibility / shadow pass | **必须 permute**（整段代码不同） | 输入输出契约都不一样 |
| 材质特性开关（normal map 有无、IBL 有无、detail 层数） | **ubershader + uniform branch**（零成本） | Uniform 分支现代 GPU 零开销 |
| 纹理 / 参数差异 | **bindless 索引**（完全运行时） | 不影响 PSO |
| 性能敏感的少数变体（移动端 vs 桌面） | **specialization constant** | 少量 PSO，driver 优化 |

## 4.4 Pass 内部 vs Pass 之间的边界

一个容易踩的坑："shader 膨胀"不是"所有东西合成一个巨无霸 shader"。

**同一个 pass 内的不同材质可以合并进一个 ubershader**。但**不同 pass（shadow / GBuffer / forward / post）一般仍各有各的 shader**。原因：

- shadow pass 没有 fragment 输出
- GBuffer 输出多张 RT
- forward 输出带 alpha 的 HDR
- post 的输入是 screen-space texture

这些 pass 的输入输出契约从根子上不同，强合并得不偿失。

Doom Eternal 的 ~100 shader / ~350 PSO 的结构大致是：

```
每个 pass 一个 ubershader（10~20 个 pass）
    ↓
每个 pass 的 ubershader × 少数几种必须 permute 的 vertex / blend / state 组合
    ↓
~350 PSO
```

## 4.5 对 LX Engine 当前架构的启示

当前 `openspec/specs/` 下已有相关 spec：

- `pipeline-key/spec.md` —— `PipelineKey::build(objSig, matSig)` 结构化 pipeline 身份
- `pipeline-build-desc/spec.md` —— pipeline 构造输入聚合
- `render-signature/spec.md` —— `getRenderSignature(pass)`

这些 spec 已经把 pipeline identity 结构化了，是未来演进的良好基础。具体结合 bindless 的做法见 [06 · LX Engine 演进路径](06-LXEngine演进路径.md)。

## 4.6 风险提示：Driver 的隐式行为

MJP 的一个重要观察：**driver 可能会悄悄帮你把 uniform 分支 flatten**（偷看 uniform/constant buffer 的值做常量传播）。

好处：写 ubershader 的人可能得到意外的性能 bonus。
坏处：**不同驱动 / 不同驱动版本行为可能不一致**，同一份代码在 NVIDIA 上快在 AMD 上慢。

所以 ubershader 路线需要持续做性能 profile，别假设一次测好就永远好。

## 下一步

- 想把“自动生成 pipeline layout”和 bindless 的关系彻底分开看 → [04A · Shader 反射与 Layout 约束](04a-Shader反射与Layout约束.md)
- 想看业界具体怎么做 → [05 · 业界现状对比](05-业界现状对比.md)
- 想看 LX Engine 的演进方案 → [06 · LX Engine 演进路径](06-LXEngine演进路径.md)
