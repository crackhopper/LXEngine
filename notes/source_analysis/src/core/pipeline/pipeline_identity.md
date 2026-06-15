# Pipeline Identity：从结构签名到构建输入

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/pipeline/pipeline_key.hpp](../../../../../src/core/pipeline/pipeline_key.hpp)
和
[src/core/pipeline/pipeline_build_desc.hpp](../../../../../src/core/pipeline/pipeline_build_desc.hpp)
出发，解释 pipeline identity 的两层分工：
`PipelineKey` 负责回答“是不是同一条 pipeline”，`PipelineBuildDesc`
负责回答“如果要创建它，backend 需要哪些输入”。

可以先带着一个问题阅读：为什么 `RenderWorkItem` 已经有 shader、material、
vertex buffer，还要额外保存 `pipelineKey`？答案是，渲染提交和 pipeline
预构建都需要一个稳定、可哈希、可调试的 identity，而不是每次临时比较所有字段。

源码入口：[pipeline_key.hpp](../../../../src/core/pipeline/pipeline_key.hpp)

关联源码：

- [pipeline_key.cpp](../../../../src/core/pipeline/pipeline_key.cpp)
- [pipeline_build_desc.hpp](../../../../src/core/pipeline/pipeline_build_desc.hpp)
- [pipeline_build_desc.cpp](../../../../src/core/pipeline/pipeline_build_desc.cpp)

## pipeline_key.hpp

源码位置：[pipeline_key.hpp](../../../../src/core/pipeline/pipeline_key.hpp)

### PipelineKey：pipeline 身份的最终句柄

`PipelineKey` 故意只包一个结构化 `StringID`。073-c 之后它只接受两类已经
归约好的事实：

- MaterialTypeVariant：材质类型、source 契约和已解析 shader variant 身份。
- RenderPathNode：pass id、shader、render state、rendering mode、attachment
  contract、resource flow 和 geometry/topology 合约。

object/mesh 不再是 key 轴；它只参与 RenderPathNode geometry contract 的兼容性
校验。target 格式也不再作为独立 TargetRender 轴，而是由 RenderPathNode 的
attachment contract 包含。

## pipeline_build_desc.hpp

源码位置：[pipeline_build_desc.hpp](../../../../src/core/pipeline/pipeline_build_desc.hpp)

### PushConstantRange：当前固定 ABI 的占位描述

当前 pipeline layout 仍能描述 shader 反射得到的 push constant 范围；默认
render work 不再用它承载每 draw 数据。

这里保存的是 pipeline layout 需要的结构信息，不是每个 draw 的实际数据值。

### PipelineBuildDesc：从 RenderWorkItem 派生出的构建输入包

`PipelineKey` 只回答“是不是同一条 pipeline”；`PipelineBuildDesc` 回答
“如果这条 pipeline 还没建，backend 需要哪些输入”。

它从一个已经校验好的 `RenderWorkItem` 派生，不重新判断材质是否合法，也不重新推导
identity。这样前端的 SceneNode/RenderWorkQueue 负责把 draw 事实准备好，backend
只负责把 这些事实翻译成 Vulkan pipeline 创建参数。

## pipeline_build_desc.cpp

源码位置：[pipeline_build_desc.cpp](../../../../src/core/pipeline/pipeline_build_desc.cpp)

### filterVertexLayoutToShaderInputs：让 pipeline 只声明

shader 真正读取的输入 Mesh 的 vertex layout 可能包含 shader 当前 pass
不读取的属性。pipeline 创建时如果把所有 属性都照搬进去，会让同一个 mesh 在不同
shader/pass 下的 vertex input state 过宽，也会增加 “shader 没声明但 pipeline
填了”的噪声。

这里按 shader reflection 得到的 vertex inputs 过滤 layout，只保留当前 shader
需要的 location/type。前置校验已经在 `SceneNode` 做过；这里的 assert 是为了保证
`PipelineBuildDesc::fromRenderWorkItem` 只消费已经通过验证的 raster / compute
work item。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 补充说明

这里可以继续补充源码之外的上下文；脚本重跑时会保留这一节。
