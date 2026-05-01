# 01 · Frame Graph 是什么

> 阅读前提：理解一帧渲染会涉及多个 pass（shadow / GBuffer / lighting / post-process / UI），多个 pass 之间通过 GPU 资源传递数据。

## 1.1 一帧渲染的真实复杂度

简单 demo 的一帧只有：

```
clear → draw mesh → present
```

但现代引擎的一帧通常是：

```
shadow pass → depth pre-pass → gbuffer pass → SSAO → lighting →
transparent → bloom → tonemap → UI → present
```

每个 pass 都：

- 写到某些 GPU 资源（color attachment / depth buffer / storage buffer）
- 读取前面 pass 写出的资源（深度图 / G-buffer / 光照结果）

由此引出三个必须解决的问题。

## 1.2 三类问题

### 问题 1：依赖关系

`lighting_pass` 需要 `gbuffer_pass` 写的法线和材质数据。lighting 必须在 gbuffer 之后执行。

如果 pass 顺序由人手维护：每加一个 pass 就要重排一遍调用顺序，容易写错。pass 多了之后很难一眼看清谁依赖谁。

### 问题 2：GPU 同步（barrier）

GPU 是高度并行的硬件。"画 GBuffer" 和 "用 GBuffer 算光照" 这两个命令提交后，GPU 不会自动等前者完全写完。**必须显式插入 barrier**，告诉 GPU：

- 等 GBuffer 的写入全部完成
- 把图像 layout 从 `COLOR_ATTACHMENT_OPTIMAL` 切换到 `SHADER_READ_ONLY_OPTIMAL`

这叫 **memory barrier** + **layout transition**。手写极容易漏 / 错，错了的症状是花屏、黑屏、Vulkan validation error。

### 问题 3：显存复用（aliasing）

每个 1280×800 的 RGBA8 attachment 占约 4 MB。一帧中可能有十几张，加起来几十 MB。

但很多 attachment 的生命周期 *不重叠*：

```
gbuffer_colour:    [写] -- [读] ----------- (之后没人用)
gbuffer_normals:   [写] -- [读] -----------
final:                              [写] -- [读]
```

`final` 完全可以复用 `gbuffer_colour` 的显存 —— 它们生命周期不重叠。这叫 **resource aliasing**。桌面端可有可无；移动端因显存紧张几乎是必须的。

## 1.3 Frame Graph 是什么

**Frame Graph 就是用来自动解决上面三个问题的一个数据结构 + 算法**。

它把"一帧渲染"建模成一个 **DAG（有向无环图）**：

- **节点（node）** = 一个 pass
- **边（edge）** = pass 之间的资源依赖（B 读 A 写出的资源 → A → B 边）

有了这个 DAG，框架可以：

| 算法 | 解决的问题 |
|------|-----------|
| 拓扑排序 | 自动决定 pass 执行顺序 |
| 资源生命周期分析 | 自动插入正确的 barrier |
| 引用计数 + 区间 packing | 决定哪些资源可以 alias |

总之是把"我有哪些 pass、它们用哪些资源"这个 *声明性描述*，转成"GPU 命令该怎么发"这个 *命令性执行流*。

## 1.4 为什么 LX 当前的实现不算 Frame Graph

LX 当前 `FrameGraph` 只做了：

```cpp
class FrameGraph {
  std::vector<FramePass> m_passes;          // 平铺 pass 列表
  void addPass(FramePass);                   // 顺序累加
  void buildFromScene(const Scene &);        // 按调用顺序透传 target
  std::vector<PipelineBuildDesc>             // 跨 pass PipelineKey 去重
    collectAllPipelineBuildDescs() const;
};
```

它能做的：

- 按 `addPass` 调用顺序一字排开
- 给每个 pass 单独跑 `RenderQueue::buildFromScene`
- 跨 pass 去重 PipelineKey

它做不到的（也是上面三类问题）：

- 资源依赖：`FramePass` 没有 inputs / outputs 字段
- barrier 推导：barrier 散在 backend 里 ad-hoc 处理
- aliasing：每个 attachment 独立分配，无生命周期分析

所以 **它叫 frame graph 但不是 frame graph**，更像 *render queue list*。

## 1.5 为什么现在不需要立刻补上

LX 当前实际跑的渲染只有：

- 单 forward pass
- 单 swapchain target
- 没有 deferred / shadow / post-process / GBuffer 复用

在这种 *单 pass 单 target* 的场景下，没有 frame graph 也跑得动 —— 三类问题都退化成 trivial 解（依赖只有一个、barrier 简单、没有 alias 机会）。

真正需要 frame graph 的触发条件：

1. 引入 [REQ-119](../../main-roadmap/phase-1-rendering-depth.md#req-119--g-buffer--延迟渲染路径) 延迟渲染（GBuffer 跨 pass 流转）
2. 引入 [REQ-103](../../main-roadmap/phase-1-rendering-depth.md#req-103--shadow-pass--depth-only-pipeline) shadow（shadow map 跨 pass 流转）
3. 引入 [REQ-107](../../main-roadmap/phase-1-rendering-depth.md#req-107--bloom-pass) Bloom（多级 mip downsample/upsample）
4. 引入 [REQ-101](../../main-roadmap/phase-1-rendering-depth.md#req-101--hdr-scene-color-target) HDR scene color → tonemap

任一条命中，"手工 barrier + 手工 attachment 管理" 的成本会陡升，是引入 frame graph 的合理时点。

## 1.6 接下来读什么

- [02 数据驱动方式](02-数据驱动方式.md) — JSON 描述形态 + 四种资源类型
- [03 实现层数据结构](03-实现层数据结构.md) — 内存中如何表达节点和资源
- [04 compile 阶段](04-compile-阶段.md) — 构边 / 拓扑排序 / aliasing 的具体算法
- [05 LX 当前状态对照](05-LX当前状态对照.md) — 字段级 gap 分析
- [06 演进路径](06-演进路径.md) — REQ-A..G 切分
