# 术语表

项目自造词的一句定义 + 出处。按字母排序。**不收录**标准 C++ / Vulkan 术语。

---

### `CameraUBO`

Camera 的 GPU 端数据（view/proj 矩阵 + eye 位置）。`struct alignas(16) CameraUBO : IRenderResource`，由 `Camera::getUBO()` 管理。
→ `src/core/scene/camera.hpp:11`

### `CombinedTextureSampler`

纹理 + sampler 的成对包装，**同时** 是一个 `IRenderResource`（`Texture` 本身不是）。material 的 descriptor 路径只接受 `CombinedTextureSamplerPtr`，不接受裸 `TexturePtr`。
→ `src/core/resources/texture.hpp:49`

### `DrawMaterial`（已废弃）

曾是 `IMaterial` 的唯一实现，持有手写的 `BlinnPhongMaterialUBO` struct。**REQ-005 已彻底删除**，由 `MaterialInstance` 取代。注释或文档里提到它时应视为历史残留。

### `FramePass`

`FrameGraph` 的基本单元。包含 `StringID name`（例如 `Pass_Forward`）、`RenderTarget target`、`RenderQueue queue`。
→ `src/core/scene/frame_graph.hpp:14`

### `FrameGraph`

描述一帧的渲染结构，包含多个 `FramePass`。提供 `buildFromScene(scene)` 和 `collectAllPipelineBuildInfos()`，是 pipeline 预构建的入口。
→ `src/core/scene/frame_graph.hpp:22`

### `GlobalStringTable`

单例字符串驻留表。字符串 ↔ `uint32_t` ID 的双向映射 + 结构化 `compose` / `decompose`。线程安全（`shared_mutex`）。
→ `src/core/utils/string_table.hpp:44`

### `IMaterial`

材质的抽象接口。方法集：`getDescriptorResources() / getShaderInfo() / getPassFlag() / getRenderState() / getRenderSignature(pass)`。唯一实现是 `MaterialInstance`。
→ `src/core/resources/material.hpp:157`

### `IRenderable`

场景对象的抽象接口。提供顶点/索引 buffer、descriptor 资源列表、shader 信息、pass mask，以及 pass-aware 的 `getRenderSignature(pass)`。
→ `src/core/scene/object.hpp:50`

### `IRenderResource`

所有 GPU 端可消费资源的基类。提供 `getType() / getRawData() / getByteSize() / getBindingName() / setDirty() / clearDirty()`。
→ `src/core/gpu/render_resource.hpp:44`

### `IShader`

Shader 的抽象接口。提供 `getAllStages() / getReflectionBindings() / findBinding(name) / findBinding(set,binding)`。唯一实现是 `ShaderImpl`（infra 层）。
→ `src/core/resources/shader.hpp:110`

### `ImageFormat`

Core 层的格式枚举（`RGBA8` / `BGRA8` / `R8` / `D32Float` / `D24UnormS8` / `D32FloatS8`）。backend 做 `toVkFormat()` 映射；core 层**不**引用 `VkFormat`。
→ `src/core/gpu/image_format.hpp`

### `MaterialInstance`

唯一的 `IMaterial` 实现。基于 shader 反射自动分配 std140 字节 buffer，通过 `setVec3/setVec4/setFloat/setInt/setTexture` 按 `StringID` 写入。非拷贝非移动（因为 `m_uboResource` 持有指向 `m_uboBuffer` 的非拥有指针）。
→ `src/core/resources/material.hpp:272`

### `MaterialTemplate`

材质蓝图。持有 `IShaderPtr` + 若干 pass 的 `RenderPassEntry` + 一个 `StringID → ShaderResourceBinding` 的 binding cache。Pass key 类型为 `StringID`（REQ-007 之后）。
→ `src/core/resources/material.hpp:175`

### `Pass_Forward` / `Pass_Shadow` / `Pass_Deferred`

预设的 pass 常量，`inline const StringID`（不能 `constexpr`，因为构造 `StringID` 会 intern 到全局表）。
→ `src/core/scene/pass.hpp:9-11`

### `PipelineBuildInfo`

Backend 构建 pipeline 所需的**全部**数据（shader stages + 反射 bindings + vertex layout + render state + topology + push constant range）。通过 `fromRenderingItem(item)` 工厂从 `RenderingItem` 推导。完全 backend-agnostic。
→ `src/core/resources/pipeline_build_info.hpp:31`

### `PipelineCache`

Vulkan backend 的 pipeline 存储。提供 `find(key) / getOrCreate(buildInfo, renderPass) / preload(buildInfos, renderPass)`。与 `VulkanResourceManager` 解耦（见 REQ-003b）。
→ `src/backend/vulkan/details/pipelines/`

### `PipelineKey`

Pipeline 的结构化身份，底层是一个 `StringID`。通过 `GlobalStringTable::compose(TypeTag::PipelineKey, {objectSig, materialSig})` 产生。可通过 `GlobalStringTable::toDebugString(key.id)` 完整还原成人类可读的 pipeline tree。
→ `src/core/resources/pipeline_key.hpp:11`

### `PipelineSlotId`（已废弃）

曾是硬编码的 descriptor slot 枚举（`CameraUBO` / `MaterialUBO` / `SkeletonUBO` / `AlbedoTexture` / ...）。**REQ-003b 之后**，descriptor 路由改为按 `IRenderResource::getBindingName() → StringID` 匹配反射 binding 名字，该枚举被废弃。

