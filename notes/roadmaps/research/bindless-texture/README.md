# Bindless Texture 技术调研

> 状态：**仅记录，暂不实施**
> 最后更新：2026-04-23
> 目的：记录现代引擎 bindless 资源绑定的做法，为 LX Engine 未来渲染架构演进留存参考。

## 这个目录是什么

Bindless 是现代 GPU 驱动渲染的基石之一。这组文档面向 **没用过 bindless 的读者**，从"它解决什么问题"一路讲到"如果 LX Engine 要实现，大致怎么做"。

整套内容完整顺读约 45 分钟。可以按章节挑读。

## 建议阅读顺序

| # | 文档 | 讲什么 | 谁适合读 |
|---|------|-------|---------|
| 01 | [动机与核心模型](01-动机与核心模型.md) | 传统绑定的痛点、bindless 的基本思路、**binding vs slot 两层模型** | 所有人 |
| 02 | [实现细节](02-实现细节.md) | Vulkan 代码（device feature / pool / layout / set / update）、shader 端基础、**同步与更新策略** | 打算真正写代码的人 |
| 03 | [nonuniform 与 warp 执行模型](03-nonuniform与warp执行模型.md) | warp/wave 概念、**传统不需要、bindless 必须用 `nonuniformEXT` 的原因** | 所有写 shader 的人 |
| 04 | [Pipeline 与 Shader 变量策略](04-Pipeline与Shader策略.md) | bindless 对 PSO 数量的真实影响、ubershader vs permutation vs spec const | 关心 PSO 爆炸问题的人 |
| 04A | [Shader 反射与 Layout 约束](04a-Shader反射与Layout约束.md) | 传统 reflection 自动生成 layout，与 bindless 下固定 ABI + 校验模型的关系 | 想把 reflection / material / layout 三层职责想清楚的人 |
| 05 | [业界现状对比](05-业界现状对比.md) | UE5 / id Tech / Godot / KlayGE 的做法与定位差异 | 做技术选型参考 |
| 06 | [LX Engine 演进路径](06-LXEngine演进路径.md) | 当前状态、是否要做、若做则分步路径、触发条件 | 工程决策层 |

## 按问题导航

