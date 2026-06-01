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

可以先带着一个问题阅读：为什么 `RenderingItem` 已经有 shader、material、
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

`PipelineKey` 故意只包一个结构化 `StringID`。它不保存 shader、render state、
vertex layout 的副本，而是要求调用方先把 object-side 和 material-side 的结构事实
各自归约成 signature，并在 RenderQueue 已知 render target 时把 target signature
一起传入这里做最后一次 compose。

当前 queue build 之后的完整形状是：

```text
PipelineKey(
  ObjectRender(mesh signature),
  MaterialRender(material pass signature),
  TargetRender(render target signature)
)
```

这样 cache lookup 的键很小，调试时又可以通过
`GlobalStringTable::toDebugString(key.id)` 展开整棵树。

## pipeline_build_desc.hpp

源码位置：[pipeline_build_desc.hpp](../../../../src/core/pipeline/pipeline_build_desc.hpp)

### PushConstantRange：当前固定 ABI 的占位描述

当前 forward draw path 的 push constant ABI 已收敛到 model-only 数据，但
backend-neutral 层仍用 `PushConstantRange` 描述“pipeline 创建时需要声明的范围”。

这里保存的是 pipeline layout 需要的结构信息，不是每个 draw 的实际 push constant
值。真实的 per-draw 数据在 `RenderingItem::drawData` / `PerDrawData` 路径上传。

### PipelineBuildDesc：从 RenderingItem 派生出的构建输入包

`PipelineKey` 只回答“是不是同一条 pipeline”；`PipelineBuildDesc` 回答
“如果这条 pipeline 还没建，backend 需要哪些输入”。

它从一个已经校验好的 `RenderingItem` 派生，不重新判断材质是否合法，也不重新推导
identity。这样前端的 SceneNode/RenderQueue 负责把 draw 事实准备好，backend 只负责把
这些事实翻译成 Vulkan pipeline 创建参数。

## pipeline_build_desc.cpp

源码位置：[pipeline_build_desc.cpp](../../../../src/core/pipeline/pipeline_build_desc.cpp)

### filterVertexLayoutToShaderInputs：让 pipeline 只声明 shader 真正读取的输入

Mesh 的 vertex layout 可能包含 shader 当前 pass 不读取的属性。pipeline 创建时如果把所有
属性都照搬进去，会让同一个 mesh 在不同 shader/pass 下的 vertex input state 过宽，也会增加
“shader 没声明但 pipeline 填了”的噪声。

这里按 shader reflection 得到的 vertex inputs 过滤 layout，只保留当前 shader 需要的
location/type。前置校验已经在 `SceneNode` 做过；这里的 assert 是为了保证
`PipelineBuildDesc::fromRenderingItem` 只消费已经通过验证的 item。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 补充说明

这里可以继续补充源码之外的上下文；脚本重跑时会保留这一节。