### `PushConstantRange`

Core 层描述 push constant 的值类型（stageFlags + offset + size）。`PipelineBuildInfo.pushConstant` 的类型。目前由工厂注入引擎级约定（128 字节，vertex+fragment 可见）。
→ `src/core/resources/pipeline_build_info.hpp:18`

### `RenderableSubMesh`

`IRenderable` 的具体实现。持有一个 `MeshPtr`、一个 `MaterialPtr`、可选 `SkeletonPtr`、一个 `ObjectPCPtr`（push constant）。
→ `src/core/scene/object.hpp:69`

### `RenderingItem`

一次 draw call 的完整上下文值对象。字段：`shaderInfo / objectInfo / vertexBuffer / indexBuffer / descriptorResources / passMask / pass / pipelineKey / material`。由 `Scene::buildRenderingItem(pass)` 填充。
→ `src/core/scene/scene.hpp:14`

### `RenderPassEntry`

`MaterialTemplate` 每个 pass 对应的配置：`renderState` + `shaderSet` + `bindingCache`（per-pass 的 `name → ShaderResourceBinding`）。
→ `src/core/resources/material.hpp:114`

### `RenderQueue`

单个 pass 内的 draw call 队列。提供 `addItem / sort()`（按 PipelineKey 排序以减少 pipeline 切换）/ `collectUniquePipelineBuildInfos()`。
→ `src/core/scene/render_queue.hpp:12`

### `RenderState`

固定管线状态的值类型：`cullMode / depthTestEnable / depthWriteEnable / depthOp / blendEnable / srcBlend / dstBlend`。提供 `getRenderSignature()` 贡献 pipeline 身份。
→ `src/core/resources/material.hpp:68`

### `RenderTarget`

Render pass 附件集的描述（`colorFormat` + `depthFormat` + `sampleCount`）。提供 `getHash()`。**目前不参与 `PipelineKey`**，预留给未来多目标。
→ `src/core/gpu/render_target.hpp:11`

### `ResourcePassFlag`

资源的 pass 归属位标志（`Forward` / `Deferred` / `Shadow`）。用于从 `descriptorResources` 里筛选当前 pass 需要的资源。
→ `src/core/gpu/render_resource.hpp:11`

### `ResourceType`

`IRenderResource` 的类型枚举（`VertexBuffer` / `IndexBuffer` / `UniformBuffer` / `CombinedImageSampler` / `PushConstant` / `Shader` / ...）。backend 据此走不同路径。
→ `src/core/gpu/render_resource.hpp:32`

### `ShaderProgramSet`

`{shaderName, variants}` 的值类型。参与 `PipelineKey` 构建（通过 `getRenderSignature()`）。
→ `src/core/resources/shader.hpp:153`

### `ShaderResourceBinding`

SPIR-V 反射出的一个 descriptor binding 描述：`name / set / binding / type / descriptorCount / size / offset / stageFlags / members`。**REQ-004 起** `members` 字段描述 UBO block 的 std140 member 布局。
→ `src/core/resources/shader.hpp:73`

### `Skeleton` / `SkeletonUBO`

骨骼动画资源 + 对应的 GPU UBO（固定 128 根骨骼的 Mat4 数组）。`Skeleton::getRenderSignature()` 贡献"启用骨骼"到 pipeline 身份。
→ `src/core/resources/skeleton.hpp:89` / `:24`

### `StringID`

`uint32_t` 的强类型封装。可从 `const char*` / `std::string` 隐式构造（构造即 intern）。相等比较是 O(1) 整数对比，适合作为 `unordered_map` key。
→ `src/core/utils/string_table.hpp`

### `StructMemberInfo`

UBO block 内部单个成员的 std140 layout 描述：`name / type / offset / size`。由 `ShaderReflector` 通过 SPIRV-Cross 填充，存在 `ShaderResourceBinding.members` 中。REQ-004 引入。
→ `src/core/resources/shader.hpp:45`

### `TypeTag`

`GlobalStringTable` 的结构化 compose 标签（`String` / `ShaderProgram` / `RenderState` / `VertexLayout` / `VertexLayoutItem` / `MeshRender` / `Skeleton` / `RenderPassEntry` / `MaterialRender` / `ObjectRender` / `PipelineKey`）。leaf 字符串标为 `String`，compose 结果带对应 tag。
→ `src/core/utils/string_table.hpp:19`

### `UboByteBufferResource`

`IRenderResource` 的一个实现，对 `std::vector<uint8_t>` 做非拥有包装。`MaterialInstance` 用它把自己的 std140 字节 buffer 暴露给 descriptor sync 路径。REQ-005 引入。
→ `src/core/resources/material.hpp:247`

### `VertexFactory`

管理可复用的顶点类型注册表（`VertexPos` / `VertexPBR` / `VertexPosNormalUvBone` / `VertexSkinned` / ...）。每种类型通过 CRTP (`VertexBase<T>`) 自描述 layout。
→ `src/core/resources/vertex_buffer.hpp:207`

### `VertexLayout`

顶点布局（若干 `VertexLayoutItem` + 总 stride）。提供 `getRenderSignature()`（按 item 顺序结构化 compose + stride）贡献 pipeline 身份。
→ `src/core/resources/vertex_buffer.hpp:92`
