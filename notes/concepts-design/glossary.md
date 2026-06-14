# 术语表：项目自造词的最短解释

术语表像地图图例。我们不在这里展开教程，只给出足够短的定义和继续阅读入口。标准 C++ / Vulkan 术语不收录，除非它在 LXEngine 里有特殊边界。

| 术语 | 一句话定义 | 继续阅读 |
|---|---|---|
| `CameraData` | Camera 上传给 shader 的 view/projection/eye 数据。 | [场景系统：相机](../scene-system/camera.md) |
| `CombinedTextureSampler` | 纹理和 sampler 的成对 GPU resource，材质 texture binding 使用它而不是裸 `Texture`。 | [资产系统](../concepts/assets/model-texture-material-flow.md) |
| `CommandBus` | editor 行为中线，UI、Console、API、recording 都通过它执行命令。 | [CommandBus 中线](../design/editor-system/02-command-first-surface.md) |
| `FrameGraph` | pass/target/queue 的组织骨架，realtime 和 offline 都通过它构建 work queue。 | [架构总览](architecture.md) |
| `FramePass` | `FrameGraph` 中的一个 pass，包含 pass name、target 和 render work queue。 | [材质多 Pass](../concepts/material/pass-rendering-flow.md) |
| `GlobalStringTable` | 全局字符串驻留表，把字符串映射成稳定 `StringID`。 | [源码分析](../source_analysis/src/core/utils/string_table.md) |
| `IGpuResource` | backend 可上传或绑定的 GPU resource 统一接口。 | [架构总览](architecture.md) |
| `IRenderable` | 能被 render queue 消费的场景对象接口。 | [场景系统：可渲染对象](../scene-system/renderable-object.md) |
| `IShader` | shader 抽象接口，暴露 stage、descriptor reflection、vertex input reflection。 | [材质系统](../concepts/material/index.md) |
| `MaterialContractReflection` | `.contract.glsl` metadata 的反射结果，描述 BSDF 参数、storage ABI 和 accessor ABI。 | [Material Contract v2](../concepts/material/material-contract-v2.md) |
| `MaterialInstance` | 运行时材质账本，持有 BSDF type、contract source signature、参数 envelope 和资源依赖。 | [MaterialInstance](../concepts/material/material-instance.md) |
| `Pass_Forward` / `Pass_Shadow` / `Pass_Deferred` | 预定义 pass 的 `StringID` 常量。 | [材质多 Pass](../concepts/material/pass-rendering-flow.md) |
| `PerDrawData` | 每个 draw 的小型数据包，当前主要承载 model matrix。 | [场景系统：可渲染对象](../scene-system/renderable-object.md) |
| `PipelineBuildDesc` | backend 构建 graphics / compute pipeline 所需的完整输入包。 | [Pipeline 是什么](../concepts/material/what-is-pipeline.md) |
| `PipelineKey` | pipeline cache 使用的结构化身份。 | [Contract 如何影响 Pipeline](../concepts/material/contract-and-pipeline.md) |
| `ProjectSession` | `lxe_editor` 当前 project 的打开、创建、active scene 和 dirty 状态管理器。 | [SceneRuntime 与持久化](../design/editor-system/04-scene-runtime-and-persistence.md) |
| `RenderWorkQueue` | 单个 pass 内的 work queue，把 realtime renderables 或 offline job 变成 `RenderWorkItem`。 | [架构总览](architecture.md) |
| `RenderWorkItem` | 一次 pipeline work 的 backend 消费上下文，当前支持 raster draw 和 compute dispatch。 | [Realtime / Offline 流水线](rendering-pipeline/realtime-offline-shared-flow.md) |
| `SceneDocument` | `.scene.yaml` 反序列化后的文档对象。 | [资产系统：场景文件](../concepts/assets/scene-assets.md) |
| `SceneNode` | 当前主路径的运行时节点，持有 transform、components 和 pass 级校验缓存。 | [场景系统：Node](../scene-system/node.md) |
| `SceneRuntime` | `lxe_editor` 中连接 scene document 和 runtime `Scene` 的装配层。 | [文档到 Runtime](../scene-system/document-runtime-flow.md) |
| `ShaderProgramSet` | material pass 中 `{shaderName, variants, shader}` 的值对象。 | [材质系统](../concepts/material/index.md) |
| `ShaderResourceBinding` | shader 反射出的 descriptor binding 描述。 | [材质系统](../concepts/material/index.md) |
| `Skeleton` / `SkeletonData` | 骨骼资源和它的 GPU 数据。 | [源码分析：Skeleton](../subsystems/skeleton.md) |
| `StringID` | `uint32_t` 封装的 interned string id。 | [源码分析](../source_analysis/src/core/utils/string_table.md) |
| `ValidatedRenderablePassData` | `SceneNode` 针对某个 pass 缓存的可绘制数据。 | [场景系统：可渲染对象](../scene-system/renderable-object.md) |

## 继续阅读

- [架构总览](architecture.md)
- [项目目录结构](project-layout.md)
- [源码分析入口](../source_analysis/index.md)
