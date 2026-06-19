# FrameGraph：把 RenderPath pass 收束成可验证的资源 DAG

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/frame_graph/frame_graph.hpp](../../../../../src/core/frame_graph/frame_graph.hpp)
和它的实现
[src/core/frame_graph/frame_graph.cpp](../../../../../src/core/frame_graph/frame_graph.cpp)
出发，关注的不是"FrameGraph 有哪些方法"，而是它在当前单轨渲染流里的位置：
`FramePass` 打包 pass 身份、target、input contract、resource flow 和
RenderPathNode contract；`FrameGraph::compile()` 校验资源依赖并输出稳定 pass
顺序。draw / dispatch payload 由后续 `RenderWorkCompiler` 生成。

可以先带着一个问题阅读：core 层的 FrameGraph 已经负责哪些 graph 语义，
又把什么留给 compiler / backend？答案是它负责 resource registry 校验和
DAG 排序；它不持有 backend attachment，不做 aliasing / barrier 推导，也不遍历
scene 生成 render input。

源码入口：[frame_graph.hpp](../../../../src/core/frame_graph/frame_graph.hpp)

关联源码：

- [frame_graph.cpp](../../../../src/core/frame_graph/frame_graph.cpp)

## frame_graph.hpp

源码位置：[frame_graph.hpp](../../../../src/core/frame_graph/frame_graph.hpp)

### FramePass：pass 身份、target、input 与资源流

`FramePass` 把一条渲染 pass 的 core 层声明打包成一个结构体：

- `name`：StringID，来自 RenderPathGraph 的 pass 身份；它是这条 pass
  在后续 work 编译、material pass 选择、shader 变体合并里的统一身份
- `target`：这条 pass 的输出形状，使用 `RenderTargetDesc` 保留 offscreen /
  depth-only 等结构性描述；旧的 scene camera matching 边界再转回 `RenderTarget`
- `input`：RenderPathGraph 传入的 work 输入合同，只描述这条 pass 从哪里取
  输入，不在 FrameGraph 阶段生成 draw / dispatch payload
- `reads` / `writes`：有序 FrameGraph 的资源流声明，例如 Forward 写
  `hdr.color`，Bloom 再以 `SceneColor` binding 采样它

之所以打包而不是让 `FrameGraph` 持有多个并行 vector，是因为这些字段在每条
pass 上是强绑定的：`name` 决定后续编译诊断身份，`target` 决定 attachment
合同，`input` 决定后续编译输入来源，`reads` / `writes` 决定与前后 pass 的
attachment 依赖；分开
存就要在 `FrameGraph` 里维护"i-th name 对应 i-th target / resource flow"
的隐式索引，容易写出 off-by-one。

注意 FramePass 不持有任何 backend 资源（renderpass / framebuffer / pipeline
都在 backend 侧），也不持有 draw / dispatch payload。它纯粹是 core 层的
"这条 pass 的 graph 合同是什么，以及如何声明跨 pass attachment 读写"的
描述符。

### FrameGraph：加载期预构建的 per-pass 调度器

`FrameGraph` 是把 RenderPathGraph pass 合同收进一帧列表，并校验 pass 间资源
声明的入口。它的核心职责包括：

- 持有 `vector<FramePass>`：通过 `addPass` 累加，顺序是 declaration / original
  insertion order；真正的执行顺序由 `compile` 输出的 DAG order 决定
- 在 `compile` 时用 `GraphResourceRegistry` 校验 source / target 名称，
  将非 imported source 连接到对应 producer，并按资源依赖 DAG 排序 pass
- 编译排序的稳定兜底顺序是 phase、`stableOrder`、原始插入 index；phase
  约束保证 PreEffect 先于 Material、Material 先于 PostEffect、非 Debug 先于
Debug

注意它仍然不做 attachment 复用，也不持有 backend attachment 资源；这些都留给
backend 执行层。core 层这里只提供 registry-backed 资源依赖图、稳定 pass 顺序和
每条 pass 的 graph 合同。draw / dispatch input 和 pipeline-facing desc 由
FrameGraph::compile() 之后的 render work compiler 处理。

<!-- SOURCE_ANALYSIS:EXTRA -->

## frame_graph 子目录的三层视角

frame_graph 子目录里的三个源码分析页对应三个抽象层，从底向上读最顺：

| 层 | 页 | 关注点 |
|---|---|---|
| 形状 | [RenderTarget](render_target.md) | 一条 pass 的输出形状（attachment 配置） |
| 多 pass 调度 | 本页 FrameGraph | 把多条 pass 串起来，校验 source/target/resource DAG |
| per-pass 工单 | [RenderWorkCompiler](../../../concepts-design/rendering-pipeline/render-work-compiler.md) | 在 `compile()` 后生成 `RenderInput[]`、`RenderInputDesc[]`、binding plan 和 pipeline build desc |

读完三层就能回答："一条 RenderPathGraph pass 如何变成可验证的 FramePass，
FramePass 又如何变成 backend 能消费的 draw/dispatch input"这类问题。

## 与 backend 渲染图的边界

业界常说的"render graph / frame graph"通常还包括 backend attachment 生命周期、
自动 alias、barrier 推导和多 queue 同步。本仓库的 core `FrameGraph` 已经负责
resource registry 校验、producer/consumer 连接和 DAG 排序，但仍不持有 GPU memory，
也不推导 Vulkan barrier。

对应职责切分：

- **core/FrameGraph**：保存 pass 合同、校验 resource vocabulary、连接
  producer/consumer，并输出依赖排序后的 compiled pass 列表。
- **core/RenderWorkCompiler**：按 `FramePass.input` 生成 typed `RenderInput`，
  再把 shader、binding、resource dependency 和 `PipelineBuildDesc` 写入
  `RenderInputDesc`。
- **backend (e.g. VulkanRenderer / VulkanFrameGraphExecutor)**：决定怎么在 GPU
  上提交，包括 attachment、pipeline、descriptor、命令录制和 barrier。

这种切分让 core 层能表达跨 pass 资源真值，同时避免把 Vulkan / D3D / Metal 的具体资源状态机塞进 core。
