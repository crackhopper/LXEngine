# 材质系统总览：从 SurfaceMaterial 到 RenderPathGraph

材质系统负责回答一个渲染对象“表面参数是什么”；RenderPathGraph 负责回答“应该怎样被画出来”：当前场景选择哪个 RenderPath、这张 graph 里跑哪些 pass、每个 pass 用哪份 shader、固定功能状态是什么、哪些参数和纹理由材质/feature 提供，哪些数据由场景系统注入。

可以把新模型理解成两份资产协作：`.material` / `SurfaceMaterial` 是表面参数单，`RenderPathGraph` 是渲染工艺单；`MaterialInstance` 保存这一份表面参数的运行时 identity，`RenderWorkQueue` 按 graph pass 生成具体 work item。

## 核心对象

| 对象 | 当前职责 | 类比 |
|---|---|---|
| `.material` / `SurfaceMaterial` | 声明 PBRT BSDF pure envelope、render class/tag 和资源引用，不声明 shader/pass | 点菜单 |
| `SurfaceMaterialResourceParser` | 校验 envelope、注册依赖资源、创建 base MaterialInstance | 后厨备料 |
| `SurfaceMaterialTemplate` | 保存 BSDF type 的参数 schema、layout 和校验规则 | 菜谱 |
| `RenderPathGraph` | 声明 RenderPath 下的 pass DAG、shader、source/target 和 render state | 工艺单 |
| `RenderPassNode` | 单个 pass 的 shader program、filter 和 render state | 工艺单里的一个步骤 |
| `RenderFeature` | shadowmap、SSAO、GI、tone mapping 等算法参数 envelope | 调味包 |
| `MaterialInstance` | 保存参数 envelope、typed resource handles、dirty/version | 上桌的菜 |
| `SceneNode` | 把 mesh + material 校验成 per-pass `ValidatedRenderablePassData` | 出菜前质检 |
| `RenderWorkItem` | 某个 pass 下的一次 pipeline work；realtime 通常是 draw，offline 可以是 dispatch | 一张出菜单 |
| `PipelineKey` | 判断 work item 能否复用同一条 pipeline 的结构化身份 | 厨房设备配置号 |

## 建议阅读顺序

1. [从 .material 到 MaterialInstance](file-to-instance.md)：先看资产文件如何进入运行时对象。
2. [模板与 Pass：材质的结构定义](template-blueprint.md)：理解 legacy `MaterialTemplate` 如何保存 pass；Material v2 会把 pass 移到 RenderPathGraph。
3. [Shader 在材质中的角色](shader.md)：看 shader reflection 如何决定参数、纹理和系统资源边界。
4. [内置 Shader 清单](shader-catalog.md)：逐个认识当前 GLSL shader 家族和它们所在的渲染流水线。
5. [MaterialInstance：运行时状态](material-instance.md)：看参数写入、纹理绑定、pass enable 如何保存。
6. [多 Pass 材质怎样变成 RenderWork](pass-rendering-flow.md)：把 scene validation、RenderWorkQueue、FrameGraph 串起来。
7. [什么是 Pipeline](what-is-pipeline.md)：建立 pipeline identity 的基本模型。
8. [模板如何影响 Pipeline](template-and-pipeline.md)：细分哪些材质变化会改变 pipeline，哪些不会。
9. [创建与排错自定义材质](custom-template.md)：把前面的概念落到 authoring 流程。
10. [未来路线：Bindless、Variants 与 FrameGraph](future-roadmap.md)：只讨论 roadmap，全部标注为尚未实施。
11. [Material Contract v2：SurfaceMaterial Pure Envelope 与 RenderPathGraph 分离](material-contract-v2.md)：讨论下一版材质 contract 的设计草案。

这个顺序刻意把 pipeline 放到后面。对新人来说，先知道数据从文件走到 instance，再理解一个 pass 如何变成 work item，最后再讨论“哪些结构会要求不同 pipeline”，会更接近代码实际执行顺序。

## 当前边界先记住

| 问题 | 当前答案 |
|---|---|
| 一个材质能有多个 technique 吗 | 不允许。Material v2 是 SurfaceMaterial pure envelope；Forward/Deferred/OfflineRT 在 RenderPathGraph 中表达 |
| 一个 RenderPath 能有多个 pass 吗 | 可以；Forward 可以是单 pass，也可以有 Shadow/Transparent/ToneMap；Deferred 可以有 GBuffer、Lighting、Transparent |
| `SurfaceMaterialTemplate` 保存 pass 吗 | 不保存；它只保存 BSDF schema 和参数布局 |
| 一个 work item 属于几个 pass | 一个 `RenderWorkItem` 只属于一个 pass；多 pass 材质会在不同 queue 里产生不同 item |
| `.material resources` 能写系统 UBO 吗 | 不能；它只写 material-owned texture binding 的默认值 |
| 参数值会影响 pipeline 吗 | 不会；参数写入 `ParameterBuffer`，不进入 `PipelineKey` |
| variants 会影响 pipeline 吗 | 会；enabled variants 进入 `ShaderProgramSet::getPipelineSignature()` |
| 当前 FrameGraph 会自动分析资源依赖吗 | 不会；当前只按 `FramePass` 顺序构建 per-pass queue |
| bindless 是否已经实现 | 尚未实现；当前仍是传统 descriptor resource 路径 |

## 正在收敛的 Material v2

Material v2 的设计草案把 `SurfaceMaterialTemplate`、`RenderPathGraph`、`RenderFeature` 和 `MaterialInstance` 的边界重新拆开：template 只表达 BSDF type 的参数 contract，RenderPathGraph 表达 pass graph 和 shader 绑定，RenderFeature 表达算法参数，instance 保存具体参数和资源 handle。这个设计仍在讨论中，先放在单独页面，避免和当前实现混在一起。

| 草案对象 | 设计职责 |
|---|---|
| `SurfaceMaterialTemplate` | `matte/glass/uber/metal/substrate/fourier/mix` 等 BSDF type 的参数 schema 和数据布局 |
| `RenderPathGraph` | Forward/Deferred/OfflineRT 的 pass、shader、source、target、render state |
| `RenderFeature` | shadowmap/SSAO/GI/tone mapping 等算法参数 envelope |
| `MaterialInstance` | 某份 material 文件或 scene override 后的参数值与资源 handle |

详见 [Material Contract v2](material-contract-v2.md)。

## 权威参考

- [MaterialTemplate 源码分析](../../source_analysis/src/core/asset/material_template.md)
- [MaterialInstance 源码分析](../../source_analysis/src/core/asset/material_instance.md)
- [RenderWorkQueue 源码分析](../../source_analysis/src/core/frame_graph/render_queue.md)