| 我想搞懂 | 看哪 |
|---------|------|
| bindless 到底是什么？ | [01](01-动机与核心模型.md) |
| binding 和 slot 有啥区别？ | [01 §1.3](01-动机与核心模型.md#13-bindless-的两层模型binding-vs-slot) |
| 代码到底怎么写？ | [02](02-实现细节.md) |
| 更新会不会竞态？ | [02 §2.7 同步与更新策略](02-实现细节.md#27-同步与更新策略) |
| 为什么 shader 要写 `nonuniformEXT`？ | [03](03-nonuniform与warp执行模型.md) |
| 什么是 warp / wave？ | [03 §3.1](03-nonuniform与warp执行模型.md#31-gpu-怎么执行-shader) |
| bindless 能让 PSO 数量下降吗？ | [04](04-Pipeline与Shader策略.md) |
| 自动生成 pipeline layout 和 bindless 到底是什么关系？ | [04A](04a-Shader反射与Layout约束.md) |
| 为什么同一个 binding 能被不同 shader 当成 2D / 3D 用？ | [04A §4A.4](04a-Shader反射与Layout约束.md#4a4-pipelinelayout-到底会不会限制-2d--3d) |
| bindless 下 reflection 还有没有必要？ | [04A §4A.6](04a-Shader反射与Layout约束.md#4a6-bindless-下reflection-和材质系统怎么配合) |
| ubershader 和 permutation 哪个好？ | [04 §4.3](04-Pipeline与Shader策略.md#43-shader-变量差异的三种处理路线) |
| Godot / UE / KlayGE 怎么做的？ | [05](05-业界现状对比.md) |
| 我们什么时候上？怎么上？ | [06](06-LXEngine演进路径.md) |

## TL;DR · 关键结论

**关于 bindless**：

- Bindless 是 AAA 引擎的默认方向：id Tech 7 / UE5 / Frostbite / Decima 等都已全面采用
- **单独用 bindless 不会减少 PSO 数量**，它消除的是 **descriptor set layout / pipeline layout / CPU 绑定开销**
- 想把 PSO 从 10 万+ 打到 ~500，需要 **bindless + ubershader + GPU-driven** 三件套组合

**关于实现**：

- 整个应用只创建 **1 个** descriptor set，内部是一个或几个超大数组
- `binding` 固定不变，`slot` 运行时动态分配
- 通过 **PARTIALLY_BOUND + UPDATE_AFTER_BIND + append-only slot 分配 + frames-in-flight 延迟回收** 保证安全
- Shader 必须对发散的索引加 `nonuniformEXT()`，否则 UB

**关于移动端**：

- Android 端 descriptor indexing 支持率只有 1%，**移动端仍需传统路径做 fallback**
- WebGPU 暂无 bindless 标准化

**关于 LX Engine**：

- 当前**不采用** bindless，决策原因和演进路径见 [06](06-LXEngine演进路径.md)
- 最重要的前置动作是 **material DSL / shader 编译器层抽象**（参考 Godot `.gdshader` 的做法）
- 暂时不动渲染后端，但 shader / material 接口要按"将来能切到 bindless"的方式设计

## 参考资料汇总

### 通用

- [GPU Driven Rendering Overview — Vulkan Guide](https://vkguide.dev/docs/gpudriven/gpu_driven_engines/)
- [The Shader Permutation Problem Part 2 — MJP](https://therealmjp.github.io/posts/shader-permutations-part2/)
- [Rendering Doom Eternal — SIGGRAPH 2020](https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf)
- [DOOM Eternal Graphics Study — Simon Coenen](https://simoncoenen.com/blog/programming/graphics/DoomEternalStudy)
- [Bindless Rendering in DX12 & SM6.6 — Tarun Ramaswamy](https://rtarun9.github.io/blogs/bindless_rendering/)
- [Bindless — Alex Tardif](https://alextardif.com/Bindless.html)
- [Uber Shaders and Shader Permutations — Alex Tardif](https://alextardif.com/UberShader.html)
- [Bindless Resources in UE5 — Epic Forums](https://forums.unrealengine.com/t/bindless-resources-in-ue5/1393077)
- [Managing bindless descriptors in Vulkan — dev.to](https://dev.to/gasim/implementing-bindless-design-in-vulkan-34no)
- [Designing Mobile Rendering Engines with Bindless Vulkan — ACM](https://dl.acm.org/doi/fullHtml/10.1145/3641233.3664326)
- [Vulkan Pills 1: Bindless Textures — Gabriel Sassone](https://jorenjoestar.github.io/post/vulkan_bindless_texture/)
- [Bindless Texturing for Deferred Rendering — MJP](https://mynameismjp.wordpress.com/2016/03/25/bindless-texturing-for-deferred-rendering-and-decals/)
- *Mastering Graphics Programming with Vulkan* (Raptor Engine / Hydra) — 02 章的 Vulkan 代码骨架来自本书

### nonuniform / subgroup

- [SPIR-V 规范 · NonUniform Decoration](https://www.khronos.org/registry/SPIR-V/specs/unified1/SPIRV.html#_a_id_nonuniform_a_nonuniform)
- [GL_EXT_nonuniform_qualifier 扩展](https://github.com/KhronosGroup/GLSL/blob/master/extensions/ext/GL_EXT_nonuniform_qualifier.txt)
- [Vulkan 规范 · Shaders Chapter · Uniformity](https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#shaders-scope-subgroup)

### Godot

- [GPU Driven Renderer for Godot 4.x — reduz (gist)](https://gist.github.com/reduz/c5769d0e705d8ab7ac187d63be0099b5)
- [godot-proposals#10423 · Add support for nonuniformEXT()](https://github.com/godotengine/godot-proposals/issues/10423)
- [Internal rendering architecture — Godot Engine 4.5 docs](https://docs.godotengine.org/en/4.5/engine_details/architecture/internal_rendering_architecture.html)
- [Overview of renderers — Godot Engine docs](https://docs.godotengine.org/en/stable/tutorials/rendering/renderers.html)
- [RenderingDevice — Godot Engine docs](https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html)

### KlayGE

- [KlayGE 官方网站](http://www.klayge.org/)
- [KlayGE GitHub 仓库 — gongminmin/KlayGE](https://github.com/gongminmin/KlayGE)
