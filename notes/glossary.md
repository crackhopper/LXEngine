# 术语表

项目自造词的一句定义 + 出处。按字母排序。**不收录**标准 C++ / Vulkan 术语。

---

### `CameraData`

Camera 的 GPU 端数据（view/proj 矩阵 + eye 位置）。由 `Camera::getUBO()` 管理。
→ `src/core/scene/camera.hpp`

### `CombinedTextureSampler`

纹理 + sampler 的成对包装，**同时** 是一个 `IGpuResource`。material 的 descriptor 路径只接受 `CombinedTextureSamplerPtr`，不接受裸 `TexturePtr`。
→ `src/core/asset/texture.hpp:49`

### `FrameGraph`

描述场景启动阶段的渲染结构，包含多个 `FramePass`。提供 `buildFromScene(scene)` 和 `collectAllPipelineBuildDescs()`。
→ `src/core/frame_graph/frame_graph.hpp:22`

### `FramePass`

`FrameGraph` 的基本单元。包含 `StringID name`、`RenderTarget target`、`RenderQueue queue`。
→ `src/core/frame_graph/frame_graph.hpp:14`

### `GlobalStringTable`

单例字符串驻留表。提供字符串 ↔ `uint32_t` ID 的双向映射，以及结构化 `compose` / `decompose`。
→ `src/core/utils/string_table.hpp:44`

### `IRenderable`

场景对象的抽象接口。除了 buffer / descriptor / signature 访问，还提供 `supportsPass(pass)` 和 `getValidatedPassData(pass)`。
→ `src/core/scene/object.hpp:58`

### `IGpuResource`

所有 GPU 端可消费资源的基类。提供 `getType() / getRawData() / getByteSize() / getBindingName() / setDirty() / clearDirty()`。
→ `src/core/rhi/gpu_resource.hpp`

### `IShader`

Shader 的抽象接口。提供 stage、descriptor reflection、vertex input reflection 和按 name/location 的查询。
→ `src/core/asset/shader.hpp:111`

### `MaterialInstance`

当前唯一的材质类型。基于 shader 反射自动分配 std140 字节 buffer，并维护 pass enable 状态与 pass-state listeners。
→ `src/core/asset/material_instance.hpp`

### `MaterialInstancePtr`

`MaterialInstance` 的共享指针别名。当前 scene、queue 和 loader 的公共材质句柄统一使用这个名字，而不是裸写 `MaterialInstance::Ptr`。
→ `src/core/asset/material_instance.hpp`

### `MaterialTemplate`

材质蓝图。持有 template shader、每个 pass 的 `MaterialPassDefinition`，以及 template 级 binding cache。
→ `src/core/asset/material_template.hpp`

### `PerDrawData`

renderable 自带的每 draw 数据包装。底层缓冲 128 字节，但当前 ABI 只要求 `PerDrawLayoutBase` / `PerDrawLayout` 的 `model` 字段。
→ `src/core/scene/object.hpp:17`

### `Pass_Forward` / `Pass_Shadow` / `Pass_Deferred`

预设的 pass 常量，`inline const StringID`。
→ `src/core/frame_graph/pass.hpp`

### `PerDrawLayoutBase` / `PerDrawLayout`

engine-wide draw payload ABI。`PerDrawLayout` 现在只是 `PerDrawLayoutBase` 的别名，两者都只包含 `model`。
→ `src/core/rhi/gpu_resource.hpp`

### `PipelineBuildDesc`

backend 构建 pipeline 所需的完整输入包，通过 `fromRenderingItem(item)` 从 `RenderingItem` 派生。
→ `src/core/pipeline/pipeline_build_desc.hpp`

### `PipelineKey`

pipeline 的结构化身份，底层是一个 `StringID`，由 object/material 两侧 render signature compose 而成。
→ `src/core/pipeline/pipeline_key.hpp:11`

### `MaterialPassDefinition`

`MaterialTemplate` 每个 pass 的配置：`renderState` + `shaderSet` + 过渡性的 per-pass binding cache。
→ `src/core/asset/material_pass_definition.hpp`

### `RenderingItem`

一次 draw 的完整上下文值对象，由 `RenderQueue` 从 validated renderable 数据装配而成。
→ `src/core/scene/scene.hpp:13`

### `RenderQueue`

单个 pass 内的 draw call 队列。`buildFromScene(scene, pass, target)` 只消费 renderable 的 validated entry，并在末尾追加 scene-level 资源。
→ `src/core/frame_graph/render_queue.hpp:12`

### `RenderTarget`

render pass 附件集的描述（`colorFormat` + `depthFormat` + `sampleCount`）。目前不参与 `PipelineKey`。
→ `src/core/frame_graph/render_target.hpp`

### `SceneNode`

当前主路径的高层 renderable。要求 `nodeName + mesh + materialInstance` 必填，可选 `Skeleton`，并维护 `pass -> validated entry` 缓存。
→ `src/core/scene/object.hpp:75`

### `ShaderProgramSet`

`{shaderName, variants}` 的值类型。enabled variants 会参与 render signature 和 pipeline identity。
→ `src/core/asset/shader.hpp:161`

### `ShaderResourceBinding`

SPIR-V 反射出的 descriptor binding 描述，包含名字、set/binding、类型、stageFlags、UBO members 等信息。
→ `src/core/asset/shader.hpp:69`

### `Skeleton` / `SkeletonData`

骨骼动画资源及其 GPU 数据。现在只提供 runtime 数据，不再提供 pipeline identity token。
→ `src/core/asset/skeleton.hpp`

### `StringID`

`uint32_t` 的强类型封装。可从 `const char*` / `std::string` 隐式构造（构造即 intern）。
→ `src/core/utils/string_table.hpp`

### `MaterialParameterData`

`IGpuResource` 的一个实现，对 `std::vector<uint8_t>` 做非拥有包装，供 `MaterialInstance` 暴露参数缓冲数据。
→ `src/core/asset/material_instance.hpp`

### `ValidatedRenderablePassData`

`SceneNode` 的 pass 级结构缓存项，保存 queue 构造 `RenderingItem` 所需的稳定字段。
→ `src/core/scene/object.hpp:45`
