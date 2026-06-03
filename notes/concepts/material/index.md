# 材质系统总览：从一份 .material 到一次 RenderWork

材质系统负责回答一个渲染对象“应该怎样被画出来”：用哪份 shader、跑哪些 pass、固定功能状态是什么、哪些参数和纹理由材质提供，哪些数据由场景系统注入。

我们可以把它想成厨房出菜流程：`.material` 文件是一张点菜单，`GenericMaterialLoader` 按菜单准备菜谱和默认配料；`MaterialTemplate` 是菜谱，规定有哪些步骤；`MaterialInstance` 是这一次真正端上桌的菜，保存具体调料和食材；`RenderWorkQueue` 则把每一道菜在每个步骤里的出菜顺序排好。

## 核心对象

| 对象 | 当前职责 | 类比 |
|---|---|---|
| `.material` | 声明 shader、variants、passes、默认参数和默认纹理 | 点菜单 |
| `GenericMaterialLoader` | 读取 YAML、编译 shader、反射 binding、创建 template 和 instance | 后厨备料 |
| `MaterialTemplate` | 定义 pass 结构、canonical material binding interface、pipeline signature | 菜谱 |
| `MaterialPassDefinition` | 单个 pass 的 shader program 和 render state | 菜谱里的一个步骤 |
| `MaterialInstance` | 保存参数字节、纹理资源、pass 启用状态 | 上桌的菜 |
| `SceneNode` | 把 mesh + material 校验成 per-pass `ValidatedRenderablePassData` | 出菜前质检 |
| `RenderWorkItem` | 某个 pass 下的一次 pipeline work；realtime 通常是 draw，offline 可以是 dispatch | 一张出菜单 |
| `PipelineKey` | 判断 work item 能否复用同一条 pipeline 的结构化身份 | 厨房设备配置号 |

## 建议阅读顺序

1. [从 .material 到 MaterialInstance](file-to-instance.md)：先看资产文件如何进入运行时对象。
2. [模板与 Pass：材质的结构定义](template-blueprint.md)：理解 `MaterialTemplate` 为什么是结构真值来源。
3. [Shader 在材质中的角色](shader.md)：看 shader reflection 如何决定参数、纹理和系统资源边界。
4. [内置 Shader 清单](shader-catalog.md)：逐个认识当前 GLSL shader 家族和它们所在的渲染流水线。
5. [MaterialInstance：运行时状态](material-instance.md)：看参数写入、纹理绑定、pass enable 如何保存。
6. [多 Pass 材质怎样变成 RenderWork](pass-rendering-flow.md)：把 scene validation、RenderWorkQueue、FrameGraph 串起来。
7. [什么是 Pipeline](what-is-pipeline.md)：建立 pipeline identity 的基本模型。
8. [模板如何影响 Pipeline](template-and-pipeline.md)：细分哪些材质变化会改变 pipeline，哪些不会。
9. [创建与排错自定义材质](custom-template.md)：把前面的概念落到 authoring 流程。
10. [未来路线：Bindless、Variants 与 FrameGraph](future-roadmap.md)：只讨论 roadmap，全部标注为尚未实施。

这个顺序刻意把 pipeline 放到后面。对新人来说，先知道数据从文件走到 instance，再理解一个 pass 如何变成 work item，最后再讨论“哪些结构会要求不同 pipeline”，会更接近代码实际执行顺序。

## 当前边界先记住

| 问题 | 当前答案 |
|---|---|
| 一个材质能有多个 pass 吗 | 可以，`MaterialTemplate` 以 `StringID pass` 保存多份 `MaterialPassDefinition` |
| 一个 work item 属于几个 pass | 一个 `RenderWorkItem` 只属于一个 pass；多 pass 材质会在不同 queue 里产生不同 item |
| `.material resources` 能写系统 UBO 吗 | 不能；它只写 material-owned texture binding 的默认值 |
| 参数值会影响 pipeline 吗 | 不会；参数写入 `ParameterBuffer`，不进入 `PipelineKey` |
| variants 会影响 pipeline 吗 | 会；enabled variants 进入 `ShaderProgramSet::getPipelineSignature()` |
| 当前 FrameGraph 会自动分析资源依赖吗 | 不会；当前只按 `FramePass` 顺序构建 per-pass queue |
| bindless 是否已经实现 | 尚未实现；当前仍是传统 descriptor resource 路径 |

## 权威参考

- [MaterialTemplate 源码分析](../../source_analysis/src/core/asset/material_template.md)
- [MaterialInstance 源码分析](../../source_analysis/src/core/asset/material_instance.md)
- [RenderWorkQueue 源码分析](../../source_analysis/src/core/frame_graph/render_queue.md)
- `openspec/specs/material-system/spec.md`
